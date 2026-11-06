#include "d3d9_renderer.hpp"
#include <d3dx9math.h>
#include <algorithm>
#include <cstring>

namespace cod4x::rml {
namespace {
template<class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
struct Vertex { float x, y, z; D3DCOLOR color; float u, v; };
constexpr DWORD VertexFormat = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

// State blocks do not include render targets, and transform restoration is explicit.
class SavedState {
public:
    explicit SavedState(IDirect3DDevice9* value) : device(value) {
        result = device->CreateStateBlock(D3DSBT_ALL, &block);
        if (FAILED(result)) return;
        result = block->Capture();
        if (FAILED(result)) return;
        device->GetRenderTarget(0, &target);
        device->GetDepthStencilSurface(&depth);
        device->GetViewport(&viewport);
        device->GetScissorRect(&scissor);
        device->GetTransform(D3DTS_WORLD, &world);
        device->GetTransform(D3DTS_VIEW, &view);
        device->GetTransform(D3DTS_PROJECTION, &projection);
        captured = true;
    }
    ~SavedState() {
        if (captured) {
            device->SetRenderTarget(0, target);
            device->SetDepthStencilSurface(depth);
            block->Apply();
            device->SetTransform(D3DTS_WORLD, &world);
            device->SetTransform(D3DTS_VIEW, &view);
            device->SetTransform(D3DTS_PROJECTION, &projection);
            device->SetViewport(&viewport);
            device->SetScissorRect(&scissor);
        }
        Release(block); Release(target); Release(depth);
    }
    HRESULT result = E_FAIL;
private:
    IDirect3DDevice9* device;
    IDirect3DStateBlock9* block = nullptr;
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* depth = nullptr;
    D3DVIEWPORT9 viewport = {};
    RECT scissor = {};
    D3DMATRIX world = {}, view = {}, projection = {};
    bool captured = false;
};
}

D3D9Renderer::~D3D9Renderer() { DeviceLost(); }

void D3D9Renderer::Clear() {
    for (auto& entry : geometries) { Release(entry.second.vertices); Release(entry.second.indices); }
    for (auto& entry : textures) Release(entry.second.texture);
    geometries.clear(); textures.clear(); current_device = nullptr;
}

void D3D9Renderer::DeviceLost() { std::lock_guard<std::mutex> lock(mutex); Clear(); }
void D3D9Renderer::Stats(rml_stats_t& out) {
    std::lock_guard<std::mutex> lock(mutex);
    out.rendered_frames = stats.rendered_frames; out.draw_calls = stats.draw_calls;
    out.geometry_uploads = stats.geometry_uploads; out.texture_uploads = stats.texture_uploads;
    out.last_render_result = stats.last_render_result;
}

void D3D9Renderer::Collect() {
    for (auto it = geometries.begin(); it != geometries.end();) {
        if (it->second.source.expired()) { Release(it->second.vertices); Release(it->second.indices); it = geometries.erase(it); }
        else ++it;
    }
    for (auto it = textures.begin(); it != textures.end();) {
        if (it->second.source.expired()) { Release(it->second.texture); it = textures.erase(it); }
        else ++it;
    }
}

HRESULT D3D9Renderer::DrawGeometry(IDirect3DDevice9* device, const Draw& draw) {
    auto& cached = geometries[draw.geometry.get()];
    const Geometry& geometry = *draw.geometry;
    HRESULT result;
    if (!cached.vertices) {
        result = device->CreateVertexBuffer(UINT(geometry.vertices.size() * sizeof(Vertex)), D3DUSAGE_WRITEONLY,
            VertexFormat, D3DPOOL_MANAGED, &cached.vertices, nullptr);
        if (FAILED(result)) return result;
        result = device->CreateIndexBuffer(UINT(geometry.indices.size() * sizeof(int)), D3DUSAGE_WRITEONLY,
            D3DFMT_INDEX32, D3DPOOL_MANAGED, &cached.indices, nullptr);
        if (FAILED(result)) { Release(cached.vertices); return result; }
        Vertex* vertices = nullptr;
        result = cached.vertices->Lock(0, 0, reinterpret_cast<void**>(&vertices), 0);
        if (FAILED(result)) { Release(cached.vertices); Release(cached.indices); return result; }
        for (size_t i = 0; i < geometry.vertices.size(); ++i) {
            const auto& v = geometry.vertices[i];
            vertices[i] = {v.position.x, v.position.y, 0,
                D3DCOLOR_ARGB(v.colour.alpha, v.colour.red, v.colour.green, v.colour.blue), v.tex_coord.x, v.tex_coord.y};
        }
        cached.vertices->Unlock();
        void* indices = nullptr;
        result = cached.indices->Lock(0, 0, &indices, 0);
        if (FAILED(result)) { Release(cached.vertices); Release(cached.indices); return result; }
        std::memcpy(indices, geometry.indices.data(), geometry.indices.size() * sizeof(int));
        cached.indices->Unlock(); cached.source = draw.geometry; ++stats.geometry_uploads;
    }
    IDirect3DTexture9* texture = nullptr;
    if (draw.texture) {
        auto& cached_texture = textures[draw.texture.get()];
        if (!cached_texture.texture) {
            const Texture& source = *draw.texture;
            result = device->CreateTexture(source.width, source.height, 1, 0, D3DFMT_A8R8G8B8,
                D3DPOOL_MANAGED, &cached_texture.texture, nullptr);
            if (FAILED(result)) return result;
            D3DLOCKED_RECT pixels;
            result = cached_texture.texture->LockRect(0, &pixels, nullptr, 0);
            if (FAILED(result)) { Release(cached_texture.texture); return result; }
            for (int y = 0; y < source.height; ++y) {
                auto* destination = reinterpret_cast<DWORD*>(static_cast<char*>(pixels.pBits) + y * pixels.Pitch);
                const auto* input = source.rgba.data() + size_t(y) * source.width * 4;
                for (int x = 0; x < source.width; ++x, input += 4)
                    destination[x] = D3DCOLOR_ARGB(input[3], input[0], input[1], input[2]);
            }
            cached_texture.texture->UnlockRect(0);
            cached_texture.source = draw.texture; ++stats.texture_uploads;
        }
        texture = cached_texture.texture;
    }
    device->SetTexture(0, texture);
    device->SetTextureStageState(0, D3DTSS_COLOROP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, texture ? D3DTOP_MODULATE : D3DTOP_SELECTARG2);
    D3DXMATRIX translation, world;
    D3DXMatrixTranslation(&translation, draw.translation.x, draw.translation.y, 0);
    // RmlUi's column-major bytes represent the transposed matrix D3D's row-vector convention needs.
    for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
        world.m[row][column] = draw.transform.GetColumn(row)[column];
    world = translation * world;
    device->SetTransform(D3DTS_WORLD, &world);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, draw.scissor_enabled);
    if (draw.scissor_enabled) {
        RECT rect = {draw.scissor.Left(), draw.scissor.Top(), draw.scissor.Right(), draw.scissor.Bottom()};
        device->SetScissorRect(&rect);
    }
    device->SetStreamSource(0, cached.vertices, 0, sizeof(Vertex));
    device->SetIndices(cached.indices);
    result = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, UINT(geometry.vertices.size()), 0, UINT(geometry.indices.size() / 3));
    if (SUCCEEDED(result)) ++stats.draw_calls;
    return result;
}

HRESULT D3D9Renderer::Render(IDirect3DDevice9* device, const Frame& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!device || frame.width <= 0 || frame.height <= 0) return E_INVALIDARG;
    if (device != current_device) { Clear(); current_device = device; }
    SavedState saved(device);
    if (FAILED(saved.result)) return stats.last_render_result = saved.result;
    Collect();
    D3DVIEWPORT9 viewport = {0, 0, DWORD(frame.width), DWORD(frame.height), 0, 1};
    device->SetViewport(&viewport);
    D3DXMATRIX identity, projection;
    D3DXMatrixIdentity(&identity);
    D3DXMatrixOrthoOffCenterLH(&projection, 0.5f, frame.width + 0.5f, frame.height + 0.5f, 0.5f, 0, 1);
    device->SetTransform(D3DTS_VIEW, &identity); device->SetTransform(D3DTS_PROJECTION, &projection);
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr); device->SetFVF(VertexFormat);
    device->SetRenderState(D3DRS_ZENABLE, FALSE); device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE); device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE); device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID); device->SetRenderState(D3DRS_CLIPPING, TRUE);
    device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD); device->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0); device->SetRenderState(D3DRS_WRAP0, 0);
    device->SetRenderState(D3DRS_VERTEXBLEND, D3DVBF_DISABLE); device->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE); device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15); device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    device->SetTextureStageState(0, D3DTSS_RESULTARG, D3DTA_CURRENT);
    device->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    HRESULT result = S_OK;
    for (const Draw& draw : frame.draws) { result = DrawGeometry(device, draw); if (FAILED(result)) break; }
    if (SUCCEEDED(result)) ++stats.rendered_frames;
    return stats.last_render_result = result;
}

}
