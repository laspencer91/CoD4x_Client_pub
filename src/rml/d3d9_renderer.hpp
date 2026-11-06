#pragma once
#include "frame.hpp"
#include "rml_host.h"
#include <d3d9.h>
#include <mutex>

namespace cod4x::rml {

class D3D9Renderer {
public:
    ~D3D9Renderer();
    HRESULT Render(IDirect3DDevice9* device, const Frame& frame);
    void DeviceLost();
    void Stats(rml_stats_t& stats);

private:
    struct GpuGeometry {
        std::weak_ptr<const Geometry> source;
        IDirect3DVertexBuffer9* vertices = nullptr;
        IDirect3DIndexBuffer9* indices = nullptr;
    };
    struct GpuTexture {
        std::weak_ptr<const Texture> source;
        IDirect3DTexture9* texture = nullptr;
    };
    std::mutex mutex;
    IDirect3DDevice9* current_device = nullptr; // Borrowed; engine owns the device.
    std::unordered_map<const Geometry*, GpuGeometry> geometries;
    std::unordered_map<const Texture*, GpuTexture> textures;
    rml_stats_t stats = {};
    void Clear();
    void Collect();
    HRESULT DrawGeometry(IDirect3DDevice9*, const Draw&);
};

}
