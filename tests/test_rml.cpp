#define NOMINMAX
#include "rml/rml_host.h"
#include "keycodes.h"
#include <windows.h>
#include <d3d9.h>
#include <d3dx9tex.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
bool fixture = true, missing_document = false;
int warnings = 0, hover_sounds = 0, click_sounds = 0;
const char* root = RML_SOURCE_ROOT;

void Require(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
    std::printf("PASS: %s\n", label);
}
void Check(HRESULT result, const char* label) {
    if (FAILED(result)) { std::fprintf(stderr, "HRESULT=0x%08lx\n", (unsigned long)result); Require(false, label); }
}
int ReadFile(const char* path, void** buffer) {
    *buffer = nullptr;
    if (missing_document && std::strcmp(path, "ui/rml/diagnostic.rml") == 0) return -1;
    if (std::strcmp(path, "ui/rml/images/probe.tga") == 0) {
        // Top-left red, top-right green, bottom-left blue, bottom-right white.
        const unsigned char tga[] = {0,0,2,0,0,0,0,0,0,0,0,0,2,0,2,0,32,0x28,
            0,0,255,255, 0,255,0,255, 255,0,0,255, 255,255,255,255};
        *buffer = std::malloc(sizeof(tga)); std::memcpy(*buffer, tga, sizeof(tga)); return sizeof(tga);
    }
    std::string file = std::string(root) + "/" +
        (fixture && std::strcmp(path, "ui/rml/diagnostic.rml") == 0 ? "tests/fixtures/rml_render.rml" : path);
    FILE* input = std::fopen(file.c_str(), "rb");
    if (!input) return -1;
    std::fseek(input, 0, SEEK_END); long length = std::ftell(input); std::rewind(input);
    if (length < 0) { std::fclose(input); return -1; }
    *buffer = std::malloc(size_t(length) + 1);
    const bool ok = std::fread(*buffer, 1, size_t(length), input) == size_t(length);
    std::fclose(input);
    if (!ok) { std::free(*buffer); *buffer = nullptr; return -1; }
    return int(length);
}
void Log(const char* message) { ++warnings; std::fprintf(stderr, "RmlUi: %s\n", message); }
void Sound(const char* alias) { if (!std::strcmp(alias, "mouse_over")) ++hover_sounds; else if (!std::strcmp(alias, "mouse_click")) ++click_sounds; }

struct Device {
    HWND window = nullptr;
    IDirect3D9* api = nullptr;
    IDirect3DDevice9* device = nullptr;
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* surface = nullptr;
    D3DPRESENT_PARAMETERS params = {};
    int width = 320, height = 240;

    Device() {
        window = CreateWindowA("STATIC", "RmlUi native render checks", WS_OVERLAPPEDWINDOW,
            0, 0, 1280, 800, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
        Require(window != nullptr, "hidden native test window");
        api = Direct3DCreate9(D3D_SDK_VERSION); Require(api != nullptr, "Direct3D9 available");
        std::printf("D3D9 adapters: %u\n", api->GetAdapterCount());
        params.Windowed = TRUE; params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        params.hDeviceWindow = window; params.BackBufferWidth = 1280; params.BackBufferHeight = 800;
        params.BackBufferFormat = D3DFMT_UNKNOWN; params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        HRESULT result = D3DERR_NOTAVAILABLE;
        for (UINT adapter = 0; adapter < api->GetAdapterCount() && !device; ++adapter) {
            D3DADAPTER_IDENTIFIER9 identity = {}; api->GetAdapterIdentifier(adapter, 0, &identity);
            D3DCAPS9 caps = {};
            std::printf("Adapter %u: %s caps=0x%08lx\n", adapter, identity.Description,
                (unsigned long)api->GetDeviceCaps(adapter, D3DDEVTYPE_HAL, &caps));
            result = api->CreateDevice(adapter, D3DDEVTYPE_HAL, window,
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &params, &device);
            if (FAILED(result)) result = api->CreateDevice(adapter, D3DDEVTYPE_HAL, window,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED, &params, &device);
        }
        if (result == D3DERR_NOTAVAILABLE) {
            std::fprintf(stderr, "SKIP_GPU: no D3D9 HAL device in this execution session. Run from the desktop.\n");
            std::exit(77);
        }
        Check(result, "create native D3D9 device");
        Target(width, height);
    }
    void ReleaseTarget() {
        IDirect3DSurface9* backbuffer = nullptr;
        Check(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer), "get backbuffer");
        device->SetRenderTarget(0, backbuffer); backbuffer->Release();
        if (surface) { surface->Release(); surface = nullptr; }
        if (texture) { texture->Release(); texture = nullptr; }
    }
    void Target(int w, int h) {
        if (surface) ReleaseTarget();
        width = w; height = h;
        Check(device->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr), "create UI render target");
        Check(texture->GetSurfaceLevel(0, &surface), "get target surface");
        Check(device->SetRenderTarget(0, surface), "bind target");
        device->SetDepthStencilSurface(nullptr);
    }
    uint32_t Render(double seconds) {
        DWORD scissor_enabled; device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissor_enabled);
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        Check(device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 255), 1, 0), "clear target");
        device->SetRenderState(D3DRS_SCISSORTESTENABLE, scissor_enabled);
        auto ticket = RmlHost_Record(width, height, seconds); Require(ticket != 0, "record immutable frame");
        Check(device->BeginScene(), "begin scene"); Check(RmlHost_Render(device, ticket), "render RmlUi frame");
        Check(device->EndScene(), "end scene");
        return ticket;
    }
    std::vector<DWORD> Pixels() {
        IDirect3DSurface9* readback = nullptr;
        Check(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &readback, nullptr), "create readback");
        Check(device->GetRenderTargetData(surface, readback), "read pixels");
        D3DLOCKED_RECT locked;
        Check(readback->LockRect(&locked, nullptr, D3DLOCK_READONLY), "lock pixels");
        std::vector<DWORD> pixels(size_t(width) * height);
        for (int y = 0; y < height; ++y) std::memcpy(pixels.data() + size_t(y) * width,
            static_cast<char*>(locked.pBits) + y * locked.Pitch, size_t(width) * 4);
        readback->UnlockRect(); readback->Release(); return pixels;
    }
    ~Device() {
        RmlHost_DeviceLost(); ReleaseTarget(); device->Release(); api->Release(); DestroyWindow(window);
    }
};
bool Near(DWORD pixel, int r, int g, int b) {
    return abs(int((pixel >> 16) & 255) - r) <= 2 && abs(int((pixel >> 8) & 255) - g) <= 2 && abs(int(pixel & 255) - b) <= 2;
}
}

int main(int argc, char** argv) {
    if (argc == 1) {
        std::freopen(RML_TEST_OUTPUT "/rml-desktop.log", "w", stdout);
        std::freopen(RML_TEST_OUTPUT "/rml-desktop-errors.log", "w", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
    }
    const rml_host_t host = {ReadFile, std::free, Log, Sound};
    Require(RmlHost_Init(&host), "initialize actual core and FreeType");
    if (argc > 1 && std::strcmp(argv[1], "--cpu") == 0) {
        fixture = false;
        Require(RmlHost_Show(), "load real diagnostic with packaged styles");
        auto ticket = RmlHost_Record(1280, 800, 1.0); Require(ticket != 0, "layout and record real document without a device"); RmlHost_Discard(ticket);
        RmlHost_MouseMove(960, 465); RmlHost_Record(1280, 800, 1.1);
        RmlHost_MouseMove(970, 466); Require(hover_sounds == 1, "one hover cue within a control");
        RmlHost_MouseMove(700, 700); RmlHost_Record(1280, 800, 1.2); RmlHost_MouseMove(960, 465);
        Require(hover_sounds == 2, "reentering a control plays its next hover cue");
        RmlHost_Key(K_MOUSE1, 1, 0); RmlHost_Key(K_MOUSE1, 0, 0);
        rml_stats_t state; RmlHost_Stats(&state); Require(state.activations == 1 && click_sounds == 1, "bound local action and selection cue");
        RmlHost_Key(K_ENTER, 1, 0); RmlHost_Key(K_ENTER, 0, 0);
        RmlHost_Stats(&state); Require(state.activations == 2 && click_sounds == 2, "Enter activates the focused native control");
        RmlHost_MouseMove(960, 510); RmlHost_Key(K_MOUSE1, 1, 0); RmlHost_Key(K_MOUSE1, 0, 0);
        RmlHost_Stats(&state); Require(state.activations == 2, "disabled native control rejects activation");
        RmlHost_MouseMove(960, 465);
        RmlHost_Key(K_MOUSE1, 1, 0); RmlHost_FocusLost(); Require(!RmlHost_Visible(), "hide while pointer is held");
        Require(RmlHost_Show(), "reopen after capture loss"); RmlHost_Key(K_MOUSE1, 0, 0);
        RmlHost_Stats(&state); Require(state.activations == 2, "release after focus loss cannot activate a stale control");
        RmlHost_Key(K_ESCAPE, 1, 0); Require(!RmlHost_Record(1280, 800, 1.3), "Escape stops recording");
        Require(RmlHost_Show(), "reopen after Escape");
        for (int i = 0; i < 12; ++i) RmlHost_Record(1280, 800, 2.0 + i * .01);
        RmlHost_Stats(&state); Require(state.pending_frames == 3, "bounded discarded-queue ownership");
        RmlHost_DeviceLost(); RmlHost_Stats(&state); Require(!state.pending_frames, "device loss invalidates pending frame tickets");
        Require(RmlHost_Reload(), "source reload retains bound model"); RmlHost_Stats(&state); Require(state.activations == 2, "model survives reload");
        missing_document = true; Require(!RmlHost_Reload() && !RmlHost_Visible(), "failed reload is not modal");
        Require(warnings <= 2, "no unexpected parse or style diagnostics"); RmlHost_Shutdown();
        std::printf("RML_CPU_CHECKS_PASSED\n"); return 0;
    }
    Require(RmlHost_Show(), "load pixel fixture through packaged filesystem interface");
    Device gpu;
    const DWORD expected_fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
    gpu.device->SetFVF(expected_fvf); gpu.device->SetRenderState(D3DRS_FOGENABLE, TRUE);
    gpu.device->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_FLAT);
    gpu.device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_MIRROR);
    const D3DVIEWPORT9 viewport = {3, 4, 300, 200, 0.2f, 0.8f}; gpu.device->SetViewport(&viewport);
    const RECT scissor = {8, 9, 250, 160}; gpu.device->SetScissorRect(&scissor);
    gpu.device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    auto ticket = gpu.Render(1.0);
    auto pixels = gpu.Pixels();
    std::printf("Clip pixels: inside=%08lx outside=%08lx\n", (unsigned long)pixels[110 * 320 + 20], (unsigned long)pixels[110 * 320 + 70]);
    D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-pixels.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr);
    Require(Near(pixels[20 * 320 + 20], 255, 0, 0), "untextured geometry and RGBA channel order");
    Require(Near(pixels[20 * 320 + 80], 0, 128, 127), "premultiplied transparency over scene color");
    Require(Near(pixels[110 * 320 + 20], 255, 0, 0) && Near(pixels[110 * 320 + 70], 0, 0, 255), "overflow scissor clipping");
    Require(Near(pixels[130 * 320 + 240], 255, 255, 0) && Near(pixels[130 * 320 + 210], 0, 0, 255), "GPU transform and translation convention");
    Require(Near(pixels[25 * 320 + 195], 255, 0, 0) && Near(pixels[25 * 320 + 245], 0, 255, 0) &&
        Near(pixels[75 * 320 + 195], 0, 0, 255) && Near(pixels[75 * 320 + 245], 255, 255, 255), "TGA texture orientation and color channels");
    size_t ink = 0;
    for (int y = 170; y < 205; ++y) for (int x = 10; x < 290; ++x) if (!Near(pixels[y * 320 + x], 0, 0, 255)) ++ink;
    Require(ink > 100, "real FreeType glyph atlas draws text");
    DWORD value = 0; gpu.device->GetFVF(&value); Require(value == expected_fvf, "restore native FVF");
    gpu.device->GetRenderState(D3DRS_FOGENABLE, &value); Require(value == TRUE, "restore native render state");
    gpu.device->GetRenderState(D3DRS_SHADEMODE, &value); Require(value == D3DSHADE_FLAT, "restore shade mode");
    gpu.device->GetSamplerState(0, D3DSAMP_ADDRESSU, &value); Require(value == D3DTADDRESS_MIRROR, "restore texture sampler");
    D3DVIEWPORT9 after; gpu.device->GetViewport(&after); Require(!std::memcmp(&after, &viewport, sizeof(after)), "restore viewport");
    RECT after_scissor; gpu.device->GetScissorRect(&after_scissor); Require(EqualRect(&scissor, &after_scissor), "restore scissor rectangle");
    Require(RmlHost_Render(gpu.device, ticket) == S_FALSE, "a frame ticket is consumed exactly once");
    rml_stats_t before = {}, after_stats = {}; RmlHost_Stats(&before);
    gpu.Render(1.1); RmlHost_Stats(&after_stats);
    Require(after_stats.geometry_uploads == before.geometry_uploads && after_stats.texture_uploads == before.texture_uploads, "reuse compiled GPU geometry and glyph textures");
    for (int i = 0; i < 9; ++i) RmlHost_Record(320, 240, 1.2 + i * 0.01);
    RmlHost_Stats(&after_stats); Require(after_stats.pending_frames == 3, "bounded retention when native queues are discarded");
    RmlHost_DeviceLost(); gpu.ReleaseTarget(); Check(gpu.device->Reset(&gpu.params), "actual D3D9 device reset"); gpu.Target(320, 240);
    gpu.Render(2.0); pixels = gpu.Pixels(); Require(Near(pixels[20 * 320 + 20], 255, 0, 0), "recover rendering after reset from retained CPU resources");
    RmlHost_Hide(); Require(!RmlHost_Record(320, 240, 2.1), "hidden UI records no frames");
    fixture = false; Require(RmlHost_Show() && RmlHost_Reload(), "reload actual diagnostic document and styles");
    gpu.Target(1280, 800); gpu.Render(3.0); gpu.Render(3.5);
    Check(D3DXSaveSurfaceToFileA(argc > 1 ? argv[1] : RML_TEST_OUTPUT "/rml-diagnostic.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "save native diagnostic capture");
    // Actual diagnostic: hover over SELECT, then move inside the same control and activate it.
    RmlHost_MouseMove(960, 465); gpu.Render(3.6); const int hover_count = hover_sounds;
    RmlHost_MouseMove(970, 466); gpu.Render(3.7); Require(hover_count == 1 && hover_sounds == hover_count, "moving within a control does not repeat hover audio");
    RmlHost_Key(K_MOUSE1, 1, 0); RmlHost_Key(K_MOUSE1, 0, 0); gpu.Render(3.8);
    RmlHost_Stats(&after_stats); Require(after_stats.activations == 1 && click_sounds == 1, "real document input updates binding and local selection cue");
    RmlHost_Key(K_ESCAPE, 1, 0); Require(!RmlHost_Visible() && !RmlHost_Record(1280, 800, 4.0), "Escape releases modal visibility");
    Require(RmlHost_Show(), "reopen diagnostic"); RmlHost_FocusLost(); Require(!RmlHost_Visible(), "focus loss hides modal surface");
    Require(RmlHost_Show(), "reopen before failed reload"); missing_document = true;
    Require(!RmlHost_Reload() && !RmlHost_Visible(), "missing document reload leaves no modal surface");
    Require(warnings <= 2, "diagnostic styles produce no unexpected RmlUi warnings");
    RmlHost_Shutdown(); Require(RmlHost_Init(&host), "runtime can initialize again after shutdown"); RmlHost_Shutdown();
    std::printf("RML_NATIVE_CHECKS_PASSED\n");
    return 0;
}
