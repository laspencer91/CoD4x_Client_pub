#include "rml_host.h"
#include "frame.hpp"
#include "d3d9_renderer.hpp"
#include "../keycodes.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace cod4x::rml {
namespace {
rml_host_t host = {};
double now = 0;

class System final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return now; }
    bool LogMessage(Rml::Log::Type, const Rml::String& message) override {
        if (host.log) host.log(message.c_str());
        return true;
    }
};

class Files final : public Rml::FileInterface {
    struct File { std::vector<unsigned char> bytes; size_t cursor = 0; };
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        // All UI resources use the same packaged namespace in the game and in native tests.
        if (path.compare(0, 7, "ui/rml/") || path.find("..") != path.npos ||
            path.find('\\') != path.npos || path.find(':') != path.npos || !host.read_file) return 0;
        void* bytes = nullptr;
        const int length = host.read_file(path.c_str(), &bytes);
        if (!bytes || length < 0 || length > 16 * 1024 * 1024) {
            if (bytes) host.free_file(bytes);
            return 0;
        }
        auto file = std::make_unique<File>();
        file->bytes.assign(static_cast<unsigned char*>(bytes), static_cast<unsigned char*>(bytes) + length);
        host.free_file(bytes);
        return reinterpret_cast<Rml::FileHandle>(file.release());
    }
    void Close(Rml::FileHandle handle) override { delete reinterpret_cast<File*>(handle); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle handle) override {
        auto& file = *reinterpret_cast<File*>(handle);
        size = std::min(size, file.bytes.size() - file.cursor);
        if (size) std::memcpy(buffer, file.bytes.data() + file.cursor, size);
        file.cursor += size;
        return size;
    }
    bool Seek(Rml::FileHandle handle, long offset, int origin) override {
        auto& file = *reinterpret_cast<File*>(handle);
        int64_t base;
        if (origin == SEEK_SET) base = 0;
        else if (origin == SEEK_CUR) base = file.cursor;
        else if (origin == SEEK_END) base = file.bytes.size();
        else return false;
        const int64_t position = base + offset;
        if (position < 0 || uint64_t(position) > file.bytes.size()) return false;
        file.cursor = size_t(position);
        return true;
    }
    size_t Tell(Rml::FileHandle handle) override { return reinterpret_cast<File*>(handle)->cursor; }
};

System system_interface;
Files file_interface;
Recorder recorder;
D3D9Renderer renderer;
Rml::Context* context = nullptr;
Rml::ElementDocument* document = nullptr;
Rml::DataModelHandle model;
bool initialized = false, visible = false, want_close = false;
std::set<Rml::Input::KeyIdentifier> pressed_keys;
bool pressed_buttons[5] = {};
int activations = 0;
Rml::String status = "Select a control to check focus and local audio.";
Rml::String focused_control;
double last_hover = -1;
std::mutex frames_mutex;
std::map<uint32_t, std::shared_ptr<const Frame>> frames;
uint32_t next_ticket = 1, recorded_frames = 0;

Rml::Element* Control(Rml::Element* element) {
    while (element && !(element->GetTagName() == "input" && element->GetAttribute<Rml::String>("type", "") == "button"))
        element = element->GetParentNode();
    return element;
}

class Events final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        if (!visible) return;
        Rml::Element* control = Control(event.GetTargetElement());
        if (!control || control->HasAttribute("disabled")) return;
        const auto& type = event.GetType();
        if (type == "mouseover" || type == "focus") {
            const auto id = control->GetId();
            if (id != focused_control) {
                focused_control = id;
                if (now - last_hover >= 0.075) { if (host.play_sound) host.play_sound("mouse_over"); last_hover = now; }
            }
            return;
        }
        if (type != "click") return;
        const auto action = control->GetAttribute<Rml::String>("data-action", "");
        if (action == "close") { want_close = true; return; }
        if (action != "select" && action != "replay") return;
        if (host.play_sound) host.play_sound("mouse_click");
        ++activations;
        status = action == "replay" ? "Motion replayed. The controls retain their hit boxes." : "Selection accepted locally. Economy transactions are not connected.";
        model.DirtyVariable("activations"); model.DirtyVariable("status");
        if (action == "replay") {
            auto* preview = document->GetElementById("motion-preview");
            if (preview) preview->Animate("opacity", Rml::Property(0.25f, Rml::Unit::NUMBER), 0.18f, Rml::Tween(Rml::Tween::Sine, Rml::Tween::InOut), 2, true);
        }
    }
} events;

Rml::Input::KeyIdentifier TranslateKey(int key) {
    using namespace Rml::Input;
    if (key >= 'a' && key <= 'z') return KeyIdentifier(KI_A + key - 'a');
    if (key >= 'A' && key <= 'Z') return KeyIdentifier(KI_A + key - 'A');
    if (key >= '0' && key <= '9') return KeyIdentifier(KI_0 + key - '0');
    switch (key) {
    case K_TAB: return KI_TAB; case K_ENTER: case K_KP_ENTER: return KI_RETURN;
    case K_ESCAPE: return KI_ESCAPE; case K_SPACE: return KI_SPACE; case K_BACKSPACE: return KI_BACK;
    case K_UPARROW: return KI_UP; case K_DOWNARROW: return KI_DOWN;
    case K_LEFTARROW: return KI_LEFT; case K_RIGHTARROW: return KI_RIGHT;
    case K_HOME: return KI_HOME; case K_END: return KI_END;
    case K_PGUP: return KI_PRIOR; case K_PGDN: return KI_NEXT;
    case K_DEL: return KI_DELETE; case K_INS: return KI_INSERT;
    case K_SHIFT: return KI_LSHIFT; case K_CTRL: return KI_LCONTROL; case K_ALT: return KI_LMENU;
    default: return KI_UNKNOWN;
    }
}
}
}

using namespace cod4x::rml;

extern "C" int RmlHost_Init(const rml_host_t* services) {
    if (initialized) return 1;
    if (!services || !services->read_file || !services->free_file) return 0;
    host = *services;
    Rml::SetSystemInterface(&system_interface); Rml::SetFileInterface(&file_interface); Rml::SetRenderInterface(&recorder);
    if (!Rml::Initialise()) return 0;
    initialized = true;
    context = Rml::CreateContext("cod4x", {1280, 800});
    if (!context) { RmlHost_Shutdown(); return 0; }
    auto constructor = context->CreateDataModel("diagnostic");
    constructor.Bind("status", &status); constructor.Bind("activations", &activations);
    model = constructor.GetModelHandle();
    if (!Rml::LoadFontFace("ui/rml/fonts/SourceSansPro-Regular.otf")) { RmlHost_Shutdown(); return 0; }
    return 1;
}

extern "C" void RmlHost_Shutdown() {
    if (!initialized) return;
    RmlHost_Hide();
    { std::lock_guard<std::mutex> lock(frames_mutex); frames.clear(); }
    renderer.DeviceLost();
    Rml::Shutdown();
    context = nullptr; document = nullptr; initialized = false; model = {};
}

extern "C" int RmlHost_Show() {
    if (!context) return 0;
    if (!document) {
        document = context->LoadDocument("ui/rml/diagnostic.rml");
        if (!document) return 0;
        for (const char* type : {"click", "mouseover", "focus"}) document->AddEventListener(type, &events, true);
    }
    want_close = false; visible = true; focused_control.clear(); last_hover = now;
    document->Show();
    return 1;
}

extern "C" void RmlHost_Hide() {
    visible = false; want_close = false;
    if (context) {
        for (auto key : pressed_keys) context->ProcessKeyUp(key, 0);
        for (int button = 0; button < 5; ++button) if (pressed_buttons[button]) context->ProcessMouseButtonUp(button, 0);
    }
    pressed_keys.clear(); std::fill(std::begin(pressed_buttons), std::end(pressed_buttons), false);
    if (document) document->Hide();
    if (context) context->ProcessMouseLeave();
    focused_control.clear();
}
extern "C" int RmlHost_Visible() { return visible && !want_close; }

extern "C" int RmlHost_Reload() {
    if (!context) return 0;
    const bool was_visible = visible;
    RmlHost_Hide();
    if (document) {
        for (const char* type : {"click", "mouseover", "focus"}) document->RemoveEventListener(type, &events, true);
        document->Close(); document = nullptr; context->Update();
    }
    Rml::Factory::ClearStyleSheetCache(); Rml::Factory::ClearTemplateCache();
    return was_visible ? RmlHost_Show() : 1;
}

extern "C" void RmlHost_MouseMove(int x, int y) {
    if (!RmlHost_Visible()) return;
    context->ProcessMouseMove(x, y, 0);
    if (!Control(context->GetHoverElement())) focused_control.clear();
}
extern "C" void RmlHost_Text(uint32_t value) {
    if (RmlHost_Visible() && value >= 32 && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff)) context->ProcessTextInput(Rml::Character(value));
}
extern "C" void RmlHost_FocusLost() { RmlHost_Hide(); }

extern "C" void RmlHost_Key(int key, int down, int modifiers) {
    if (!RmlHost_Visible()) return;
    if (key == K_ESCAPE && down) { want_close = true; return; }
    if (key >= K_MOUSE1 && key <= K_MOUSE5) {
        pressed_buttons[key - K_MOUSE1] = down != 0;
        if (down) context->ProcessMouseButtonDown(key - K_MOUSE1, modifiers);
        else context->ProcessMouseButtonUp(key - K_MOUSE1, modifiers);
    } else if (key == K_MWHEELDOWN || key == K_MWHEELUP) {
        if (down) context->ProcessMouseWheel(key == K_MWHEELUP ? -1.f : 1.f, modifiers);
    } else {
        const auto translated = TranslateKey(key);
        if (translated != Rml::Input::KI_UNKNOWN) {
            if (down) pressed_keys.insert(translated); else pressed_keys.erase(translated);
            if (down) context->ProcessKeyDown(translated, modifiers);
            else context->ProcessKeyUp(translated, modifiers);
        }
    }
}

extern "C" uint32_t RmlHost_Record(int width, int height, double seconds) {
    if (want_close) { RmlHost_Hide(); return 0; }
    if (!visible || !context || width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0;
    now = std::max(now, seconds);
    context->SetDimensions({width, height});
    context->SetDensityIndependentPixelRatio(std::max(0.5f, std::min(width / 1280.f, height / 800.f)));
    context->Update();
    recorder.Begin(width, height); context->Render(); auto frame = recorder.End();
    std::lock_guard<std::mutex> lock(frames_mutex);
    // A discarded native command queue cannot retain an unbounded number of frames.
    if (frames.size() >= 3) frames.erase(frames.begin());
    uint32_t ticket = next_ticket++;
    if (!ticket) ticket = next_ticket++;
    frames.emplace(ticket, std::move(frame)); ++recorded_frames;
    return ticket;
}
extern "C" void RmlHost_Discard(uint32_t ticket) { std::lock_guard<std::mutex> lock(frames_mutex); frames.erase(ticket); }
extern "C" int32_t RmlHost_Render(void* device, uint32_t ticket) {
    std::shared_ptr<const Frame> frame;
    { std::lock_guard<std::mutex> lock(frames_mutex);
      auto found = frames.find(ticket); if (found == frames.end()) return S_FALSE;
      frame = std::move(found->second); frames.erase(found); }
    return renderer.Render(static_cast<IDirect3DDevice9*>(device), *frame);
}
extern "C" void RmlHost_DeviceLost() {
    renderer.DeviceLost();
    std::lock_guard<std::mutex> lock(frames_mutex); frames.clear();
}
extern "C" void RmlHost_Stats(rml_stats_t* out) {
    if (!out) return;
    *out = {}; renderer.Stats(*out);
    out->visible = RmlHost_Visible(); out->activations = activations;
    std::lock_guard<std::mutex> lock(frames_mutex);
    out->recorded_frames = recorded_frames; out->pending_frames = uint32_t(frames.size());
}
