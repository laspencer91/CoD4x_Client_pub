#define NOMINMAX
#include "rml/rml_host.h"
#include "rml/native_keys.h"
#include <RmlUi/Core.h>
#include "rml/source.hpp"
#include <windows.h>
#include <d3d9.h>
#include <d3dx9tex.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <vector>
#include <map>

namespace {
bool fixture = true, missing_document = false;
int warnings = 0, hover_sounds = 0, click_sounds = 0;
std::map<std::string,std::string> economy_values;
std::vector<std::string> economy_requests;
const char* EconomyValue(const char* name) { return economy_values[name].c_str(); }
int EconomyResponse(const char* response) { economy_requests.emplace_back(response); return 1; }
class KeyProbe final : public Rml::EventListener {
public:
    int last = 0;
    void ProcessEvent(Rml::Event& event) override { last = event.GetParameter<int>("key_identifier", 0); }
};
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
void Log(const char* message) { if (std::strncmp(message, "Loaded font face", 16)) ++warnings; std::fprintf(stderr, "RmlUi: %s\n", message); }
void Sound(const char* alias) { if (!std::strcmp(alias, "mouse_over")) ++hover_sounds; else if (!std::strcmp(alias, "mouse_click")) ++click_sounds; }

struct Device {
    HWND window = nullptr;
    IDirect3D9* api = nullptr;
    IDirect3DDevice9* device = nullptr;
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* surface = nullptr;
    D3DPRESENT_PARAMETERS params = {};
    int width = 320, height = 240;
    DWORD clear_color = D3DCOLOR_ARGB(255, 0, 0, 255);

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
        Check(device->Clear(0, nullptr, D3DCLEAR_TARGET, clear_color, 1, 0), "clear target");
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
    if (const char* override_root = std::getenv("COD4X_RML_TEST_ROOT")) root = override_root;
    if (argc == 1) {
        std::freopen(RML_TEST_OUTPUT "/rml-desktop.log", "w", stdout);
        std::freopen(RML_TEST_OUTPUT "/rml-desktop-errors.log", "w", stderr);
        std::setvbuf(stdout, nullptr, _IONBF, 0);
    }
    const rml_host_t host = {ReadFile, std::free, Log, Sound, nullptr, EconomyValue, EconomyResponse};
    Require(RmlHost_Init(&host), "initialize actual core and FreeType");
    if (argc > 1 && std::strcmp(argv[1], "--broadcast") == 0) {
        fixture=false;
        const std::string client_source=std::string(root)+"/ui/rml";
        const char* mod=RML_SOURCE_ROOT "/../promod-econ-na/ui/rml";
        if (GetFileAttributesA(mod)==INVALID_FILE_ATTRIBUTES) return 77;
        Require(RmlHost_SetSourceDirectory(client_source.c_str()) && RmlHost_SetModSourceDirectory(mod), "read passive HUD sources");
        // Engine values (CL_Rml_Value) plus Promod's shoutcaster slots; nothing HUD-specific comes from the server.
        economy_values={{"rml_local_team","spectator"},{"rml_score_allies","12"},{"rml_score_axis","3"},
            {"rml_seconds_left","82"},{"ui_bomb_timer","0"},{"ui_rml_round","16"},{"cg_rmlCards","0"},{"rml_follow_client","2"}};
        const char* names[]={"^1Hi^7as","^5Cooper","^2Skubal","^3Korkunen","Giombi","Merboth","^1Biessman","Bumsky","^5Adamson","Guest"};
        for (int i=0;i<10;++i) {
            const auto slot=std::to_string(i);
            economy_values["shout_client"+slot]=slot; economy_values["shout_name"+slot]=names[i];
            economy_values["shout_health"+slot]=(i==0 || i==5 || i==7 || i==9) ? "0" : i==2 ? "0.75" : "1";
            economy_values["shout_weapon"+slot]=i%2 ? "ak47" : "m4";
            economy_values["shout_utility"+slot]=i%2 ? "weapon_flashbang" : "weapon_smokegrenade"; economy_values["shout_frag"+slot]="1";
        }
        RmlHost_EnableBroadcast(1);
        Device gpu; gpu.clear_color=D3DCOLOR_ARGB(255,38,38,40); gpu.Target(1920,1080);
        gpu.Render(1.0); gpu.Render(1.3);
        Require(RmlHost_BroadcastReady() && !RmlHost_Visible(), "passive HUD renders without a captured menu");
        auto* doc=Rml::GetContext("cod4x-broadcast")->GetDocument(0);
        Require(doc->QuerySelector(".match-clock")->GetInnerRML()=="1:22" && doc->QuerySelector(".match-clock")->GetBox().GetSize().y>0,
            "HUD reads and lays out the authoritative round clock");
        Rml::ElementList cards; doc->QuerySelectorAll(cards,".player-card");
        cards.erase(std::remove_if(cards.begin(),cards.end(),[](Rml::Element* element) { return !element->IsVisible(); }),cards.end());
        Require(cards.size()==10 && cards[0]->IsClassSet("dead") && cards[2]->IsClassSet("observed"), "shared cards reflect dead and observed states");
        auto* name=cards[0]->QuerySelector("player-name");
        Require(name->GetNumChildren()==2 && name->GetChild(0)->GetInnerRML()=="Hi", "player name preserves game color runs");
        name->SetAttribute("text", "^1<img src='x' />^7Z^"); gpu.Render(1.4);
        Require(name->QuerySelector("img")==nullptr && name->GetNumChildren()==2, "player names stay text and preserve a trailing caret");
        name->SetAttribute("text", names[0]); gpu.Render(1.5);
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-broadcast-expanded.png",D3DXIFF_PNG,gpu.surface,nullptr,nullptr),"capture expanded match HUD");
        economy_values["rml_follow_client"]="1"; gpu.Render(1.6);
        Require(cards[1]->IsClassSet("observed") && !cards[2]->IsClassSet("observed"), "camera highlight follows native viewed-client changes without server roster updates");
        economy_values["shout_name0"]="Replacement"; economy_values["shout_health0"]="1";
        gpu.Render(1.7);

        Require(cards[0]->IsClassSet("fresh") && cards[0]->GetComputedValues().opacity()==1.f && cards[0]->GetBox().GetSize().y>=63,
            "a replacement identity resets the previous occupant's death presentation immediately");
        gpu.Render(1.75); economy_values["shout_health0"]="0"; gpu.Render(1.8); gpu.Render(1.9);
        Require(cards[0]->GetBox().GetSize().y>34 && cards[0]->GetBox().GetSize().y<66, "death smoothly collapses the existing identity's card");
        economy_values["cg_rmlCards"]="1"; gpu.Render(2.0); gpu.Render(2.1);
        Require(cards[1]->GetBox().GetSize().y>34 && cards[1]->GetBox().GetSize().y<66, "compact transition has an intermediate animated state");
        gpu.Render(2.4); gpu.Render(2.5);
        Require(cards[1]->GetBox().GetSize().y<40, "compact mode settles to short cards");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-broadcast-compact.png",D3DXIFF_PNG,gpu.surface,nullptr,nullptr),"capture compact match HUD");
        economy_values["cg_rmlCards"]="2";
        for (int i=0;i<9;++i) gpu.Render(3.0+i*.05);

        Require(cards[1]->GetComputedValues().opacity()<0.01f && doc->QuerySelector(".match-score")->IsVisible(), "hidden cards settle to zero opacity while the score bar stays visible");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-broadcast-hidden.png",D3DXIFF_PNG,gpu.surface,nullptr,nullptr),"capture hidden cards with score bar retained");
        Require(RmlHost_Reload(), "reload passive HUD without opening a menu"); gpu.Render(3.5);
        doc=Rml::GetContext("cod4x-broadcast")->GetDocument(0);
        Require(!RmlHost_Visible(), "passive reload never acquires menu input");
        const auto hud_pixels=gpu.Pixels();
        int allies_ink=0;
        for (int y=21;y<66;++y) for (int x=795;x<855;++x) allies_ink+=Near(hud_pixels[y*1920+x],138,184,201);
        Require(allies_ink>20, "top score bar remains rendered after card hiding and reload");
        for (const auto size : {Rml::Vector2i(1024,768),Rml::Vector2i(3440,1440)}) {
            gpu.Target(size.x,size.y); gpu.Render(3.6);
            auto* bar=doc->QuerySelector(".match-score");
            const auto offset=bar->GetAbsoluteOffset(Rml::BoxArea::Border), dimensions=bar->GetBox().GetSize(Rml::BoxArea::Border);
            const float density=Rml::GetContext("cod4x-broadcast")->GetDensityIndependentPixelRatio();
            Require(offset.x>=0 && offset.x+dimensions.x<=size.x+1 && offset.y<30*density, "match bar stays at the viewport top on 4:3 and ultrawide");
            Rml::ElementList live_cards; doc->QuerySelectorAll(live_cards,".player-card");
            auto* last=live_cards[4]; const float edge=last->GetAbsoluteOffset(Rml::BoxArea::Border).y+last->GetBox().GetSize(Rml::BoxArea::Border).y;
            Require(edge<=size.y+1 && edge>size.y-60*density, "team cards stay anchored to the viewport bottom");
        }
        economy_values["rml_team_color_allies"]="#99a3b0"; economy_values["rml_team_color_axis"]="#a69169"; gpu.Render(3.9); gpu.Render(3.95);
        {
            const auto faction=doc->QuerySelector(".team-score.left")->GetComputedValues().color();
            Require(faction.red==188 && faction.green==195 && faction.blue==203, "observer scores use the scoreboard's faction colours");
        }
        economy_values["rml_local_team"]="axis"; gpu.Render(4.0);
        {
            gpu.Target(1920,1080); gpu.Render(4.05);
            Rml::ElementList scores; doc->QuerySelectorAll(scores,".score-value");
            Require(doc->QuerySelector(".match-score")->IsClassSet("relative") && scores.size()==2 &&
                scores[0]->GetInnerRML()=="3" && scores[1]->GetInnerRML()=="12", "players see their own team's score on the left");
            Require(!doc->QuerySelector(".alive-count") || !doc->QuerySelector(".alive-count")->IsVisible(), "player bar omits alive counts");
            Require(doc->QuerySelector(".round-strip > div")->GetInnerRML()=="ROUND 16", "the round number comes from server info");
            economy_values["rml_seconds_left"]=""; gpu.Render(4.06);
            Require(doc->QuerySelector(".match-clock")->GetInnerRML().empty(), "no engine end time shows no clock");
            economy_values["rml_seconds_left"]="82";
            Require(doc->QuerySelector(".armor-hud")->IsClassSet("depleted"), "armor HUD stays hidden without armor");
            economy_values["economy_armor_capacity"]="2"; economy_values["economy_armor_charges"]="2"; gpu.Render(4.1); gpu.Render(4.5);
            Rml::ElementList pips; doc->QuerySelectorAll(pips,".armor-pip");
            Require(!doc->QuerySelector(".armor-hud")->IsClassSet("depleted") && pips.size()==2 && !pips[0]->IsClassSet("spent") && !pips[1]->IsClassSet("spent"),
                "armor HUD shows one pip per charge");
            economy_values["economy_armor_charges"]="1"; gpu.Render(4.6); gpu.Render(4.7);
            auto* consumed=pips[1]->QuerySelector(".armor-fill");
            Require(pips[1]->IsClassSet("spent") && consumed->GetComputedValues().opacity()>0.f && consumed->GetComputedValues().opacity()<1.f,
                "a consumed charge animates out");
            gpu.Render(5.2); gpu.Render(5.3);            Require(consumed->GetComputedValues().opacity()<0.01f && !pips[0]->IsClassSet("spent"), "the consumed pip settles empty beside the remaining charge");
            Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-broadcast-armor.png",D3DXIFF_PNG,gpu.surface,nullptr,nullptr),"capture armor HUD");
            Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-broadcast-player.png",D3DXIFF_PNG,gpu.surface,nullptr,nullptr),"capture player score bar");
        }
        Require(!doc->QuerySelector(".roster")->IsVisible(), "ordinary player mode has no private observer roster");
        const auto requests=economy_requests.size(); RmlHost_Key('1',1,0); RmlHost_Key('1',0,0);
        Require(economy_requests.size()==requests && !RmlHost_Visible(), "passive HUD never routes gameplay keys to menu actions");
        Require(warnings==0,"broadcast has no stylesheet or data diagnostics");
        RmlHost_EnableBroadcast(0); Require(!RmlHost_Record(1920,1080,5.0),"disabled HUD stops recording");
        RmlHost_Shutdown(); std::puts("RML_BROADCAST_CHECKS_PASSED"); return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--economy") == 0) {
        fixture = false;
        const char* mod = std::getenv("COD4X_RML_TEST_MOD_SOURCE");
        if (!mod) mod = RML_SOURCE_ROOT "/../promod-econ-na/ui/rml";
        if (GetFileAttributesA(mod) == INVALID_FILE_ATTRIBUTES) { std::puts("SKIP: sibling economy assets unavailable"); return 77; }
        const std::string client_source = std::string(root) + "/ui/rml";
        Require(RmlHost_SetSourceDirectory(client_source.c_str()) && RmlHost_SetModSourceDirectory(mod), "read shared and gameplay UI from their own authoring roots");
        Require(RmlHost_Open(RML_SCREEN_ECONOMY_PREVIEW), "open actual redesigned economy document");
        Device gpu; gpu.clear_color = D3DCOLOR_ARGB(255, 38, 38, 40);
        gpu.Target(1280, 800); gpu.Render(10.0);
        for (int frame = 1; frame <= 10; ++frame) gpu.Render(10.0 + frame * 0.05);
        auto* preview_doc = Rml::GetContext("cod4x")->GetDocument(0);
        auto* deploy = preview_doc->GetElementById("deploy");
        Require(deploy && deploy->GetAbsoluteOffset().y + deploy->GetBox().GetSize(Rml::BoxArea::Border).y <= 800,
            "deploy stays visible with the full loadout at 1280x800");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-economy.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture economy design");
        for (const auto size : {Rml::Vector2i(1920,1080), Rml::Vector2i(1024,768), Rml::Vector2i(3440,1440)}) {
            gpu.Target(size.x, size.y); gpu.Render(10.6);
            auto* stage = preview_doc->GetElementById("economy-stage");
            const auto offset = stage->GetAbsoluteOffset(); const auto dimensions = stage->GetBox().GetSize();
            Require(offset.x >= -1 && offset.y >= -1 && offset.x + dimensions.x <= size.x + 1 && offset.y + dimensions.y <= size.y + 1,
                "entire fixed-layout canvas fits the target viewport");
            Require(std::abs(dimensions.x / dimensions.y - 1920.f / 1080.f) < 0.005f, "menu scale preserves the authored aspect ratio");
            const std::string capture = std::string(RML_TEST_OUTPUT) + "/rml-economy-" + std::to_string(size.x) + ".png";
            Check(D3DXSaveSurfaceToFileA(capture.c_str(), D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture scaled economy layout");
        }
        gpu.Target(1280,800); gpu.Render(10.7);
        auto click_button = [&](Rml::Element* button) {
            const auto point = button->GetAbsoluteOffset(Rml::BoxArea::Border);
            RmlHost_MouseMove(int(point.x+4), int(point.y+4)); RmlHost_Key(200,1,0); RmlHost_Key(200,0,0);
        };
        auto action_button = [&](const char* action) -> Rml::Element* {
            Rml::ElementList inputs; preview_doc->GetElementsByTagName(inputs, "input");
            for (auto* input : inputs) if (input->GetAttribute<Rml::String>("data-econ", "") == action) return input;
            Require(false, "preview action exists"); return nullptr;
        };
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(10.701);
        RmlHost_Key(RML_KEY_DOWNARROW,1,0); RmlHost_Key(RML_KEY_DOWNARROW,0,0);
        RmlHost_Key(RML_KEY_ENTER,1,0); RmlHost_Key(RML_KEY_ENTER,0,0); gpu.Render(10.702);
        Require(preview_doc->QuerySelector(".primary-art")->GetAttribute<Rml::String>("src", "")=="images/m4.tga", "preview weapon selection changes the displayed loadout");
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(10.703);
        auto* preview_drawer = preview_doc->GetElementById("primary-drawer");
        auto panel_point = preview_drawer->QuerySelector(".picker-panel")->GetAbsoluteOffset();
        RmlHost_MouseMove(int(panel_point.x+20),int(panel_point.y+20)); RmlHost_Key(200,1,0); RmlHost_Key(200,0,0); gpu.Render(10.704);
        Require(preview_drawer->IsVisible(), "clicking modal panel content does not dismiss it");
        RmlHost_MouseMove(1200,500); RmlHost_Key(200,1,0); RmlHost_Key(200,0,0); gpu.Render(10.705);
        Require(!preview_drawer->IsVisible(), "clicking the backdrop dismisses the picker");
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(10.706); preview_drawer->RemoveAttribute("dismiss-on-backdrop");
        RmlHost_MouseMove(1200,500); RmlHost_Key(200,1,0); RmlHost_Key(200,0,0); gpu.Render(10.707);
        Require(preview_drawer->IsVisible(), "generic modal can opt out of backdrop dismissal");
        preview_drawer->SetAttribute("dismiss-on-backdrop", "true"); RmlHost_Key(RML_KEY_ESCAPE,1,0); RmlHost_Key(RML_KEY_ESCAPE,0,0); gpu.Render(10.708);
        click_button(action_button("attachment:silencer")); gpu.Render(10.71);
        click_button(preview_doc->GetElementById("buy-frag")); gpu.Render(10.72);
        click_button(preview_doc->GetElementById("buy-armor")); gpu.Render(10.73);
        Require(economy_requests.empty(), "preview shopping never sends live transactions");
        Require(preview_doc->GetElementById("armor-charge-one")->IsClassSet("filled") &&
            preview_doc->GetElementById("armor-charge-two")->IsClassSet("filled"), "preview armor purchase updates both charge segments");
        click_button(action_button("repeat_buy")); gpu.Render(10.74);
        Require(preview_doc->GetElementById("repeat-toggle")->IsClassSet("checked"), "preview toggle updates its simulated rebuy setting");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-preview-cart.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture interactive preview purchases and saved rebuy rows");
        Rml::ElementList moves; preview_doc->QuerySelectorAll(moves, ".rebuy-arrow");
        Require(moves.size()==6 && moves[0]->HasAttribute("disabled") && moves[5]->HasAttribute("disabled"), "rebuy exposes right-aligned arrows with boundary controls disabled");
        click_button(moves[4]); gpu.Render(10.8);
        moves.clear(); preview_doc->QuerySelectorAll(moves, ".rebuy-arrow");
        Require(moves[2]->GetAttribute<Rml::String>("data-value", "")=="armor", "preview up arrow actually moves armor earlier");
        click_button(moves[3]); gpu.Render(10.9);
        const auto armor_point = preview_doc->GetElementById("buy-armor")->GetAbsoluteOffset(Rml::BoxArea::Border);
        RmlHost_MouseMove(int(armor_point.x+4), int(armor_point.y+4));
        RmlHost_Key(RML_KEY_MOUSE2,1,0); RmlHost_Key(RML_KEY_MOUSE2,0,0); gpu.Render(10.91);
        Require(!preview_doc->GetElementById("armor-charge-one")->IsClassSet("filled") && economy_requests.empty(),
            "right-click refunds owned armor even when the purchase control is disabled");
        RmlHost_Key('2',1,0); RmlHost_Key('2',0,0); gpu.Render(10.95);
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(10.96);
        Require(preview_doc->GetElementById("primary-drawer")->IsVisible(), "class changes keep the shared weapon picker usable");
        RmlHost_Key(RML_KEY_ESCAPE,1,0); RmlHost_Key(RML_KEY_ESCAPE,0,0); gpu.Render(10.97);
        Require(RmlHost_ActionAllowed("buy_armor") && RmlHost_ActionAllowed("give:player_123_4") &&
            !RmlHost_ActionAllowed("buy_armor;quit") && !RmlHost_ActionAllowed("pick_primary:8") &&
            !RmlHost_ActionAllowed("give:player_1_2\""), "economy response allowlist rejects command syntax and invalid rows");
        economy_values = {{"economy_cash","100"},{"economy_buy_open","1"},{"economy_purchase_allowed","1"},{"flat_has_class","1"},
            {"economy_repeat_order","attachment,frag,armor"},
            {"economy_repeat_attachment","silencer"},{"economy_repeat_frag","1"},{"economy_repeat_armor","2"},
            {"loadout_class","assault"},{"loadout_primary","ak47"},{"loadout_secondary","deserteagle"},{"loadout_primary_attachment","none"},
            {"flat_primary_rows","1"},{"flat_primary_0_ref","ak47"},{"economy_armor_price","100"},{"economy_can_buy_armor","1"},
            {"economy_armor_capacity","2"},{"flat_buy_seconds","24"},{"economy_frag_price","100"},{"economy_can_buy_frag","1"}};
        Require(RmlHost_Open(RML_SCREEN_ECONOMY), "switch preview to live server-bound state"); gpu.Render(11.0);
        auto* doc=Rml::GetContext("cod4x")->GetDocument(0);
        auto* armor=doc->GetElementById("buy-armor");
        Require(armor && !armor->HasAttribute("disabled"), "server-eligible purchase is enabled");
        auto point=armor->GetAbsoluteOffset(); RmlHost_MouseMove(int(point.x+35),int(point.y+25));
        RmlHost_Key(200,1,0); RmlHost_Key(200,0,0);
        Require(economy_requests.size()==1 && economy_requests.back()=="buy_armor", "native click submits existing armor response without local debit");
        Require(economy_values["economy_cash"]=="100", "client never changes authoritative cash");
        economy_values["economy_buy_open"]="0"; economy_values["economy_purchase_allowed"]="0"; economy_values["economy_can_buy_armor"]="0";
        economy_values["economy_can_buy_frag"]="0";
        gpu.Render(11.3); Require(armor->HasAttribute("disabled"), "buy expiry disables the purchase surface");
        RmlHost_Key(200,1,0); RmlHost_Key(200,0,0); Require(economy_requests.size()==1, "disabled purchase submits no request");
        Require(RmlHost_Reload(), "live economy reload keeps authoritative state"); gpu.Render(11.6);
        Require(Rml::GetContext("cod4x")->GetDocument(0)->GetElementById("buy-armor")->HasAttribute("disabled"), "closed-buy state survives hot reload");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-economy-closed.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture closed economy state");
        auto* live_doc = Rml::GetContext("cod4x")->GetDocument(0);
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(11.7);
        Require(live_doc->GetElementById("primary-drawer")->IsVisible(), "Q opens the primary picker without spending cash");
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(11.8);
        Require(!live_doc->GetElementById("primary-drawer")->IsVisible(), "Q closes its own picker");
        RmlHost_Key('z',1,0); RmlHost_Key('z',0,0); RmlHost_Key('x',1,0); RmlHost_Key('x',0,0);
        Require(economy_requests.size()==1, "equipment shortcuts cannot buy after phase expiry");
        auto* irons = live_doc->GetElementById("primary-none"); const auto irons_point = irons->GetAbsoluteOffset();
        RmlHost_MouseMove(int(irons_point.x+10),int(irons_point.y+10)); RmlHost_Key(200,1,0); RmlHost_Key(200,0,0); gpu.Render(11.9);
        Require(economy_requests.back()=="attachment:none" && !live_doc->GetElementById("primary-drawer")->IsVisible(),
            "attachment control submits its own action without opening the card picker");
        RmlHost_Key(RML_KEY_SPACE,1,0); RmlHost_Key(RML_KEY_SPACE,0,0);
        Require(economy_requests.back()=="deploy", "Space uses the existing deploy response");
        Require(RmlHost_Open(RML_SCREEN_PALETTE), "switch from letterboxed economy to palette"); gpu.Render(12.0);
        Require(Rml::GetContext("cod4x")->GetDensityIndependentPixelRatio() == 1.f, "palette restores its own authoring density");
        Require(RmlHost_Open(RML_SCREEN_ECONOMY), "switch back to the live economy"); gpu.Render(13.0);
        auto* restored_stage = Rml::GetContext("cod4x")->GetDocument(0)->GetElementById("economy-stage");
        Require(std::abs(restored_stage->GetBox().GetSize().x - 1280.f) < 1, "economy restores its scale after switching screens");
        auto* restored_doc = Rml::GetContext("cod4x")->GetDocument(0);
        moves.clear(); restored_doc->QuerySelectorAll(moves, ".rebuy-arrow"); click_button(moves[4]); gpu.Render(13.1);
        Require(economy_requests.back()=="rebuy_up:armor" && economy_values["economy_cash"]=="100", "live arrow sends only an allowlisted priority change without debiting cash");
        economy_values["economy_repeat_order"]="attachment,armor,frag"; gpu.Render(13.2);
        moves.clear(); restored_doc->QuerySelectorAll(moves, ".rebuy-arrow");
        Require(moves[2]->GetAttribute<Rml::String>("data-value", "")=="armor", "live priority display follows the server-published order");
        auto* live_toggle = restored_doc->GetElementById("repeat-toggle");
        click_button(live_toggle->QuerySelector("input")); gpu.Render(13.21);
        Require(economy_requests.back()=="repeat_buy" && !live_toggle->IsClassSet("checked"), "controlled live toggle waits for server confirmation");
        economy_values["economy_repeat_enabled"]="1"; gpu.Render(13.22);
        Require(live_toggle->IsClassSet("checked"), "controlled toggle reflects the confirmed server value");
        Require(!RmlHost_ActionAllowed("rebuy_up:invalid") && !RmlHost_ActionAllowed("rebuy_down:armor;quit"), "rebuy allowlist rejects unknown items and command syntax");
        economy_values["flat_primary_rows"]="3"; economy_values["flat_primary_1_ref"]="m4"; economy_values["flat_primary_2_ref"]="m16"; gpu.Render(13.3);
        RmlHost_Key('q',1,0); RmlHost_Key('q',0,0); gpu.Render(13.4);
        Require(Rml::GetContext("cod4x")->GetFocusElement()->GetAttribute<Rml::String>("data-value", "")=="0", "picker opens focused on the equipped weapon");
        RmlHost_Key(RML_KEY_DOWNARROW,1,0); RmlHost_Key(RML_KEY_DOWNARROW,0,0); gpu.Render(13.45);
        Require(Rml::GetContext("cod4x")->GetFocusElement()->GetAttribute<Rml::String>("data-value", "")=="1", "Down moves picker focus to the next weapon");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-strikepoint-picker.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture indexed picker with actual artwork and focused row");
        RmlHost_Key(RML_KEY_ENTER,1,0); RmlHost_Key(RML_KEY_ENTER,0,0); gpu.Render(13.5);
        Require(economy_requests.back()=="pick_primary:1" && !restored_doc->GetElementById("primary-drawer")->IsVisible(), "Enter equips the focused picker entry and closes the drawer");
        economy_values["flat_loadout_mode"]="standard";
        economy_values["weap_allow_frag_grenade"]="1";
        economy_values["flat_camo_label"]="Woodland"; economy_values["flat_camo_material"]="ui_camoskin_bshdwl";
        economy_values["flat_utility_name"]="SMOKE"; economy_values["flat_team"]="ATTACK"; economy_values["mapname"]="mp_crash";
        economy_values["flat_mate_0_exists"]="1"; economy_values["flat_mate_0_name"]="MAVERICK";
        economy_values["flat_mate_0_class"]="Spec Ops"; economy_values["flat_mate_0_primary"]="mp5";
        economy_values["flat_mate_0_utility"]="weapon_flashbang"; economy_values["flat_mate_0_frag"]="1";
        economy_values["flat_demolitions_allowed"]="1";
        economy_values["flat_demolitions_capacity"]="0/1";
        economy_values["economy_suppressor_primary_supported"]="1";
        economy_values["attach_allow_assault_reflex"]="1";
        gpu.Render(14.0);
        Require(!restored_doc->QuerySelector(".top-status")->IsVisible() &&
            !restored_doc->QuerySelector(".armor-card")->IsVisible() &&
            !restored_doc->QuerySelector(".rebuy-panel")->IsVisible(), "standard mode hides wallet, buy phase, armor and rebuy");
        Require(restored_doc->QuerySelector(".standard-frag")->IsVisible(), "standard mode retains the included frag card");
        Require(restored_doc->GetElementById("deploy")->GetAttribute<Rml::String>("value", "")=="CONFIRM", "standard mode uses a class confirmation action");
        const auto requests_before = economy_requests.size();
        RmlHost_Key('z',1,0); RmlHost_Key('z',0,0); RmlHost_Key('x',1,0); RmlHost_Key('x',0,0); gpu.Render(14.1);
        Require(economy_requests.size()==requests_before, "standard mode never submits paid equipment shortcuts");
        RmlHost_Key('4',1,0); RmlHost_Key('4',0,0); gpu.Render(14.2);
        Require(economy_requests.back()=="select:demolitions", "standard Demolition uses normal class selection");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-standard-loadout.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture shared standard class menu");
        economy_values["flat_has_class"]="0"; gpu.Render(15.0);
        Require(restored_doc->QuerySelector(".unselected-loadout")->IsVisible() &&
            !restored_doc->QuerySelector(".primary-card")->IsVisible(), "initial class choice shows a prompt instead of empty weapon cards");
        Require(warnings==0, "economy parses without model or stylesheet diagnostics");
        RmlHost_Shutdown(); RmlHost_SetSourceDirectory(nullptr); RmlHost_SetModSourceDirectory(nullptr);
        std::puts("RML_ECONOMY_CHECKS_PASSED"); return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--decrypt") == 0) {
        fixture = false;
        Require(RmlHost_Open(RML_SCREEN_PALETTE), "open palette with registered decrypt component");
        Device gpu; gpu.clear_color = D3DCOLOR_ARGB(255, 38, 38, 40); gpu.Target(1280, 800);
        auto* palette = Rml::GetContext("cod4x")->GetDocument(0);
        auto* decrypt = palette->GetElementById("decrypt-probe");
        Require(decrypt != nullptr, "actual decrypt-text element exists");
        auto output = [&]() { return static_cast<Rml::ElementText*>(decrypt->QuerySelector(".decrypt-output")->GetChild(0))->GetText(); };
        auto alphanumeric = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
        decrypt->SetAttribute("text", "AB7"); decrypt->SetAttribute("cycles", 2); gpu.Render(100.0);
        const float width = decrypt->GetBox().GetSize().x;
        const auto first = output();
        Require(first.size() == 1 && alphanumeric(first[0]) && first[0] != 'A', "first cycle contains only a non-final letter or digit");
        gpu.Render(100.018); const auto second = output();
        Require(second.size() == 1 && alphanumeric(second[0]) && second != first && second != "A", "second cycle is different and still alphanumeric");
        gpu.Render(100.036); Require(output() == "A", "two-cycle character locks to its correct value");
        gpu.Render(100.054); Require(output().size() == 2 && output()[0] == 'A' && alphanumeric(output()[1]), "locked prefix stays correct as the next character scrambles");
        gpu.Render(100.162); Require(output() == "AB7" && !decrypt->IsClassSet("decrypting"), "the number and letters settle exactly");
        Require(decrypt->GetBox().GetSize().x == width, "reserved final text keeps component width stable");
        decrypt->SetAttribute("cycles", 3); decrypt->SetAttribute("replay", 1); gpu.Render(101.0);
        const auto third_first = output(); gpu.Render(101.018); const auto third_second = output(); gpu.Render(101.036);
        Require(output().size() == 1 && alphanumeric(output()[0]) && output() != "A" && output() != third_first && output() != third_second,
            "three-cycle mode presents three distinct scramble characters");
        gpu.Render(101.054); Require(output() == "A", "three-cycle character locks on the following tick");
        const std::string literal = u8"GO / 7: \u00e9 <A&7>";
        decrypt->SetAttribute("text", literal); gpu.Render(102.0); gpu.Render(104.0);
        Require(output() == literal, "text changes preserve spaces, punctuation, Unicode and literal markup");
        decrypt->SetAttribute("instant", true); decrypt->SetAttribute("text", "INSTANT 42"); gpu.Render(105.0);
        Require(output() == "INSTANT 42", "instant mode shows final text without scrambling");
        decrypt->RemoveAttribute("instant"); decrypt->SetAttribute("text", ""); gpu.Render(106.0);
        Require(output().empty(), "empty text clears without stale output");
        decrypt->SetAttribute("text", "SECURE THE SITE"); gpu.Render(107.0);
        palette->GetElementById("decrypt-effects")->ScrollIntoView(); gpu.Render(107.001);
        auto click = [&](const char* id) {
            auto point = palette->GetElementById(id)->GetAbsoluteOffset(Rml::BoxArea::Border);
            RmlHost_MouseMove(int(point.x + 20), int(point.y + 15)); RmlHost_Key(200, 1, 0); RmlHost_Key(200, 0, 0);
        };
        click("decrypt-speed"); gpu.Render(108.0);
        Require(decrypt->GetAttribute<int>("tick-ms", 0) == 45, "slow preview control updates the component speed");
        click("decrypt-speed"); gpu.Render(109.0);
        Require(decrypt->GetAttribute<int>("tick-ms", 0) == 18, "speed control restores the rapid default");
        gpu.Render(110.0); click("replay-decrypt"); gpu.Render(111.0);
        Require(output().size() == 1, "mouse replay restarts a settled component");
        gpu.Render(111.26);
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-decrypt-active.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture active decrypt component");
        gpu.Render(113.0);
        Require(output() == "SECURE THE SITE" && economy_requests.empty(), "decrypt settles without submitting gameplay actions");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-decrypt-settled.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture settled decrypt component");
        Require(RmlHost_Reload(), "reload a document containing the custom component"); gpu.Render(114.0);
        Require(Rml::GetContext("cod4x")->GetDocument(0)->GetElementById("decrypt-probe") != nullptr, "custom component survives document replacement");
        Require(warnings == 0, "decrypt component loads without model or stylesheet warnings");
        RmlHost_Shutdown(); return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--palette") == 0) {
        fixture = false;
        Require(RmlHost_Open(RML_SCREEN_PALETTE), "open palette as the first screen");
        Device gpu; gpu.Target(1280, 800); gpu.Render(50.0); gpu.Render(50.4);
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-palette-first.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "save first-screen palette capture");
        auto count_text = [&](const std::vector<DWORD>& pixels) {
            auto* control = Rml::GetContext("cod4x")->GetDocument(0)->GetElementById("palette-select");
            Require(control != nullptr, "palette control is present");
            const auto origin = control->GetAbsoluteOffset(Rml::BoxArea::Border);
            int count = 0;
            for (int y = std::max(0,int(origin.y)+8); y < std::min(800,int(origin.y)+35); ++y) for (int x = std::max(0,int(origin.x)+75); x < std::min(1280,int(origin.x)+175); ++x) {
                const DWORD pixel = pixels[y * 1280 + x];
                if (((pixel >> 16) & 255) > 150 && ((pixel >> 8) & 255) > 150 && (pixel & 255) > 150) ++count;
            }
            return count;
        };
        const int first_ink = count_text(gpu.Pixels());
        Require(first_ink > 80, "complete control text on first open, before any reload");
        Require(RmlHost_Reload(), "reload palette without losing its screen selection");
        gpu.Render(51.0); gpu.Render(51.4);
        Require(count_text(gpu.Pixels()) == first_ink, "text is identical before and after reload");
        gpu.Target(1024, 768); gpu.Render(52.0);
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-palette-4x3.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture 4:3 palette");
        gpu.Target(3440, 1440); gpu.Render(53.0);
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-palette-ultrawide.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture ultrawide palette");
        gpu.Target(1280, 800); gpu.Render(54.0);
        auto* palette = Rml::GetContext("cod4x")->GetDocument(0);
        auto* effects = palette->GetElementById("motion-effects");
        auto* reveal = palette->GetElementById("typewriter-probe");
        auto* replay = palette->GetElementById("replay-effects");
        auto* frame_content = palette->GetElementById("frame-content-probe");
        auto* stagger = palette->GetElementById("stagger-probe");
        auto* power = palette->GetElementById("power-probe");
        Require(effects && reveal && replay && frame_content && stagger && power, "briefing motion samples and local replay are present");
        effects->ScrollIntoView(); gpu.Render(54.1);
        const auto replay_origin = replay->GetAbsoluteOffset(Rml::BoxArea::Border);
        RmlHost_MouseMove(int(replay_origin.x + 20), int(replay_origin.y + 15));
        RmlHost_Key(200, 1, 0); RmlHost_Key(200, 0, 0);
        gpu.Render(54.2);
        const float start_width = reveal->GetBox().GetSize().x;
        Require(frame_content->GetComputedValues().opacity() < 0.01f && stagger->GetComputedValues().opacity() < 0.01f &&
            power->GetComputedValues().opacity() < 0.01f, "frame, rows and power-on restart from their entrance states");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-motion-start.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture motion start");
        for (int frame = 1; frame <= 7; ++frame) gpu.Render(54.2 + frame * 0.05);
        const float middle_width = reveal->GetBox().GetSize().x;
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-motion-middle.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture motion mid reveal");
        for (int frame = 8; frame <= 20; ++frame) gpu.Render(54.2 + frame * 0.05);
        const float settled_width = reveal->GetBox().GetSize().x;
        Require(start_width < 5 && middle_width > start_width + 30 && settled_width > middle_width + 30,
            "replay restarts the clipped text reveal and progresses to full width");
        Require(frame_content->GetComputedValues().opacity() > 0.99f && stagger->GetComputedValues().opacity() > 0.99f &&
            power->GetComputedValues().opacity() > 0.99f, "all briefing entrances settle to fully readable content");
        Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-motion-settled.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "capture settled briefing effects");
        gpu.Render(56.2);
        Require(reveal->GetBox().GetSize().x == settled_width, "motion settles without looping or collapsing text");
        Require(RmlHost_Open(RML_SCREEN_DIAGNOSTIC), "switch back to diagnostic through typed screen API");
        Require(warnings == 0, "palette loads without warnings");
        RmlHost_Shutdown(); return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--source") == 0) {
        const std::string directory = RML_TEST_OUTPUT "/live-source";
        CreateDirectoryA(directory.c_str(), nullptr);
        auto write = [&](const char* filename, const char* contents) {
            FILE* output = std::fopen((directory + "/" + filename).c_str(), "wb");
            Require(output != nullptr, "open isolated authoring fixture");
            std::fputs(contents, output); std::fclose(output);
        };
        write("diagnostic.rml", "<rml><head><link type=\"text/rcss\" href=\"live.rcss\" /></head><body><div id=\"probe\" /></body></rml>");
        write("live.rcss", "#probe { display: block; width: 20px; height: 30px; background-color: #f00; }");
        Require(RmlHost_SetSourceDirectory(directory.c_str()), "select loose authoring source");
        Require(RmlHost_Show(), "load document directly from authoring source");
        auto width = [&]() { return Rml::GetContext("cod4x")->GetDocument(0)->GetElementById("probe")->GetOffsetWidth(); };
        double source_time = 1.0;
        auto advance_until = [&](auto ready) {
            for (int i = 0; i < 150 && !ready(); ++i) {
                Sleep(10); source_time += 0.01;
                RmlHost_Record(1280, 800, source_time);
            }
            return ready();
        };
        RmlHost_Record(1280, 800, source_time); Require(width() == 20, "initial source style applied");
        write("live.rcss", "#probe { display: block; width: 83px; height: 30px; background-color: #0f0; }");
        Require(width() == 20, "save waits for quiet interval");
        Require(advance_until([&]() { return width() == 83; }), "style save changes live layout without restart");
        write("diagnostic.rml", "<rml><head><link type=\"text/rcss\" href=\"live.rcss\" /></head><body><div id=\"probe\" style=\"width: 109px;\" /></body></rml>");
        Require(advance_until([&]() { return width() == 109; }), "component save changes live layout without restart");
        DeleteFileA((directory + "/diagnostic.rml").c_str());
        Require(advance_until([&]() { rml_stats_t current; RmlHost_Stats(&current); return current.source_failures == 1; }), "missing source is reported");
        Require(RmlHost_Visible() && width() == 109, "failed save retains working document");
        write("diagnostic.rml", "<rml><body><div id=\"probe\" style=\"display: block; width: 143px;\" /></body></rml>");
        Require(advance_until([&]() { return width() == 143; }), "source watching recovers after missing document");
        rml_stats_t state; RmlHost_Stats(&state);
        Require(state.source_reloads == 3 && state.source_failures == 1, "reload status reports successful and failed saves");
        cod4x::rml::Source files;
        Require(files.Set(directory.c_str()), "source reader initialized");
        std::vector<unsigned char> bytes;
        Require(!files.Read("ui/rml/../../outside", bytes) && !files.Read("other/file", bytes), "source namespace rejects traversal");
        RmlHost_Shutdown(); RmlHost_SetSourceDirectory(nullptr);
        std::puts("RML_SOURCE_CHECKS_PASSED"); return 0;
    }
    if (argc > 1 && std::strcmp(argv[1], "--cpu") == 0) {
        fixture = false;
        Require(RmlHost_Show(), "load real diagnostic with packaged styles");
        auto ticket = RmlHost_Record(1280, 800, 1.0); Require(ticket != 0, "layout and record real document without a device"); RmlHost_Discard(ticket);
        RmlHost_MouseMove(960, 465); RmlHost_Record(1280, 800, 1.1);
        RmlHost_MouseMove(970, 466); Require(hover_sounds == 1, "one hover cue within a control");
        RmlHost_MouseMove(700, 700); RmlHost_Record(1280, 800, 1.2); RmlHost_MouseMove(960, 465);
        Require(hover_sounds == 2, "reentering a control plays its next hover cue");
        RmlHost_Key(RML_KEY_MOUSE1, 1, 0); RmlHost_Key(RML_KEY_MOUSE1, 0, 0);
        rml_stats_t state; RmlHost_Stats(&state); Require(state.activations == 1 && click_sounds == 1, "bound local action and selection cue");
        RmlHost_Key(RML_KEY_ENTER, 1, 0); RmlHost_Key(RML_KEY_ENTER, 0, 0);
        RmlHost_Stats(&state); Require(state.activations == 2 && click_sounds == 2, "Enter activates the focused native control");
        KeyProbe keys;
        auto* diagnostic = Rml::GetContext("cod4x")->GetDocument(0);
        diagnostic->AddEventListener("keydown", &keys, true);
        RmlHost_Key(154, 1, 0); RmlHost_Key(154, 0, 0);
        Require(keys.last == Rml::Input::KI_UP, "literal native CoD4 arrow reaches RmlUi");
        RmlHost_Key(159, 1, 1); RmlHost_Key(159, 0, 0);
        Require(keys.last == Rml::Input::KI_LCONTROL, "literal native CoD4 Ctrl reaches RmlUi");
        RmlHost_Key(160, 1, 2); RmlHost_Key(160, 0, 0);
        Require(keys.last == Rml::Input::KI_LSHIFT, "literal native CoD4 Shift reaches RmlUi");
        RmlHost_Key(158, 1, 4); RmlHost_Key(158, 0, 0);
        Require(keys.last == Rml::Input::KI_LMENU, "literal native CoD4 Alt reaches RmlUi");
        RmlHost_Key(184, 1, 0); RmlHost_Key(184, 0, 0);
        Require(keys.last == Rml::Input::KI_PRIOR, "literal native CoD4 keypad Page Up reaches RmlUi");
        diagnostic->RemoveEventListener("keydown", &keys, true);
        RmlHost_MouseMove(960, 510); RmlHost_Key(RML_KEY_MOUSE1, 1, 0); RmlHost_Key(RML_KEY_MOUSE1, 0, 0);
        RmlHost_Stats(&state); Require(state.activations == 2, "disabled native control rejects activation");
        RmlHost_MouseMove(960, 465);
        RmlHost_Key(RML_KEY_MOUSE1, 1, 0); RmlHost_FocusLost(); Require(!RmlHost_Visible(), "hide while pointer is held");
        Require(RmlHost_Show(), "reopen after capture loss"); RmlHost_Key(RML_KEY_MOUSE1, 0, 0);
        RmlHost_Stats(&state); Require(state.activations == 2, "release after focus loss cannot activate a stale control");
        RmlHost_MouseMove(960, 465); RmlHost_Key(RML_KEY_MOUSE1, 1, 0);
        RmlHost_ClearInput(); Require(RmlHost_Visible(), "input release preserves a visible adopted menu");
        RmlHost_Key(RML_KEY_MOUSE1, 0, 0); RmlHost_Stats(&state);
        Require(state.activations == 2, "input release cancels the held click without activating it");
        RmlHost_Key(RML_KEY_ESCAPE, 1, 0); Require(!RmlHost_Record(1280, 800, 1.3), "Escape stops recording");
        Require(RmlHost_Show(), "reopen after Escape");
        for (int i = 0; i < 12; ++i) RmlHost_Record(1280, 800, 2.0 + i * .01);
        RmlHost_Stats(&state); Require(state.pending_frames == 3, "bounded discarded-queue ownership");
        RmlHost_DeviceLost(); RmlHost_Stats(&state); Require(!state.pending_frames, "device loss invalidates pending frame tickets");
        Require(RmlHost_Reload(), "source reload retains bound model"); RmlHost_Stats(&state); Require(state.activations == 2, "model survives reload");
        missing_document = true; Require(!RmlHost_Reload() && RmlHost_Visible(), "failed reload retains working screen");
        missing_document = false;
        Require(RmlHost_Open(RML_SCREEN_PALETTE), "load CoD4 palette with shared effects");
        Require(!RmlHost_Open(static_cast<rml_screen_t>(99)), "screen selection rejects unknown identifiers");
        RmlHost_Record(1280, 800, 3.0);
        RmlHost_Record(1024, 768, 3.3);
        RmlHost_Record(3440, 1440, 3.6);
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
    RmlHost_Key(RML_KEY_MOUSE1, 1, 0); RmlHost_Key(RML_KEY_MOUSE1, 0, 0); gpu.Render(3.8);
    RmlHost_Stats(&after_stats); Require(after_stats.activations == 1 && click_sounds == 1, "real document input updates binding and local selection cue");
    RmlHost_Key(RML_KEY_ESCAPE, 1, 0); Require(!RmlHost_Visible() && !RmlHost_Record(1280, 800, 4.0), "Escape releases modal visibility");
    Require(RmlHost_Show(), "reopen diagnostic"); RmlHost_FocusLost(); Require(!RmlHost_Visible(), "focus loss hides modal surface");
    Require(RmlHost_Show(), "reopen before failed reload"); missing_document = true;
    Require(!RmlHost_Reload() && RmlHost_Visible(), "missing document reload retains working modal surface");
    missing_document = false;
    Require(RmlHost_Open(RML_SCREEN_PALETTE), "open native CoD4 palette");
    gpu.Render(5.0); gpu.Render(5.4);
    Check(D3DXSaveSurfaceToFileA(RML_TEST_OUTPUT "/rml-palette.png", D3DXIFF_PNG, gpu.surface, nullptr, nullptr), "save native palette capture");
    Require(warnings <= 2, "diagnostic styles produce no unexpected RmlUi warnings");
    RmlHost_Shutdown(); Require(RmlHost_Init(&host), "runtime can initialize again after shutdown"); RmlHost_Shutdown();
    std::printf("RML_NATIVE_CHECKS_PASSED\n");
    return 0;
}
