#include "avatars.hpp"
#include "view_support.hpp"
#include <windows.h>
#include <initguid.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace cod4x::rml::avatars {
namespace {
constexpr const char* kPrefix = "ui/rml/avatars/";
constexpr const wchar_t* kExtensions[] = {L".png", L".jpg"};
constexpr double kRecheckSeconds = 5;
constexpr size_t kMaxFileBytes = 1024 * 1024;
constexpr UINT kMaxSide = 128, kMaxSourceSide = 2048;

enum class State { Queued, Missing, Ready };
struct Entry { State state = State::Queued; double checked = 0; std::vector<unsigned char> tga; };

std::mutex mutex;
std::condition_variable wake;
bool worker_started = false; // one detached worker for the process; it idles without work
std::wstring directory;
std::map<Rml::String, Entry> entries; // by key
std::deque<Rml::String> queue;

std::wstring Wide(const Rml::String& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0);
    std::wstring wide(size_t(std::max(length, 0)), L'\0');
    if (length > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), int(text.size()), &wide[0], length);
    return wide;
}
template <class T> void Release(T*& value) { if (value) value->Release(); value = nullptr; }

bool ReadFileBytes(const std::wstring& path, std::vector<unsigned char>& bytes) {
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    DWORD read = 0;
    bool ok = GetFileSizeEx(handle, &size) && size.QuadPart > 0 && uint64_t(size.QuadPart) <= kMaxFileBytes;
    if (ok) { bytes.resize(size_t(size.QuadPart)); ok = ReadFile(handle, bytes.data(), DWORD(bytes.size()), &read, nullptr) && read == bytes.size(); }
    CloseHandle(handle);
    return ok;
}

// PNG or JPEG bytes to a top-down 32-bit TGA of the centred square, at most kMaxSide on a side.
bool Decode(IWICImagingFactory* factory, std::vector<unsigned char>& bytes, std::vector<unsigned char>& tga) {
    IWICStream* stream = nullptr; IWICBitmapDecoder* decoder = nullptr; IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapClipper* clipper = nullptr; IWICBitmapScaler* scaler = nullptr; IWICFormatConverter* converter = nullptr;
    UINT width = 0, height = 0;
    bool ok = factory && SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory(bytes.data(), DWORD(bytes.size()))) &&
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&width, &height)) &&
        width > 0 && height > 0 && width <= kMaxSourceSide && height <= kMaxSourceSide;
    const UINT side = std::min(width, height), target = std::min(side, kMaxSide);
    const WICRect square = {INT((width - side) / 2), INT((height - side) / 2), INT(side), INT(side)};
    // Straight-alpha BGRA is the TGA pixel layout; the texture loader premultiplies.
    ok = ok && SUCCEEDED(factory->CreateBitmapClipper(&clipper)) && SUCCEEDED(clipper->Initialize(frame, &square)) &&
        SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
        SUCCEEDED(scaler->Initialize(clipper, target, target, WICBitmapInterpolationModeFant)) &&
        SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(scaler, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
            WICBitmapPaletteTypeCustom));
    if (ok) {
        const unsigned char header[18] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, (unsigned char)(target & 255),
            (unsigned char)(target >> 8), (unsigned char)(target & 255), (unsigned char)(target >> 8), 32, 0x28};
        tga.assign(header, header + 18);
        tga.resize(18 + size_t(target) * target * 4);
        ok = SUCCEEDED(converter->CopyPixels(nullptr, target * 4, UINT(tga.size() - 18), tga.data() + 18));
    }
    Release(converter); Release(scaler); Release(clipper); Release(frame); Release(decoder); Release(stream);
    return ok;
}

void Work() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IWICImagingFactory* factory = nullptr;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, reinterpret_cast<void**>(&factory));
    std::unique_lock<std::mutex> lock(mutex);
    for (;;) {
        wake.wait(lock, [] { return !queue.empty(); });
        const Rml::String key = queue.front(); queue.pop_front();
        const std::wstring folder = directory;
        lock.unlock();
        std::vector<unsigned char> bytes, tga;
        bool found = false;
        for (const wchar_t* extension : kExtensions)
            if (ReadFileBytes(folder + L"\\" + Wide(key) + extension, bytes)) { found = Decode(factory, bytes, tga); break; }
        lock.lock();
        auto entry = entries.find(key);
        if (entry == entries.end()) continue; // the folder changed meanwhile
        entry->second.state = found ? State::Ready : State::Missing;
        entry->second.tga = std::move(tga);
    }
}
}

void SetDirectory(const char* utf8_directory) {
    std::lock_guard<std::mutex> lock(mutex);
    directory = utf8_directory && *utf8_directory ? Wide(utf8_directory) : std::wstring();
    entries.clear(); queue.clear();
}

void Shutdown() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear(); queue.clear();
}

Rml::String Key(const Rml::String& name) {
    Rml::String key;
    for (const unsigned char c : StripColors(name)) {
        if (key.size() == 48) break;
        key += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ? char(c) : c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : '_';
    }
    return key.find_first_not_of('_') == Rml::String::npos ? Rml::String() : key;
}

Rml::String Source(const Rml::String& name, double now) {
    const auto key = Key(name);
    std::lock_guard<std::mutex> lock(mutex);
    if (key.empty() || directory.empty()) return {};
    auto found = entries.find(key);
    if (found == entries.end() || (found->second.state == State::Missing && now - found->second.checked >= kRecheckSeconds)) {
        auto& entry = entries[key];
        entry.state = State::Queued; entry.checked = now;
        queue.push_back(key);
        if (!worker_started) { std::thread(Work).detach(); worker_started = true; }
        wake.notify_one();
        return {};
    }
    return found->second.state == State::Ready ? "avatars/" + key : Rml::String();
}

Rml::String Ghost(const Rml::String& source) {
    return source.compare(0, 8, "avatars/") == 0 ? "avatars/ghost/" + source.substr(8) : Rml::String();
}

bool IsAvatarPath(const Rml::String& path) { return path.compare(0, std::char_traits<char>::length(kPrefix), kPrefix) == 0; }

bool Read(const Rml::String& path, std::vector<unsigned char>& bytes) {
    Rml::String key = path.substr(std::char_traits<char>::length(kPrefix));
    const bool ghost = key.compare(0, 6, "ghost/") == 0;
    if (ghost) key.erase(0, 6);
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto entry = entries.find(key);
        if (entry == entries.end() || entry->second.state != State::Ready) return false;
        bytes = entry->second.tga;
    }
    if (!ghost) return true;
    // Desaturate most of the way, keep about a third of the alpha, fade in over the left fifth and out over the right
    // half (smoothstep ramps), so the image dissolves into the plate behind the text.
    const int side = bytes[12] | bytes[13] << 8;
    auto ramp = [](float t) { t = std::max(0.f, std::min(1.f, t)); return t * t * (3 - 2 * t); };
    for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
        unsigned char* p = bytes.data() + 18 + (size_t(y) * side + x) * 4; // BGRA
        const float u = (x + .5f) / side, grey = .11f * p[0] + .59f * p[1] + .3f * p[2];
        for (int c = 0; c < 3; ++c) p[c] = (unsigned char)std::lround(grey * .7f + p[c] * .3f);
        p[3] = (unsigned char)std::lround(p[3] * .34f * ramp(u / .2f) * (1 - ramp((u - .5f) / .5f)));
    }
    return true;
}
}
