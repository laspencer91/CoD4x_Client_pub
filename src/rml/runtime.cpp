#include "rml_host.h"
#include "frame.hpp"
#include "d3d9_renderer.hpp"
#include "source.hpp"
#include "effects.hpp"
#include "decrypt_text.hpp"
#include "toggle.hpp"
#include "modal.hpp"
#include "economy.hpp"
#include "view_support.hpp"
#include "ui_audio.hpp"
#include "broadcast.hpp"
#include "player_name.hpp"
#include "lag_graph.hpp"
#include "avatars.hpp"
#include "native_keys.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

namespace cod4x::rml {
namespace {
rml_host_t host = {};
double now = 0;
Source source;
Source mod_source;
EconomyView economy;
BroadcastView broadcast;
bool broadcast_enabled=false, broadcast_ready=false;
bool economy_screen = false, economy_preview = false;
bool fonts_loaded = false;
bool font_loaded[4] = {};
double fonts_retry_at = 0;
uint32_t source_reloads = 0, source_failures = 0;

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
        if (avatars::IsAvatarPath(path)) { // player images: decoded from a local folder, never the game's file system
            auto file = std::make_unique<File>();
            return avatars::Read(path, file->bytes) ? reinterpret_cast<Rml::FileHandle>(file.release()) : 0;
        }
        if (source.Enabled() || mod_source.Enabled()) {
            auto file = std::make_unique<File>();
            if (!mod_source.Read(path, file->bytes) && !source.Read(path, file->bytes)) return 0;
            return reinterpret_cast<Rml::FileHandle>(file.release());
        }
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
const char* document_path = "ui/rml/diagnostic.rml";
Rml::DataModelHandle model;
bool initialized = false, visible = false, want_close = false;
bool want_replay = false;
std::set<Rml::Input::KeyIdentifier> pressed_keys;
std::bitset<256> held_keys; // native keys whose key-down arrived; Windows repeats a held key's key-down
bool pressed_buttons[5] = {};
int activations = 0;
bool toggle_demo = false;
Rml::String status = "Select a control to check focus and local audio.";
UiAudio palette_audio;
std::mutex frames_mutex;
std::map<uint32_t, std::shared_ptr<const Frame>> frames;
uint32_t next_ticket = 1, recorded_frames = 0;
// Average cost per frame since rml_status last asked: recording (main thread) and drawing (render thread, under
// frames_mutex).
struct FrameCost {
    double total = 0; uint32_t count = 0;
    void Add(std::chrono::steady_clock::time_point started) {
        total += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count(); ++count;
    }
    uint32_t Take() { const uint32_t average = count ? uint32_t(total / count + .5) : 0; total = 0; count = 0; return average; }
};
FrameCost record_cost, render_cost;

Rml::Element* Control(Rml::Element* element) {
    while (element && !(element->GetTagName() == "input" && element->GetAttribute<Rml::String>("type", "") == "button"))
        element = element->GetParentNode();
    return element;
}

class Events final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override {
        if (!visible) return;
        if (event.GetType() == "modalclose") {
            const auto action = event.GetTargetElement()->GetAttribute<Rml::String>("dismiss-action", "");
            if (economy_screen) economy.Dismiss(action, host, economy_preview, now);
            else if (action == "close") want_close = true;
            return;
        }
        if (economy_screen) {
            if (economy.Event(event, host, economy_preview, now)) want_close = true;
            return;
        }
        Rml::Element* control = Control(event.GetTargetElement());
        if (!control || control->HasAttribute("disabled")) return;
        const auto& type = event.GetType();
        if (type == "mouseover" || type == "focus") {
            palette_audio.Hover(control->GetId(), true, host, now);
            return;
        }
        if (type != "click") return;
        const auto action = control->GetAttribute<Rml::String>("data-action", "");
        if (action == "close") { want_close = true; return; }
        if (action == "toggle-demo") {
            toggle_demo = !toggle_demo; model.DirtyVariable("toggle_demo");
            if (host.play_sound) host.play_sound("mouse_click");
            return;
        }
        if (action == "replay-decrypt" || action == "decrypt-speed") {
            if (action == "decrypt-speed") {
                Rml::ElementList elements; document->GetElementsByTagName(elements, "decrypt-text");
                for (auto* element : elements) element->SetAttribute("tick-ms", element->GetAttribute<int>("tick-ms", 18) == 18 ? 45 : 18);
                const bool slow = !elements.empty() && elements.front()->GetAttribute<int>("tick-ms", 18) == 45;
                control->SetAttribute("value", slow ? "NORMAL SPEED" : "SLOW PREVIEW");
            }
            ReplayDecryptText(document);
            if (host.play_sound) host.play_sound("mouse_click");
            return;
        }
        if (action != "select" && action != "replay") return;
        if (host.play_sound) host.play_sound("mouse_click");
        ++activations;
        status = action == "replay" ? "Motion replayed. The controls retain their hit boxes." : "Selection accepted locally. Economy transactions are not connected.";
        model.DirtyVariable("activations"); model.DirtyVariable("status");
        if (action == "replay") {
            want_replay = true;
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
    case RML_KEY_TAB: return KI_TAB; case RML_KEY_ENTER: case RML_KEY_KP_ENTER: return KI_RETURN;
    case RML_KEY_ESCAPE: return KI_ESCAPE; case RML_KEY_SPACE: return KI_SPACE; case RML_KEY_BACKSPACE: return KI_BACK;
    case RML_KEY_UPARROW: case RML_KEY_KP_UPARROW: return KI_UP;
    case RML_KEY_DOWNARROW: case RML_KEY_KP_DOWNARROW: return KI_DOWN;
    case RML_KEY_LEFTARROW: case RML_KEY_KP_LEFTARROW: return KI_LEFT;
    case RML_KEY_RIGHTARROW: case RML_KEY_KP_RIGHTARROW: return KI_RIGHT;
    case RML_KEY_HOME: case RML_KEY_KP_HOME: return KI_HOME;
    case RML_KEY_END: case RML_KEY_KP_END: return KI_END;
    case RML_KEY_PGUP: case RML_KEY_KP_PGUP: return KI_PRIOR;
    case RML_KEY_PGDN: case RML_KEY_KP_PGDN: return KI_NEXT;
    case RML_KEY_DEL: case RML_KEY_KP_DEL: return KI_DELETE;
    case RML_KEY_INS: case RML_KEY_KP_INS: return KI_INSERT;
    case RML_KEY_SHIFT: return KI_LSHIFT; case RML_KEY_CTRL: return KI_LCONTROL; case RML_KEY_ALT: return KI_LMENU;
    default: return KI_UNKNOWN;
    }
}
}
}

using namespace cod4x::rml;

namespace {
// The fonts come from the mod: the base font in rml_shared.iwd, the game fonts in pmflex.iwd. Missing ones (a mod
// package without them) are retried every few seconds, not every frame, by the screens that need them; a face that
// already loaded is never registered twice.
constexpr const char* kBaseFont = "ui/rml/fonts/SourceSansPro-Regular.otf";
bool LoadFonts() {
    if (fonts_loaded) return true;
    const double now = Rml::GetSystemInterface()->GetElapsedTime();
    if (now < fonts_retry_at) return false;
    const struct { const char* path; Rml::Style::FontWeight weight; } fonts[] = {
        {kBaseFont, Rml::Style::FontWeight::Auto},
        {"ui/rml/fonts/SairaCondensed-Regular.ttf", Rml::Style::FontWeight::Auto},
        {"ui/rml/fonts/SairaCondensed-SemiBold.ttf", Rml::Style::FontWeight::Bold},
        {"ui/rml/fonts/ShareTechMono-Regular.ttf", Rml::Style::FontWeight::Auto}};
    fonts_loaded = true;
    for (int i = 0; i < 4; ++i) {
        if (!font_loaded[i]) font_loaded[i] = Rml::LoadFontFace(fonts[i].path, false, fonts[i].weight);
        fonts_loaded = fonts_loaded && font_loaded[i];
    }
    if (!fonts_loaded) fonts_retry_at = now + 5;
    return fonts_loaded;
}
// Only navigation and editing keys act on a held key's repeated key-downs. A held hotkey, Enter or Space (which
// RmlUi turns into a click) acts once, so holding X cannot buy and refund a frag over and over.
bool RepeatAllowed(int key) {
    using namespace Rml::Input;
    switch (TranslateKey(key)) {
    case KI_UP: case KI_DOWN: case KI_LEFT: case KI_RIGHT: case KI_PRIOR: case KI_NEXT: case KI_BACK: case KI_DELETE: case KI_TAB:
        return true;
    default: return false;
    }
}
}
extern "C" void RmlHost_EnableBroadcast(int enabled) { broadcast_enabled=enabled!=0; if (!enabled) broadcast_ready=false; }
extern "C" int RmlHost_BroadcastReady() { return broadcast_ready; }
extern "C" int RmlHost_BroadcastFollowShown() { return broadcast_ready && broadcast.FollowShown(); }

extern "C" int RmlHost_Init(const rml_host_t* services) {
    if (initialized) return 1;
    if (!services || !services->read_file || !services->free_file) return 0;
    host = *services;
    now = host.elapsed_time ? host.elapsed_time() : 0;
    Rml::SetSystemInterface(&system_interface); Rml::SetFileInterface(&file_interface); Rml::SetRenderInterface(&recorder);
    if (!Rml::Initialise()) return 0;
    RegisterEffects();
    RegisterDecryptText();
    RegisterToggle();
    RegisterModal();
    RegisterPlayerName();
    RegisterLagGraph();
    initialized = true;
    context = Rml::CreateContext("cod4x", {1280, 800});
    if (!context) { RmlHost_Shutdown(); return 0; }
    auto constructor = context->CreateDataModel("diagnostic");
    constructor.Bind("status", &status); constructor.Bind("activations", &activations);
    constructor.Bind("toggle_demo", &toggle_demo);
    model = constructor.GetModelHandle();
    economy.Init(context);
    // A missing base font never fails startup: LoadFonts retries it for the economy screens and the HUD.
    font_loaded[0] = Rml::LoadFontFace(kBaseFont);
    return 1;
}

extern "C" void RmlHost_Shutdown() {
    if (!initialized) return;
    RmlHost_Hide();
    { std::lock_guard<std::mutex> lock(frames_mutex); frames.clear(); }
    renderer.DeviceLost();
    economy.Reset();
    broadcast.Reset(); broadcast_enabled=broadcast_ready=false;
    avatars::Shutdown();
    Rml::Shutdown();
    context = nullptr; document = nullptr; initialized = false; model = {};
    document_path = "ui/rml/diagnostic.rml"; want_replay = false;
    economy_screen = economy_preview = false;
    fonts_loaded = false; fonts_retry_at = 0;
    for (bool& loaded : font_loaded) loaded = false;
}

extern "C" int RmlHost_Show() {
    if (!context) return 0;
    if (host.elapsed_time) now = std::max(now, host.elapsed_time());
    if (economy_screen) economy.Update(host, economy_preview, now);
    if (!document) {
        document = context->LoadDocument(document_path);
        if (!document) return 0;
        for (const char* type : {"click", "mouseover", "focus", "mousedown", "keydown", "modalclose"}) document->AddEventListener(type, &events, true);
    }
    want_close = false; visible = true; palette_audio.Reset(now);
    document->Show();
    ReplayDecryptText(document);
    // Resolve initial styles/layout before recording text and control geometry.
    context->Update();
    return 1;
}

extern "C" void RmlHost_ClearInput() {
    if (context) {
        context->ProcessMouseLeave();
        for (auto key : pressed_keys) context->ProcessKeyUp(key, 0);
        for (int button = 0; button < 5; ++button) if (pressed_buttons[button]) context->ProcessMouseButtonUp(button, 0);
    }
    pressed_keys.clear(); held_keys.reset(); std::fill(std::begin(pressed_buttons), std::end(pressed_buttons), false);
    palette_audio.Leave();
}
extern "C" void RmlHost_Hide() {
    visible = false; want_close = false;
    RmlHost_ClearInput();
    if (document) document->Hide();
}
extern "C" int RmlHost_Visible() { return visible && !want_close; }

// Reloads the menu document, and the passive HUD when include_hud is set (F9 / rml_reload). Switching screens
// reloads only the menu, so a HUD authoring error can never block the buy menu.
static int ReloadDocuments(bool include_hud) {
    if (!context) return 0;
    const bool was_visible = visible;
    Rml::Factory::ClearStyleSheetCache(); Rml::Factory::ClearTemplateCache();
    Rml::ReleaseTextures(&recorder);
    const bool hud_ok=include_hud ? broadcast.Reload() : true;
    if (!document && !visible) return hud_ok;
    RmlHost_Hide();
    auto* replacement = context->LoadDocument(document_path);
    if (!replacement) {
        if (document && was_visible) { visible = true; document->Show(); }
        return 0;
    }
    if (document) {
        for (const char* type : {"click", "mouseover", "focus", "mousedown", "keydown", "modalclose"}) document->RemoveEventListener(type, &events, true);
        document->Close(); document = nullptr; context->Update();
    }
    document = replacement;
    for (const char* type : {"click", "mouseover", "focus", "mousedown", "keydown", "modalclose"}) document->AddEventListener(type, &events, true);
    const bool menu_ok=was_visible ? RmlHost_Show()!=0 : true;
    return menu_ok && hud_ok;
}
extern "C" int RmlHost_Reload() { return ReloadDocuments(true); }

extern "C" int RmlHost_Open(rml_screen_t screen) {
    if (!context) return 0;
    const char* path;
    if (screen == RML_SCREEN_DIAGNOSTIC) path = "ui/rml/diagnostic.rml";
    else if (screen == RML_SCREEN_PALETTE) path = "ui/rml/palette.rml";
    else if (screen == RML_SCREEN_ECONOMY || screen == RML_SCREEN_ECONOMY_PREVIEW) path = "ui/rml/economy.rml";
    else return 0;
    if ((screen == RML_SCREEN_ECONOMY || screen == RML_SCREEN_ECONOMY_PREVIEW) && !LoadFonts()) return 0;
    const bool previous_economy = economy_screen, previous_preview = economy_preview;
    economy_screen = screen == RML_SCREEN_ECONOMY || screen == RML_SCREEN_ECONOMY_PREVIEW;
    economy_preview = screen == RML_SCREEN_ECONOMY_PREVIEW;
    if (economy_screen) economy.Update(host, economy_preview, now);
    if (std::strcmp(document_path, path)) {
        const char* previous = document_path;
        document_path = path;
        if (!ReloadDocuments(false)) { document_path = previous; economy_screen = previous_economy; economy_preview = previous_preview; return 0; }
    }
    // Entrance animations play on every open, not only the first document load.
    if (!visible) want_replay = true;
    return RmlHost_Show();
}

extern "C" int RmlHost_SetSourceDirectory(const char* directory) { return source.Set(directory); }
extern "C" int RmlHost_SetModSourceDirectory(const char* directory) { return mod_source.Set(directory); }
extern "C" void RmlHost_SetAvatarDirectory(const char* directory) { avatars::SetDirectory(directory); }
extern "C" void RmlHost_EconomyDialog(int confirmation) {
    economy.Dialog(confirmation != 0);
    if (confirmation && context && document && economy_screen) {
        context->Update();
        if (auto* button = document->GetElementById("confirm-demolition")) button->Focus();
    }
}
extern "C" int RmlHost_ActionAllowed(const char* response) { return EconomyActionAllowed(response); }

extern "C" void RmlHost_MouseMove(int x, int y) {
    if (!RmlHost_Visible()) return;
    context->ProcessMouseMove(x, y, 0);
    if (!Control(context->GetHoverElement())) palette_audio.Leave();
}
extern "C" void RmlHost_Text(uint32_t value) {
    if (RmlHost_Visible() && value >= 32 && value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff)) context->ProcessTextInput(Rml::Character(value));
}
extern "C" void RmlHost_FocusLost() { RmlHost_Hide(); }

extern "C" void RmlHost_Key(int key, int down, int modifiers) {
    if (!RmlHost_Visible()) return;
    if (key >= 0 && key < int(held_keys.size())) {
        const bool repeat = down && held_keys[key];
        held_keys[key] = down != 0;
        if (repeat && !RepeatAllowed(key)) return;
    }
    if (economy_screen && down && economy.Shortcut(key, host, economy_preview, now)) return;
    if (key == RML_KEY_ESCAPE && down) {
        if (economy_screen && !economy_preview && host.menu_response) host.menu_response("close");
        else want_close = true;
        return;
    }
    if (key >= RML_KEY_MOUSE1 && key <= RML_KEY_MOUSE5) {
        pressed_buttons[key - RML_KEY_MOUSE1] = down != 0;
        if (down) context->ProcessMouseButtonDown(key - RML_KEY_MOUSE1, modifiers);
        else context->ProcessMouseButtonUp(key - RML_KEY_MOUSE1, modifiers);
    } else if (key == RML_KEY_MWHEELDOWN || key == RML_KEY_MWHEELUP) {
        if (down) context->ProcessMouseWheel(key == RML_KEY_MWHEELUP ? -1.f : 1.f, modifiers);
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
    const auto started = std::chrono::steady_clock::now();
    if (want_close) RmlHost_Hide();
    if ((!visible && !broadcast_enabled) || !context || width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0;
    now = std::max(now, seconds);
    const bool source_changed = source.Changed(now);
    const bool mod_changed = mod_source.Changed(now);
    if (source_changed || mod_changed) {
        if (RmlHost_Reload()) { ++source_reloads; if (host.log) host.log("source reloaded"); }
        else { ++source_failures; if (host.log) host.log("source reload failed; previous document retained"); }
    }
    broadcast_ready=broadcast_enabled && LoadFonts() && broadcast.Prepare(host,width,height,now);
    if (!visible && !broadcast_ready) return 0;
    if (visible && economy_screen) economy.Update(host, economy_preview, now);
    if (visible) {
        const bool resized = economy_screen ? FitStage(context, width, height, 1920.f, 1080.f, 0.25f) :
            FitStage(context, width, height, 1280.f, 800.f, 0.5f);
        if (want_replay && document) { document->SetClass("playing", false); context->Update(); document->SetClass("playing", true); ReplayDecryptText(document); want_replay = false; }
        UpdateStage(context, resized);
        if (economy_screen && economy.ResolveFocus()) context->Update();
    }
    recorder.Begin(width, height);
    if (broadcast_ready) broadcast.Render();
    if (visible) context->Render();
    auto frame = recorder.End();
    std::lock_guard<std::mutex> lock(frames_mutex);
    // A discarded native command queue cannot retain an unbounded number of frames.
    if (frames.size() >= 3) frames.erase(frames.begin());
    uint32_t ticket = next_ticket++;
    if (!ticket) ticket = next_ticket++;
    frames.emplace(ticket, std::move(frame)); ++recorded_frames;
    record_cost.Add(started);
    return ticket;
}
extern "C" void RmlHost_Discard(uint32_t ticket) { std::lock_guard<std::mutex> lock(frames_mutex); frames.erase(ticket); }
extern "C" int32_t RmlHost_Render(void* device, uint32_t ticket) {
    std::shared_ptr<const Frame> frame;
    { std::lock_guard<std::mutex> lock(frames_mutex);
      auto found = frames.find(ticket); if (found == frames.end()) return S_FALSE;
      frame = std::move(found->second); frames.erase(found); }
    const auto started = std::chrono::steady_clock::now();
    const int32_t result = renderer.Render(static_cast<IDirect3DDevice9*>(device), *frame);
    std::lock_guard<std::mutex> lock(frames_mutex); render_cost.Add(started);
    return result;
}
extern "C" void RmlHost_DeviceLost() {
    renderer.DeviceLost();
    std::lock_guard<std::mutex> lock(frames_mutex); frames.clear();
}
extern "C" void RmlHost_Stats(rml_stats_t* out) {
    if (!out) return;
    *out = {}; renderer.Stats(*out);
    out->visible = RmlHost_Visible(); out->activations = activations;
    out->source_reloads = source_reloads; out->source_failures = source_failures;
    std::lock_guard<std::mutex> lock(frames_mutex);
    out->recorded_frames = recorded_frames; out->pending_frames = uint32_t(frames.size());
    out->record_us = record_cost.Take(); out->render_us = render_cost.Take();
}
