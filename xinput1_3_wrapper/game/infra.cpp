#include "stdafx.h"
#include <base.h>
#include <strsafe.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <cctype>
#include "overlay.h"
#include "SimpleIni.h"
#include "infra.h"

#include "counters.h"
#include "functional_camera.h"
#include "inventory.h"

using namespace infra::structs;
using namespace infra::functions;

// Not static because other kids use it...
std::ofstream g_LogWriter = std::ofstream();

// ---- Verbose logging ([log] verbose) ----
// In verbose mode every line is timestamped and flushed immediately, so the tail
// of silta.log points at the crash site instead of dying in the stream buffer.
bool g_LogVerbose = false;
static std::mutex g_LogMx; // photo workers log from background threads
void LogLine(const char* level, const std::string& msg) {
	std::lock_guard<std::mutex> lk(g_LogMx);
	SYSTEMTIME st; GetLocalTime(&st);
	char head[48];
	sprintf_s(head, sizeof(head), "[%02d:%02d:%02d.%03d] [%s] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level);
	g_LogWriter << head << msg << "\n";
	if (g_LogVerbose) g_LogWriter.flush();
}
void LogV(const std::string& msg) { if (g_LogVerbose) LogLine("dbg", msg); }
void LogI(const std::string& msg) { LogLine("inf", msg); }
void LogE(const std::string& msg) { LogLine("ERR", msg); g_LogWriter.flush(); }

// Last-chance crash marker: notes the crash address/module in the log before the
// process dies, so "it crashed on Back" becomes "access violation at overlay.cpp
// code, address X". Registered once at startup.
static LONG WINAPI SiltaCrashFilter(EXCEPTION_POINTERS* ep) {
	char buf[160];
	const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
	const void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
	HMODULE mod = nullptr;
	char modName[MAX_PATH] = "?";
	if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		static_cast<LPCSTR>(addr), &mod) && mod) {
		GetModuleFileNameA(mod, modName, sizeof(modName));
	}
	sprintf_s(buf, sizeof(buf), "CRASH exception 0x%08X at %p (module: %s)", code, addr, modName);
	LogE(buf);
	g_LogWriter.flush();
	return EXCEPTION_CONTINUE_SEARCH; // let the game's own handler run too
}

// Engine interface factory captured in mod.cpp on plugin load.
extern void* (*g_EngineFactory)(const char* name, int* return_code);

// ----- [tweaks] console-command runner state (parsed in load_config) -----
static bool g_TweaksEnabled = false;
static std::vector<std::string> g_TweakCommands;
static std::string g_TweakEngineInterface; // "" = auto-try a list of versions
static int g_TweakClientCmdIndex = 7;      // IVEngineClient::ClientCmd vtable slot

#define HEX(X) std::hex << (X) << std::dec

BOOL CALLBACK EnumWindowsCallback(HWND handle, LPARAM lParam);
HWND GetProcessWindow();
bool GetD3D9Device(void** pTable, size_t Size);

/* Hooked functions begin */
typedef void(__thiscall* InitMapStats_t)(void*);
InitMapStats_t InitMapStats_orig;
void __fastcall InitMapStats(void* this_ptr);

typedef int(__thiscall* StatSuccess_t)(void* this_ptr, int event_type, int count, bool is_new);
StatSuccess_t StatSuccess_orig;
int __fastcall StatSuccess(void* this_ptr, int, int event_type, int count, bool is_new);

// I think this is actually CInfraCamera::OnCommand, but good enough.
// This gets called when we take a picture.
typedef int(__thiscall* CInfraCameraFreezeFrame__OnCommand_t)(void* thiz, void* lpKeyValues);
CInfraCameraFreezeFrame__OnCommand_t CInfraCameraFreezeFrame__OnCommand_orig;
int __fastcall CInfraCameraFreezeFrame__OnCommand(CInfraCameraFreezeFrame* thiz, int, void* lpKeyValues);


// DirectX EndScene()
HRESULT __stdcall EndScene(LPDIRECT3DDEVICE9 pDevice);
HRESULT (__stdcall *EndScene_orig)(LPDIRECT3DDEVICE9 pDevice);

// DirectX Reset() - hooked so the overlay releases its D3DPOOL_DEFAULT objects
// before the game resets a lost device (exclusive-fullscreen alt-tab). Without
// this, Reset fails while ImGui's buffers are alive and the game can never
// restore the device - the classic "can't alt-tab back in fullscreen" hang.
HRESULT __stdcall DeviceReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
HRESULT (__stdcall *DeviceReset_orig)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
/* Hooked functions end */

// Handles to functions in the engine
static GetPlayerByIndex_t GetPlayerByIndex;

// Various state variables
static bool g_SuccessCountersEnabled = true;
static bool g_FunctionalCameraEnabled = true;
static bool g_InventoryOverlayEnabled = true;
static bool g_ReportEnabled = true;
static float g_ReportSeconds = 12.0f;
static std::vector<std::string> g_ReportTokens;   // map-name substrings that trigger the report
static std::vector<std::string> g_InvHideTokens;  // map-name substrings where the inventory hides
static std::string g_ReportLastMap;               // guard: generate once per ending-map load
static infra::InfraEngine* g_Engine;

static void* GetModuleAddress(const char* name) {
	void* addr;

	while (true) {
		addr = static_cast<void*>(GetModuleHandleA(name));
		if (addr) {
			break;
		}

		Sleep(500);
	}

	return addr;
}

namespace infra {
	InfraEngine::InfraEngine() {
		this->engine_base = GetModuleAddress("engine.dll");
		this->client_base = GetModuleAddress("client.dll");
		this->server_base = GetModuleAddress("server.dll");
		this->materialsystem_base = GetModuleAddress("MaterialSystem.dll");
		this->vguimatsurface_base = GetModuleAddress("vguimatsurface.dll");

		this->pGlobalEntityAddEntity = reinterpret_cast<GlobalEntity_AddEntity_t>(this->get_server_ptr(0x158A10));
		this->pGlobalEntitySetCounter = reinterpret_cast<GlobalEntity_SetCounter_t>(this->get_server_ptr(0x158420));
		this->pGlobalEntityGetCounter = reinterpret_cast<GlobalEntity_GetCounter_t>(this->get_server_ptr(0x1585C0));
		this->pGlobalEntityAddToCounter = reinterpret_cast<GlobalEntity_AddToCounter_t>(this->get_server_ptr(0x158450));
		this->pGlobalEntityGetState = reinterpret_cast<GlobalEntity_GetState_t>(this->get_server_ptr(0x158590));
		this->pGlobalEntitySetState = reinterpret_cast<GlobalEntity_SetState_t>(this->get_server_ptr(0x1583F0));

		this->pKeyValuesGetInt = reinterpret_cast<KeyValues__GetInt_t>(this->get_client_ptr(0x3A0840));
		// For some reason, this yields an "Access violation executing location" error - maybe this was true one day, but now it isn't?
		this->pFindEntityByName = reinterpret_cast<CGlobalEntityList__FindEntityByName_t>(this->get_server_ptr(0x10ED60));
	}

	InfraEngine::~InfraEngine() {
		for (void* const &hook : this->enabledHooks) {
			MH_DisableHook(hook);
			MH_RemoveHook(hook);
		}

		this->enabledHooks.clear();
	}

	// TODO: Maybe make the hooker print out an error message if the hook fails.
#define HOOKER(BASE) \
	template <typename T> \
	void InfraEngine::hook_##BASE(const int32_t offset, LPVOID pDetour, T** pOriginal) { \
		void* pTarget = PTR_ADD(this->BASE##_base, offset); \
		if (MH_CreateHook(pTarget, pDetour, reinterpret_cast<void **>(pOriginal)) == MH_OK) { \
			if (MH_EnableHook(pTarget) == MH_OK) { \
				this->enabledHooks.push_back(pTarget); \
			} \
		} \
	}
#define GETTER(BASE) \
	void* InfraEngine::get_##BASE##_ptr(const int32_t offset) const { \
		return PTR_ADD(this->BASE##_base, offset); \
	}

	HOOKER(client)
	HOOKER(server)

	GETTER(client)
	GETTER(server)
	GETTER(engine)
	GETTER(vguimatsurface)
	GETTER(materialsystem)

#undef HOOKER
#undef GETTER

	bool InfraEngine::is_in_main_menu() {
		return this->engine_base
			&& *(static_cast<bool*>(PTR_ADD(this->engine_base, 0x63FD86)));
	}

	bool InfraEngine::loading_screen_visible() {
		return this->engine_base
			&& *(static_cast<bool*>(PTR_ADD(this->engine_base, 0x5F9269)));
	}

	const char* InfraEngine::get_map_name() {
		if (!this->server_base) {
			return nullptr;
		}

		// 		uint32_t ptr = *(uint32_t*)((uint32_t)this->server_base + 0x78BD00) + 0x3c;

		void* ptr = *(static_cast<void**>(PTR_ADD(this->server_base, 0x78BD00)));

		return *(static_cast<char**>(PTR_ADD(ptr, 0x3C)));
	}

	int InfraEngine::GlobalEntity_AddEntity(const char* pGlobalname, const char* pMapName, const GLOBALESTATE state) const {
		return this->pGlobalEntityAddEntity(pGlobalname, pMapName, state);
	}

	void InfraEngine::GlobalEntity_SetCounter(const int globalIndex, const int counter) const {
		this->pGlobalEntitySetCounter(globalIndex, counter);
	}

	int InfraEngine::GlobalEntity_GetCounter(const int globalIndex) const {
		return this->pGlobalEntityGetCounter(globalIndex);
	}

	int InfraEngine::GlobalEntity_AddToCounter(const int globalIndex, const int count) const {
		return this->pGlobalEntityAddToCounter(globalIndex, count);
	}

	int InfraEngine::GlobalEntity_GetState(const int globalIndex) const {
		return this->pGlobalEntityGetState(globalIndex);
	}

	void InfraEngine::GlobalEntity_SetState(const int globalIndex, const GLOBALESTATE state) const {
		return this->pGlobalEntitySetState(globalIndex, state);
	}

	CMatSystemTexture* InfraEngine::MaterialSystem_GetTextureById(const int id) const {
		void* textureDictionary = this->get_vguimatsurface_ptr(0x1432D0);
		void* textureList = *((void**)PTR_ADD(textureDictionary, 4));

		return static_cast<CMatSystemTexture*>(PTR_ADD(textureList, id << 6));
	}

	CVGui *InfraEngine::VGui() const {
		// This is the g_Vgui in VGuiMaterialSurface, it was the best way I could find to get a ptr to it.
		return *static_cast<CVGui**>(PTR_ADD(this->vguimatsurface_base, 0x14E1DC));
	}

	CHud *InfraEngine::Hud() const {
		return static_cast<CHud*>(PTR_ADD(this->client_base, 0x6BB028));
	}

	int InfraEngine::KeyValues__GetInt(void* lpKeyValues, const char* name, const int defaultValue) const {
		return this->pKeyValuesGetInt(lpKeyValues, name, defaultValue);
	}

	CGlobalEntityList* InfraEngine::server_entity_list() const {
		return static_cast<CGlobalEntityList *>(this->get_server_ptr(0x725178));
	}

	CBaseEntity* InfraEngine::CGlobalEntityList__FindEntityByName(CBaseEntity* pStartEntity, const char* szName,
	                                                 CBaseEntity* pSearchingEntity, CBaseEntity* pActivator,
	                                                 CBaseEntity* pCaller, void* pFilter) const {
		return this->pFindEntityByName(this->server_entity_list(), pStartEntity, szName, pSearchingEntity, pActivator, pCaller, pFilter);
	}


	InfraEngine* Engine() {
		return g_Engine;
	}
}


// Parse a hex colour string ("RRGGBB" or "RRGGBBAA", optional leading '#') into
// an ImVec4. Returns `fallback` if the string is malformed.
static ImVec4 ParseHexColor(const std::string& in, const ImVec4& fallback) {
	std::string s = in;
	const size_t a = s.find_first_not_of(" \t");
	const size_t b = s.find_last_not_of(" \t");
	if (a == std::string::npos) {
		return fallback;
	}
	s = s.substr(a, b - a + 1);
	if (!s.empty() && s[0] == '#') {
		s = s.substr(1);
	}
	if (s.size() != 6 && s.size() != 8) {
		return fallback;
	}

	char* end = nullptr;
	const unsigned long v = strtoul(s.c_str(), &end, 16);
	if (end == s.c_str() || *end != '\0') {
		return fallback;
	}

	if (s.size() == 8) {
		return ImVec4(
			((v >> 24) & 0xFF) / 255.0f,
			((v >> 16) & 0xFF) / 255.0f,
			((v >> 8) & 0xFF) / 255.0f,
			(v & 0xFF) / 255.0f);
	}
	return ImVec4(
		((v >> 16) & 0xFF) / 255.0f,
		((v >> 8) & 0xFF) / 255.0f,
		(v & 0xFF) / 255.0f,
		1.0f);
}

static overlay::Corner ParseCorner(const std::string& in, overlay::Corner fallback) {
	std::string t;
	for (char c : in) {
		if (c != ' ' && c != '-' && c != '_' && c != '\t') {
			t += static_cast<char>(tolower(static_cast<unsigned char>(c)));
		}
	}
	if (t == "topleft" || t == "tl") return overlay::Corner::TopLeft;
	if (t == "topright" || t == "tr") return overlay::Corner::TopRight;
	if (t == "bottomleft" || t == "bl") return overlay::Corner::BottomLeft;
	if (t == "bottomright" || t == "br") return overlay::Corner::BottomRight;
	return fallback;
}

static std::string ToLower(std::string s) {
	for (char& c : s) {
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return s;
}

// ----- [tweaks] console-command runner -----
// NOTE: this reaches into the Source engine's IVEngineClient to run console
// commands. The interface version string and the ClientCmd vtable index vary by
// engine build and could NOT be verified against INFRA here, so the feature is
// OFF by default and both values are configurable in the ini. If the interface
// can't be resolved it logs and does nothing (no crash). Calling the wrong
// vtable index, however, can crash — only enable after confirming the index.
static void* ResolveEngineClient() {
	static void* cached = nullptr;
	static bool tried = false;
	if (tried) {
		return cached;
	}
	tried = true;

	if (g_EngineFactory == nullptr) {
		g_LogWriter << "tweaks: no engine factory captured" << std::endl;
		return nullptr;
	}

	std::vector<std::string> versions;
	if (!g_TweakEngineInterface.empty()) {
		versions.push_back(g_TweakEngineInterface);
	} else {
		versions = { "VEngineClient015", "VEngineClient014", "VEngineClient013", "VEngineClient012" };
	}

	for (const std::string& v : versions) {
		int ret = 0;
		void* p = g_EngineFactory(v.c_str(), &ret);
		if (p != nullptr) {
			g_LogWriter << "tweaks: resolved engine interface " << v << std::endl;
			cached = p;
			return cached;
		}
	}

	g_LogWriter << "tweaks: could not resolve IVEngineClient" << std::endl;
	return nullptr;
}

static void EngineClientCmd(void* engineClient, int vtableIndex, const char* cmd) {
	void** vtable = *reinterpret_cast<void***>(engineClient);
	using ClientCmdFn = void(__thiscall*)(void*, const char*);
	ClientCmdFn fn = reinterpret_cast<ClientCmdFn>(vtable[vtableIndex]);
	fn(engineClient, cmd);
}

// Run all configured console commands. Called on each map load so cvars that the
// engine resets between maps get re-applied.
// Write the calculator's live custom-skin colors (and skin=custom) back into
// silta.ini, preserving the rest of the file. Called from the Style tab.
static std::string HexOfColor(const ImVec4& c) {
	char b[8];
	sprintf_s(b, sizeof(b), "%02X%02X%02X",
		static_cast<int>(c.x * 255.0f + 0.5f),
		static_cast<int>(c.y * 255.0f + 0.5f),
		static_cast<int>(c.z * 255.0f + 0.5f));
	return b;
}
void overlay::SaveGaugePos() {
	CSimpleIniA ini;
	if (ini.LoadFile("silta.ini") != SI_OK) return;
	ini.SetLongValue("inventory", "gauge_x", static_cast<long>(overlay::gaugeX));
	ini.SetLongValue("inventory", "gauge_y", static_cast<long>(overlay::gaugeY));
	ini.SaveFile("silta.ini");
	LogI("gauge: position saved to silta.ini");
}

void overlay::SaveCalcCustomColors() {
	CSimpleIniA ini;
	if (ini.LoadFile("silta.ini") != SI_OK) return;
	static const char* kCkeys[7] = { "custom_window", "custom_button", "custom_button_hover",
		"custom_button_active", "custom_accent", "custom_display", "custom_display_bg" };
	for (int i = 0; i < 7; ++i) {
		ini.SetValue("calculator", kCkeys[i], HexOfColor(overlay::calcCustom[i]).c_str());
	}
	ini.SetValue("calculator", "skin", "custom");
	ini.SaveFile("silta.ini");
	LogI("calculator: custom skin saved to silta.ini");
}

static void apply_tweaks() {
	if (!g_TweaksEnabled || g_TweakCommands.empty()) {
		return;
	}
	void* ec = ResolveEngineClient();
	if (ec == nullptr) {
		return;
	}
	for (const std::string& cmd : g_TweakCommands) {
		EngineClientCmd(ec, g_TweakClientCmdIndex, cmd.c_str());
		g_LogWriter << "tweaks: ran '" << cmd << "'" << std::endl;
	}
	g_LogWriter.flush();
}

static void load_config() {
	CSimpleIniA config;

	// If there's no config yet, write a fully-documented default file.
	if (config.LoadFile("silta.ini") != SI_OK) {
		config.SetBoolValue("features", "success_counters", true,
			"; ===== Core feature toggles =====\n"
			"; These three are the original merged modules. Every OTHER feature\n"
			"; (notes, sketchbook, calculator, study outlook, contact sheet, report,\n"
			"; gauge, watermark, log...) has its own 'enabled' key in its own section\n"
			"; below.\n"
			"; Bottom-right success-counters overlay.");
		config.SetBoolValue("features", "functional_camera", true,
			"; Save photos taken with the in-game camera to the DCIM folder.");
		config.SetBoolValue("features", "inventory_overlay", true,
			"; Flashlight/camera battery + OS-coin overlay.");

		config.SetLongValue("overlay", "font_size", 0,
			"; ===== Overlay style =====\n; 0 = auto-size by resolution, otherwise a pixel font size.");
		config.SetValue("overlay", "text_color", "FFFFFF",
			"; Hex RRGGBB. Normal counter text.");
		config.SetValue("overlay", "complete_color", "00FF00",
			"; Hex RRGGBB. Counter text once a category is fully complete.");
		config.SetValue("overlay", "title_color", "666666",
			"; Hex RRGGBB. Map-name title and inventory labels.");
		config.SetDoubleValue("overlay", "background_alpha", 0.30,
			"; Window background opacity: 0.0 (clear) .. 1.0 (opaque).");
		config.SetLongValue("overlay", "margin", 10,
			"; Gap in px between the overlays and the screen edges.");
		config.SetValue("overlay", "counters_corner", "bottom-right",
			"; Anchor corner: top-left | top-right | bottom-left | bottom-right");
		config.SetValue("overlay", "inventory_corner", "top-left",
			"; Anchor corner: top-left | top-right | bottom-left | bottom-right");
		config.SetValue("overlay", "theme", "native",
			"; native = squared/bordered HUD style; debug = plain ImGui look.");
		config.SetBoolValue("overlay", "hotkey_tips", true,
			"; Show a hotkey tip bar at the top when the cursor is out (phone/menu).");
		config.SetBoolValue("overlay", "tip_fade", false,
			"; Fade the hotkey tip bar out after tip_fade_seconds (off = stays while cursor is out).");
		config.SetDoubleValue("overlay", "tip_fade_seconds", 8.0,
			"; Seconds the tip bar stays before fading (when tip_fade = true).");
		config.SetBoolValue("overlay", "focus_fade_counters", false,
			"; ===== Focus fade (immersion) =====\n"
			"; When on, the counters overlay rests nearly transparent and 'wakes up'\n"
			"; to full visibility for a few seconds whenever a tally changes (a\n"
			"; successful photo, a repair...) or the mouse hovers it, then sinks back.");
		config.SetBoolValue("overlay", "focus_fade_inventory", false,
			"; Same for the inventory overlay (wakes on battery/coin pickups).");
		config.SetLongValue("overlay", "focus_transparency", 90,
			"; Idle transparency percent while resting (90 = faint ghost, 0 = opaque).");
		config.SetDoubleValue("overlay", "focus_seconds", 2.5,
			"; How long the overlay stays fully visible after an event.");
		config.SetDoubleValue("overlay", "focus_ramp", 0.35,
			"; Fade in/out time (seconds) between resting and visible.");
		config.SetBoolValue("overlay", "save_layout", true,
			"; Remember dragged overlay/notes/sketch window positions across game restarts.");
		config.SetValue("overlay", "palette", "default",
			"; Colour palette preset: default | osmo (amber) | ncg (navy) | corruption (red).");
		config.SetBoolValue("overlay", "location_names", true,
			"; Title the counters with the real in-world location (e.g. 'Bergmann Tunnels').");

		config.SetLongValue("camera", "photo_width", 660,
			"; ===== Camera photos =====\n"
			"; Saved photo resolution. Default 660x480 = the IMAGE-IN Crystal-shot's\n"
			"; in-lore 480p. Presets you might want:\n"
			";   660  x 480   Crystal-shot 480p (lore-accurate, default)\n"
			";   1280 x 720   720p\n"
			";   1920 x 1080  1080p\n"
			";   0    x 0     native capture (full engine resolution)\n"
			"; photo_height 0 with a width set = height follows the game aspect.");
		config.SetLongValue("camera", "photo_height", 480);
		config.SetValue("camera", "aspect", "crop",
			"; When the target aspect differs from the capture: crop (a real camera\n"
			"; crops its sensor - centered window, default) or stretch (distorts).");
		config.SetValue("camera", "exif_make", "IMAGE-IN",
			"; EXIF camera identity written into every JPEG (the in-lore camera is the\n"
			"; IMAGE-IN Crystal-shot). Change to anything you like.");
		config.SetValue("camera", "exif_model", "Crystal-shot");
		config.SetValue("camera", "exif_software", "",
			"; EXIF Software tag. Empty = 'SILTA v<version>' automatically.");
		config.SetValue("camera", "format", "jpg",
			"; jpg (smaller files) or png (lossless).");
		config.SetValue("camera", "upscale_filter", "linear",
			"; Scaling filter when resizing: linear (smoother) or point (sharp).");
		config.SetValue("camera", "naming", "camera",
			"; Photo file names: camera (DSC00001.jpg, digital-camera style)\n; or timestamp (<map>_<datetime>, the original scheme).");
		config.SetBoolValue("camera", "save_toast", true,
			"; Show a brief toast when a photo is saved.");
		config.SetDoubleValue("camera", "toast_seconds", 0.5,
			"; How long the save toast stays, in seconds (0 = disable the toast).");
		config.SetValue("camera", "toast_label", "saved to microSD\\DCIM\\",
			"; Cosmetic text shown before the photo name in the save toast.");
		config.SetBoolValue("camera", "burn_in", true,
			"; Stamp a visible N.C.G. survey caption (location, date, surveyor) onto the photo.");
		config.SetBoolValue("camera", "exif", true,
			"; Embed EXIF metadata (camera make/model, date, location) into JPEG photos.");
		config.SetValue("camera", "exif_datetime", "2016:08:08 12:00:00",
			"; EXIF capture timestamp, format YYYY:MM:DD HH:MM:SS. The date is also the\n"
			"; base date used when exif_datetime_auto is on.");
		config.SetBoolValue("camera", "exif_datetime_auto", true,
			"; Approximate the photo's time-of-day from the map's chapter (INFRA runs\n"
			"; across one day, ~08:00 in c1 to ~23:00 in c10). The epilogue is canonically\n"
			"; a different day; set this false to use a fixed exif_datetime instead.");
		config.SetValue("camera", "calibrate", "",
			"; *** DEBUG ONLY *** - normally NOT needed (the origin offset is built in).\n"
			"; If a game patch ever moves it: console (developer 1), getpos, paste the\n"
			"; three numbers after 'setpos' here (trailing ;setang is harmless), press\n"
			"; F6 and stand still ~1 s - SILTA locks the new offset (toast + log).");
		config.SetValue("camera", "burn_in_accent_color", "FFEB96",
			"; Caption top-line colour (hex RRGGBB).");
		config.SetValue("camera", "burn_in_text_color", "E6E6E6",
			"; Caption bottom-line colour (hex RRGGBB).");
		config.SetDoubleValue("camera", "burn_in_band", 0.35,
			"; Caption band darkness: 0 = solid black strip, 1 = no strip (text only).");
		config.SetBoolValue("camera", "subfolders", true,
			"; Sort photos into per-site subfolders, e.g. DCIM\\HammerValleyReservoir\\.");
		config.SetBoolValue("camera", "survey_log", true,
			"; Append every photo (file, site, time, coords) to DCIM\\survey_log.txt.");
		config.SetBoolValue("camera", "asset_tag", true,
			"; Include an N.C.G. asset tag (site + filename) in the EXIF description.");
		config.SetLongValue("camera", "player_origin_offset", 0,
			"; *** DEBUG ONLY *** Player-origin source for EXIF coordinates. Leave at 0\n"
			"; = built-in (offset 0x2C,\n"
			"; found via SILTA calibrate - works on the current game build). Set a\n"
			"; positive byte offset to override it if a future patch moves the struct\n"
			"; (re-run calibrate to find it), or -1 to disable coordinates entirely.");
		config.SetBoolValue("camera", "exif_gps", true,
			"; Map coordinates to EXIF GPS tags. Cosmetic mapping: the world origin is\n"
			"; anchored where Stolland canonically sits - the open Baltic Sea between\n"
			"; Sweden (NE) and Denmark (S), per the Stalburg wiki - so photo viewers\n"
			"; drop your survey pins on Stolland's waters.");
		config.SetDoubleValue("camera", "gps_lat0", 57.0, "; GPS latitude at world origin (default: Stolland, Baltic Sea).");
		config.SetDoubleValue("camera", "gps_lon0", 19.0, "; GPS longitude at world origin (default: Stolland, Baltic Sea).");
		config.SetDoubleValue("camera", "gps_units_per_degree", 100000.0,
			"; World units per degree of latitude/longitude (for exif_gps).");

		config.SetValue("counters", "defects_color", "E0E0E0",
			"; ===== Per-category counter rows =====\n; *_color: hex RRGGBB for that row (rows still turn complete_color when done).\n; *_show: set false to hide that row entirely.");
		config.SetBoolValue("counters", "defects_show", true);
		config.SetValue("counters", "corruption_color", "FF6E6E");
		config.SetBoolValue("counters", "corruption_show", true);
		config.SetValue("counters", "repairs_color", "6ED1FF");
		config.SetBoolValue("counters", "repairs_show", true);
		config.SetValue("counters", "geocaches_color", "FFD96E");
		config.SetBoolValue("counters", "geocaches_show", true);
		config.SetValue("counters", "flowmeters_color", "B58CFF");
		config.SetBoolValue("counters", "flowmeters_show", true);
		config.SetValue("counters", "complete_style", "color",
			"; How a finished category is shown: color (flip to complete_color),\n; check (append a checkmark), or dim (fade the row).");
		config.SetBoolValue("counters", "show_total", true,
			"; Show an overall completion line (Total: cur / max (pct%)).");
		config.SetBoolValue("counters", "canon_labels", false,
			"; Use the game's own wording for the rows (Photo spots / Corruption docs / etc.).");
		config.SetBoolValue("counters", "show_progress", true,
			"; Show a brief progress popup (per-category %, next achievement) when the cursor is out.");
		config.SetDoubleValue("counters", "progress_seconds", 5.0,
			"; Seconds the progress popup stays before fading.");

		config.SetValue("inventory", "flashlight_color", "FFE08A",
			"; ===== Battery overlay colours (hex RRGGBB) =====");
		config.SetValue("inventory", "camera_color", "8AD0FF");
		config.SetValue("inventory", "oscoins_color", "FFD96E");
		config.SetBoolValue("inventory", "battery_icons", true,
			"; Draw a small battery glyph next to the flashlight and camera lines.");
		config.SetBoolValue("inventory", "coin_icon", true,
			"; Draw a coin glyph next to OS Coins (appears once you have at least 1).");
		config.SetBoolValue("inventory", "flashlight_gauge", true,
			"; Digital battery gauge near the bottom of the screen. Without a charge\n"
			"; offset (below) it can only show the battery COUNT, appearing briefly\n"
			"; when it changes. With the offset set it tracks the real charge live.");
		config.SetLongValue("inventory", "flashlight_charge_offset", 0,
			"; *** DEBUG ONLY *** LIVE flashlight charge source. Leave at 0 = built-in\n"
			"; (offset 0x184C, percent of\n"
			"; the current battery, found via SILTA autoscan - works on the current\n"
			"; game build). Set a positive byte offset to override it if a future\n"
			"; game patch moves the struct (re-run charge_autoscan to find it), or\n"
			"; -1 to disable the live gauge (falls back to battery count).");
		config.SetLongValue("inventory", "flashlight_gauge_max", 0,
			"; Full-gauge value. 0 = auto (1000 with charge offset, 10 for count).");
		config.SetBoolValue("inventory", "charge_autoscan", false,
			"; *** DEBUG ONLY *** - normally NOT needed (the charge offset is built in).\n"
			"; If a game patch ever moves it: turn this on, flashlight ON ~15 s, and\n"
			"; SILTA finds the new offset (toast + silta.log); put it in\n"
			"; flashlight_charge_offset and turn this back off.");
		config.SetDoubleValue("inventory", "flashlight_gauge_seconds", 3.0,
			"; How long the gauge lingers after the last charge drop. The flashlight\n"
			"; drains 1 unit every ~2 s, so values under ~2.5 make the gauge blink\n"
			"; between ticks instead of staying up.");
		config.SetDoubleValue("inventory", "flashlight_gauge_fade", 0.4,
			"; Smooth fade-out duration (seconds) at the end of the linger.");
		config.SetBoolValue("inventory", "flashlight_gauge_numbers", false,
			"; Show the % text (and the spare-battery count) on the gauge.");
		config.SetValue("inventory", "flashlight_gauge_skin", "subtle",
			"; Gauge skin: default (instrument panel), subtle (slim transparent bar),\n"
			"; or custom (panel layout with the colors below).");
		config.SetLongValue("inventory", "gauge_x", -1,
			"; Gauge screen position (top-left corner, pixels). -1 -1 = automatic\n"
			"; bottom-center. Easiest way to set it: unlock overlays (F11) and drag\n"
			"; the gauge with the mouse - the position saves here automatically.");
		config.SetLongValue("inventory", "gauge_y", -1);
		config.SetValue("inventory", "gauge_custom_bg", "0C0E12",
			"; Custom gauge colors (hex RRGGBB), used when flashlight_gauge_skin = custom.");
		config.SetValue("inventory", "gauge_custom_frame", "46546E");
		config.SetValue("inventory", "gauge_custom_label", "96AAC8");
		config.SetValue("inventory", "gauge_custom_fill_high", "78DC78");
		config.SetValue("inventory", "gauge_custom_fill_mid", "EBC85A");
		config.SetValue("inventory", "gauge_custom_fill_low", "EB5A50");
		config.SetValue("inventory", "hidden_maps", "infra_c1_m1",
			"; Comma-separated map-name fragments where the inventory overlay is hidden\n"
			"; entirely (maps that never use it - the office prologue has no flashlight\n"
			"; or camera). Add more fragments if other maps follow the same logic.");

		config.SetBoolValue("notes", "enabled", true,
			"; ===== Notes scratchpad =====\n; A resizable/movable window to jot puzzle/secret/ARG notes; text is saved to\n; silta_notes.txt. Toggle with the notes hotkey.");
		config.SetValue("notes", "text_color", "EBEBDC", "; Hex RRGGBB for the notes text.");
		config.SetValue("notes", "skin", "ncg",
			"; Notes skin: ncg (memo letterhead) | pad (yellow legal pad) | plain |\n"
			"; geolog (geocache log) | custom (colors below).");
		config.SetValue("notes", "custom_paper", "F5F5ED",
			"; Custom-skin colors (hex RRGGBB / RRGGBBAA), used when skin = custom.\n"
			"; rule/margin with alpha 00 = no ruled lines / no margin line.");
		config.SetValue("notes", "custom_ink", "1A1F33");
		config.SetValue("notes", "custom_rule", "7896CC00");
		config.SetValue("notes", "custom_margin", "D25A5A00");
		config.SetBoolValue("notes", "open_with_phone", false,
			"; Auto-open the notes window whenever the cursor is out (phone/menu).");
		config.SetDoubleValue("notes", "font_scale", 1.0,
			"; Text size multiplier for the notes window (1.0 = default).");

		config.SetBoolValue("sketch", "enabled", true,
			"; ===== Sketchbook (NCG survey sketch sheet) =====\n; A paintable canvas; export saved to sketches\\NCG_SURVEY_#####.png.");
		config.SetLongValue("sketch", "size", 1000,
			"; Square canvas resolution in pixels (used when width/height are 0).");
		config.SetLongValue("sketch", "width", 0,
			"; Canvas width in pixels (0 = use 'size'). Set width AND height for a\n"
			"; rectangular page. The canvas keeps this exact shape in-game: you can\n"
			"; only resize/enlarge the window (it letterboxes), never distort the page.");
		config.SetLongValue("sketch", "height", 0,
			"; Canvas height in pixels (0 = use 'size').");
		config.SetBoolValue("sketch", "survey_sheet", true,
			"; Draw a faint survey grid + an N.C.G. title block on the page.");
		config.SetBoolValue("sketch", "transparent", false,
			"; Transparent page so you can trace over the in-game scenery.");
		config.SetBoolValue("sketch", "smooth", true,
			"; Canvas display: false = crisp (point sampling, no edge fringe),\n; true = smooth (bilinear, softer but can fringe stroke edges).");
		config.SetLongValue("sketch", "default_brush", 4, "; Starting brush size (1-40).");
		config.SetValue("sketch", "paper_color", "F7F5EB",
			"; Sketchbook colors (hex RRGGBB, or RRGGBBAA where alpha matters).\n"
			"; paper_color = window background; ink_color = toolbar text;\n"
			"; grid_color = survey grid (supports alpha); letterhead_color = the\n"
			"; N.C.G. header, rule and title block. Grid/letterhead apply on the\n"
			"; next Clear / new sheet.");
		config.SetValue("sketch", "ink_color", "141A29");
		config.SetValue("sketch", "grid_color", "6E7D966E");
		config.SetValue("sketch", "letterhead_color", "141E4B");
		config.SetValue("sketch", "default_ink", "1A1F66", "; Starting ink colour (hex RRGGBB).");

		config.SetValue("survey", "surveyor", "M. SILTANEN",
			"; ===== Survey identity (sketch title block + photo caption) =====\n; Surveyor name stamped on sketches and burned into photos.");
		config.SetValue("survey", "date", "08.08.2016",
			"; In-world survey date stamped on sketches and photos.");

		config.SetBoolValue("calculator", "enabled", true,
			"; ===== N.C.G. field calculator =====\n; Calculator with scientific, programmer (bases/bitwise) and text/cipher\n; (hex<->ASCII, Caesar) modes, plus structural-analyst helpers.");
		config.SetValue("calculator", "mode", "basic",
			"; Startup mode: basic | scientific | programmer | text.");
		config.SetValue("calculator", "custom_window", "1C1F21",
			"; Custom-skin colors (hex RRGGBB), used when skin = custom. Easiest way\n"
			"; to edit them: the calculator's in-game Style tab + 'Save to silta.ini'.");
		config.SetValue("calculator", "custom_button", "333840");
		config.SetValue("calculator", "custom_button_hover", "4D545E");
		config.SetValue("calculator", "custom_button_active", "66571F");
		config.SetValue("calculator", "custom_accent", "8C9EBF");
		config.SetValue("calculator", "custom_display", "FFBD33");
		config.SetValue("calculator", "custom_display_bg", "0D0F0D");
		config.SetValue("calculator", "skin", "ncg",
			"; Calculator skin: ncg (default instrument) or osmo (Osmo Olut beer theme).");

		config.SetBoolValue("ending", "enabled", true,
			"; ===== N.C.G. study outlook =====\n; Panel turning your photo/corruption tallies into the game's ending thresholds.");

		config.SetBoolValue("contact_sheet", "enabled", true,
			"; ===== Photo contact sheet =====\n; Thumbnail grid of this session's photos with a click-to-isolate zoom view.");

		config.SetBoolValue("log", "verbose", false,
			"; ===== Logging =====\n; *** DEBUG ONLY *** Verbose silta.log: timestamps every line, flushes\n; immediately (so a crash leaves the last action in the log), and adds debug\n; breadcrumbs. Leave off in normal play; turn on when hunting a bug.");

		config.SetBoolValue("watermark", "enabled", true,
			"; ===== Version watermark =====\n; Small white 'SILTA v1.0' tag, shown ONLY in the main menu.");
		config.SetLongValue("watermark", "corner", 3,
			"; Corner: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right.");

		config.SetBoolValue("report", "enabled", true,
			"; ===== End-of-game survey report =====\n; On reaching an ending map, write NCG_Survey_Report.txt to the game folder\n; and show a themed in-game popup with the verdict.");
		config.SetValue("report", "ending_maps", "infra_c10_m3_reactor,epilogue,outro,ending",
			"; Comma-separated map-name fragments that count as 'the end'. The report\n"
			"; fires when the current map name contains any of these. Edit to match your\n"
			"; build's epilogue map (check it in console with 'status' or the top-right HUD).");
		config.SetDoubleValue("report", "popup_seconds", 12.0,
			"; How long the report popup stays on screen.");

		config.SetLongValue("hotkeys", "reload_config", VK_F6,
			"; ===== Hotkeys (Win32 virtual-key codes; 0 = disabled) =====\n; F6=0x75 F7=0x76 F8=0x77 F9=0x78 F10=0x79 F11=0x7A F12=0x7B. INS still toggles all.\n; Reload this ini live (colors/corners/sizes) without restarting.");
		config.SetLongValue("hotkeys", "toggle_counters", VK_F7, "; Show/hide the counters overlay.");
		config.SetLongValue("hotkeys", "toggle_inventory", VK_F8, "; Show/hide the battery overlay.");
		config.SetLongValue("hotkeys", "cycle_counters_corner", VK_F9, "; Move counters to the next screen corner.");
		config.SetLongValue("hotkeys", "cycle_inventory_corner", VK_F10, "; Move inventory to the next screen corner.");
		config.SetLongValue("hotkeys", "toggle_lock", VK_F11, "; Unlock to drag overlays with the mouse; lock snaps them back.");
		config.SetLongValue("hotkeys", "reset_position", VK_F12, "; Snap both overlays back to their configured corners.");
		config.SetLongValue("hotkeys", "toggle_notes", VK_F4, "; Show/hide the notes scratchpad.");
		config.SetLongValue("hotkeys", "toggle_sketch", VK_F3, "; Show/hide the NCG sketchbook.");
		config.SetLongValue("hotkeys", "toggle_calculator", VK_F2, "; Show/hide the field calculator (incl. cipher/programmer tools).");
		config.SetLongValue("hotkeys", "toggle_ending", VK_F1, "; Show/hide the study-outlook (ending predictor) panel.");
		config.SetLongValue("hotkeys", "toggle_contact", VK_F5, "; Show/hide the photo contact sheet.");
		config.SetValue("hotkeys", "key_color", "",
			"; Hotkey tip bar styling (hex RRGGBB). Empty = follow the overlay theme\n"
			"; colors (complete_color for keys, text_color for descriptions).");
		config.SetValue("hotkeys", "text_color", "");

		config.SetBoolValue("tweaks", "enabled", false,
			"; ===== Engine tweaks (ADVANCED, off by default) =====\n; Runs console commands on each map load via IVEngineClient.\n; engine_interface: leave blank to auto-try common versions, or set one.\n; clientcmd_index: IVEngineClient::ClientCmd vtable slot - VERIFY before enabling,\n; a wrong index can crash. Any other key here is treated as a command to run,\n; e.g.  captions = closecaption 1");
		config.SetValue("tweaks", "engine_interface", "");
		config.SetLongValue("tweaks", "clientcmd_index", 7);

		config.SaveFile("silta.ini");

		// Append a keyboard reference footer to help users pick hotkey VK codes.
		std::ofstream foot("silta.ini", std::ios::app);
		if (foot.is_open()) {
			foot << "\n"
				<< "; ============================================================\n"
				<< "; Hotkeys above use Win32 Virtual-Key codes (decimal).\n"
				<< "; Full list of codes:\n"
				<< ";   https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes\n"
				<< "; Keyboard input overview:\n"
				<< ";   https://learn.microsoft.com/en-us/windows/win32/inputdev/about-keyboard-input\n"
				<< "; Quick reference: F1=112 F2=113 F3=114 F4=115 F5=116 F6=117\n"
				<< ";   F7=118 F8=119 F9=120 F10=121 F11=122 F12=123\n"
				<< ";   INS=45 DEL=46 HOME=36 END=35 PGUP=33 PGDN=34\n"
				<< ";   A-Z=65..90  0-9=48..57  SPACE=32  TAB=9  0=disabled\n"
				<< "; ============================================================\n";
		}
	}

	// ----- Features -----
	g_SuccessCountersEnabled = config.GetBoolValue("features", "success_counters", true);
	g_FunctionalCameraEnabled = config.GetBoolValue("features", "functional_camera", true);
	g_InventoryOverlayEnabled = config.GetBoolValue("features", "inventory_overlay", true);

	// ----- Overlay style -----
	overlay::fontSize = config.GetLongValue("overlay", "font_size", 0);
	overlay::fontColor = ParseHexColor(config.GetValue("overlay", "text_color", "FFFFFF"), ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	overlay::fontColorMax = ParseHexColor(config.GetValue("overlay", "complete_color", "00FF00"), ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
	overlay::tipKeyColor = ParseHexColor(config.GetValue("hotkeys", "key_color", ""), overlay::fontColorMax);
	overlay::tipTextColor = ParseHexColor(config.GetValue("hotkeys", "text_color", ""), overlay::fontColor);
	overlay::titleColor = ParseHexColor(config.GetValue("overlay", "title_color", "666666"), ImVec4(0.40f, 0.40f, 0.40f, 1.0f));
	overlay::backgroundAlpha = static_cast<float>(config.GetDoubleValue("overlay", "background_alpha", 0.30));
	overlay::margin = config.GetLongValue("overlay", "margin", 10);
	overlay::countersCorner = ParseCorner(config.GetValue("overlay", "counters_corner", "bottom-right"), overlay::Corner::BottomRight);
	overlay::inventoryCorner = ParseCorner(config.GetValue("overlay", "inventory_corner", "top-left"), overlay::Corner::TopLeft);

	if (overlay::backgroundAlpha < 0.0f) overlay::backgroundAlpha = 0.0f;
	if (overlay::backgroundAlpha > 1.0f) overlay::backgroundAlpha = 1.0f;

	// ----- Camera -----
	// photo_width falls back to the legacy output_width key so old configs keep
	// their chosen resolution (with height following the aspect, as before).
	mod::functional_camera::outputWidth = static_cast<int>(config.GetLongValue("camera", "photo_width",
		config.GetLongValue("camera", "output_width", 660)));
	mod::functional_camera::outputHeight = static_cast<int>(config.GetLongValue("camera", "photo_height",
		config.KeyExists("camera", "photo_width") || !config.KeyExists("camera", "output_width") ? 480 : 0));
	mod::functional_camera::aspectCrop = (ToLower(config.GetValue("camera", "aspect", "crop")) != "stretch");
	mod::functional_camera::exifMake = config.GetValue("camera", "exif_make", "IMAGE-IN");
	mod::functional_camera::exifModel = config.GetValue("camera", "exif_model", "Crystal-shot");
	mod::functional_camera::exifSoftware = config.GetValue("camera", "exif_software", "");
	mod::functional_camera::savePng = (ToLower(config.GetValue("camera", "format", "jpg")) == "png");
	const std::string filter = ToLower(config.GetValue("camera", "upscale_filter", "linear"));
	mod::functional_camera::linearFilter = (filter != "point" && filter != "none" && filter != "nearest");
	mod::functional_camera::cameraNaming = (ToLower(config.GetValue("camera", "naming", "camera")) != "timestamp");

	// ----- Per-category counter style -----
	{
		struct CatKeys { const char* color; const char* show; };
		static const CatKeys catKeys[overlay::CategoryCount] = {
			{ "defects_color",    "defects_show" },
			{ "corruption_color", "corruption_show" },
			{ "repairs_color",    "repairs_show" },
			{ "geocaches_color",  "geocaches_show" },
			{ "flowmeters_color", "flowmeters_show" },
		};
		for (int i = 0; i < overlay::CategoryCount; ++i) {
			// Empty default -> ParseHexColor falls back to the compiled-in colour.
			overlay::categoryColor[i] = ParseHexColor(
				config.GetValue("counters", catKeys[i].color, ""), overlay::categoryColor[i]);
			overlay::categoryVisible[i] = config.GetBoolValue("counters", catKeys[i].show, true);
		}
	}

	// ----- Theme -----
	overlay::theme = (ToLower(config.GetValue("overlay", "theme", "native")) == "debug")
		? overlay::Theme::Debug : overlay::Theme::Native;

	// ----- Complete-row style + totals -----
	{
		const std::string cs = ToLower(config.GetValue("counters", "complete_style", "color"));
		if (cs == "check")    overlay::completeStyle = overlay::CompleteStyle::Check;
		else if (cs == "dim") overlay::completeStyle = overlay::CompleteStyle::Dim;
		else                  overlay::completeStyle = overlay::CompleteStyle::Color;
	}
	overlay::showTotal = config.GetBoolValue("counters", "show_total", true);
	overlay::canonLabels = config.GetBoolValue("counters", "canon_labels", false);
	overlay::showProgress = config.GetBoolValue("counters", "show_progress", true);
	overlay::progressSeconds = static_cast<float>(config.GetDoubleValue("counters", "progress_seconds", 5.0));

	// ----- Hotkeys -----
	overlay::hotkeys.reloadConfig         = config.GetLongValue("hotkeys", "reload_config",          overlay::hotkeys.reloadConfig);
	overlay::hotkeys.toggleCounters       = config.GetLongValue("hotkeys", "toggle_counters",        overlay::hotkeys.toggleCounters);
	overlay::hotkeys.toggleInventory      = config.GetLongValue("hotkeys", "toggle_inventory",       overlay::hotkeys.toggleInventory);
	overlay::hotkeys.cycleCountersCorner  = config.GetLongValue("hotkeys", "cycle_counters_corner",  overlay::hotkeys.cycleCountersCorner);
	overlay::hotkeys.cycleInventoryCorner = config.GetLongValue("hotkeys", "cycle_inventory_corner", overlay::hotkeys.cycleInventoryCorner);
	overlay::hotkeys.toggleLock           = config.GetLongValue("hotkeys", "toggle_lock",            overlay::hotkeys.toggleLock);
	overlay::hotkeys.resetPosition        = config.GetLongValue("hotkeys", "reset_position",         overlay::hotkeys.resetPosition);
	overlay::hotkeys.toggleNotes          = config.GetLongValue("hotkeys", "toggle_notes",           overlay::hotkeys.toggleNotes);
	overlay::hotkeys.toggleSketch         = config.GetLongValue("hotkeys", "toggle_sketch",          overlay::hotkeys.toggleSketch);
	overlay::hotkeys.toggleCalculator     = config.GetLongValue("hotkeys", "toggle_calculator",      overlay::hotkeys.toggleCalculator);
	overlay::hotkeys.toggleEnding         = config.GetLongValue("hotkeys", "toggle_ending",          overlay::hotkeys.toggleEnding);
	overlay::hotkeys.toggleContact        = config.GetLongValue("hotkeys", "toggle_contact",         overlay::hotkeys.toggleContact);

	// ----- Tweaks (console commands) -----
	g_TweaksEnabled = config.GetBoolValue("tweaks", "enabled", false);
	g_TweakEngineInterface = config.GetValue("tweaks", "engine_interface", "");
	g_TweakClientCmdIndex = config.GetLongValue("tweaks", "clientcmd_index", 7);
	g_TweakCommands.clear();
	{
		CSimpleIniA::TNamesDepend keys;
		config.GetAllKeys("tweaks", keys);
		for (const auto& entry : keys) {
			const std::string kl = ToLower(entry.pItem);
			if (kl == "enabled" || kl == "engine_interface" || kl == "clientcmd_index") {
				continue;
			}
			const char* cmd = config.GetValue("tweaks", entry.pItem, "");
			if (cmd != nullptr && *cmd != '\0') {
				g_TweakCommands.emplace_back(cmd);
			}
		}
	}

	// ----- Inventory (battery) colours -----
	overlay::inventoryColor[0] = ParseHexColor(config.GetValue("inventory", "flashlight_color", ""), overlay::inventoryColor[0]);
	overlay::inventoryColor[1] = ParseHexColor(config.GetValue("inventory", "camera_color", ""), overlay::inventoryColor[1]);
	overlay::inventoryColor[2] = ParseHexColor(config.GetValue("inventory", "oscoins_color", ""), overlay::inventoryColor[2]);
	overlay::invBatteryIcons = config.GetBoolValue("inventory", "battery_icons", true);
	overlay::invCoinIcon = config.GetBoolValue("inventory", "coin_icon", true);
	overlay::flashGauge = config.GetBoolValue("inventory", "flashlight_gauge", true);
	overlay::flashGaugeMax = static_cast<int>(config.GetLongValue("inventory", "flashlight_gauge_max", 0));
	mod::inventory::flashlightChargeOffset = static_cast<int>(config.GetLongValue("inventory", "flashlight_charge_offset", 0));
	mod::inventory::chargeAutoscan = config.GetBoolValue("inventory", "charge_autoscan", false);
	overlay::flashGaugeSeconds = static_cast<float>(config.GetDoubleValue("inventory", "flashlight_gauge_seconds", 3.0));
	overlay::flashGaugeFade = static_cast<float>(config.GetDoubleValue("inventory", "flashlight_gauge_fade", 0.4));
	overlay::gaugeX = static_cast<float>(config.GetLongValue("inventory", "gauge_x", -1));
	overlay::gaugeY = static_cast<float>(config.GetLongValue("inventory", "gauge_y", -1));
	if (overlay::flashGaugeFade < 0.0f) overlay::flashGaugeFade = 0.0f;
	if (overlay::flashGaugeFade > overlay::flashGaugeSeconds) overlay::flashGaugeFade = overlay::flashGaugeSeconds;
	overlay::flashGaugeNumbers = config.GetBoolValue("inventory", "flashlight_gauge_numbers", false);
	{
		const std::string gsk = ToLower(config.GetValue("inventory", "flashlight_gauge_skin", "subtle"));
		overlay::flashGaugeSkin = (gsk == "subtle") ? 1 : (gsk == "custom") ? 2 : 0;
		static const char* kGkeys[6] = { "gauge_custom_bg", "gauge_custom_frame", "gauge_custom_label",
			"gauge_custom_fill_high", "gauge_custom_fill_mid", "gauge_custom_fill_low" };
		for (int i = 0; i < 6; ++i) {
			overlay::gaugeCustom[i] = ParseHexColor(config.GetValue("inventory", kGkeys[i], ""), overlay::gaugeCustom[i]);
		}
	}
	{
		g_InvHideTokens.clear();
		std::string list = config.GetValue("inventory", "hidden_maps", "infra_c1_m1");
		size_t start = 0;
		while (start <= list.size()) {
			size_t comma = list.find(',', start);
			std::string tok = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			size_t a = tok.find_first_not_of(" \t");
			size_t b = tok.find_last_not_of(" \t");
			if (a != std::string::npos) g_InvHideTokens.push_back(ToLower(tok.substr(a, b - a + 1)));
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
	}
	// Re-evaluate for the current map (F6 reload); at startup g_Engine isn't up yet
	// and the InitMapStats hook does this on the first map load instead.
	if (g_Engine != nullptr) {
		const char* mn = g_Engine->get_map_name();
		std::string lm = mn ? ToLower(mn) : std::string();
		overlay::inventoryHiddenByMap = false;
		for (const std::string& tok : g_InvHideTokens) {
			if (!tok.empty() && lm.find(tok) != std::string::npos) { overlay::inventoryHiddenByMap = true; break; }
		}
	}

	// ----- Notes scratchpad -----
	overlay::notesEnabled = config.GetBoolValue("notes", "enabled", true);
	overlay::notesColor = ParseHexColor(config.GetValue("notes", "text_color", ""), overlay::notesColor);
	{
		// Canonical key is "skin"; the legacy "style" key from older configs is
		// still honored as a fallback.
		const std::string ns = ToLower(config.GetValue("notes", "skin",
			config.GetValue("notes", "style", "ncg")));
		if (ns == "pad")         overlay::notesSkin = overlay::NotesSkin::Pad;
		else if (ns == "plain")  overlay::notesSkin = overlay::NotesSkin::Plain;
		else if (ns == "geolog") overlay::notesSkin = overlay::NotesSkin::Geolog;
		else if (ns == "custom") overlay::notesSkin = overlay::NotesSkin::Custom;
		else                     overlay::notesSkin = overlay::NotesSkin::Ncg;
		static const char* kNkeys[4] = { "custom_paper", "custom_ink", "custom_rule", "custom_margin" };
		for (int i = 0; i < 4; ++i) {
			overlay::notesCustom[i] = ParseHexColor(config.GetValue("notes", kNkeys[i], ""), overlay::notesCustom[i]);
		}
	}
	overlay::notesAutoOpen = config.GetBoolValue("notes", "open_with_phone", false);
	overlay::notesFontScale = static_cast<float>(config.GetDoubleValue("notes", "font_scale", 1.0));
	overlay::hintsEnabled = config.GetBoolValue("overlay", "hotkey_tips", true);
	overlay::tipFade = config.GetBoolValue("overlay", "tip_fade", false);
	overlay::tipFadeSeconds = static_cast<float>(config.GetDoubleValue("overlay", "tip_fade_seconds", 8.0));
	overlay::saveLayout = config.GetBoolValue("overlay", "save_layout", true);
	overlay::countersFocusFade = config.GetBoolValue("overlay", "focus_fade_counters", false);
	overlay::invFocusFade = config.GetBoolValue("overlay", "focus_fade_inventory", false);
	overlay::focusTransparency = static_cast<int>(config.GetLongValue("overlay", "focus_transparency", 90));
	if (overlay::focusTransparency < 0) overlay::focusTransparency = 0;
	if (overlay::focusTransparency > 100) overlay::focusTransparency = 100;
	overlay::focusSeconds = static_cast<float>(config.GetDoubleValue("overlay", "focus_seconds", 2.5));
	overlay::focusRamp = static_cast<float>(config.GetDoubleValue("overlay", "focus_ramp", 0.35));
	overlay::locationNames = config.GetBoolValue("overlay", "location_names", true);

	// ----- Sketchbook -----
	overlay::sketchEnabled = config.GetBoolValue("sketch", "enabled", true);
	overlay::sketchSize = static_cast<int>(config.GetLongValue("sketch", "size", 1000));
	if (overlay::sketchSize < 256)  overlay::sketchSize = 256;
	if (overlay::sketchSize > 4096) overlay::sketchSize = 4096;
	{
		int w = static_cast<int>(config.GetLongValue("sketch", "width", 0));
		int h = static_cast<int>(config.GetLongValue("sketch", "height", 0));
		if (w <= 0) w = overlay::sketchSize;   // fall back to the square size
		if (h <= 0) h = overlay::sketchSize;
		if (w < 256) w = 256; if (w > 4096) w = 4096;
		if (h < 256) h = 256; if (h > 4096) h = 4096;
		overlay::sketchW = w;
		overlay::sketchH = h;
	}
	overlay::sketchSurvey = config.GetBoolValue("sketch", "survey_sheet", true);
	overlay::sketchTransparent = config.GetBoolValue("sketch", "transparent", false);
	overlay::sketchSmooth = config.GetBoolValue("sketch", "smooth", true);
	overlay::sketchDefaultBrush = static_cast<int>(config.GetLongValue("sketch", "default_brush", 4));
	overlay::sketchPaper = ParseHexColor(config.GetValue("sketch", "paper_color", ""), overlay::sketchPaper);
	overlay::sketchToolInk = ParseHexColor(config.GetValue("sketch", "ink_color", ""), overlay::sketchToolInk);
	overlay::sketchGrid = ParseHexColor(config.GetValue("sketch", "grid_color", ""), overlay::sketchGrid);
	overlay::sketchHead = ParseHexColor(config.GetValue("sketch", "letterhead_color", ""), overlay::sketchHead);
	if (overlay::sketchDefaultBrush < 1)  overlay::sketchDefaultBrush = 1;
	if (overlay::sketchDefaultBrush > 40) overlay::sketchDefaultBrush = 40;
	{
		ImVec4 ink = ParseHexColor(config.GetValue("sketch", "default_ink", "1A1F66"), ImVec4(0.10f, 0.12f, 0.40f, 1.0f));
		overlay::sketchDefaultInk[0] = ink.x;
		overlay::sketchDefaultInk[1] = ink.y;
		overlay::sketchDefaultInk[2] = ink.z;
	}

	// ----- Survey identity (shared by sketch + camera) -----
	overlay::surveyorName = config.GetValue("survey", "surveyor", "M. SILTANEN");
	overlay::surveyDate = config.GetValue("survey", "date", "08.08.2016");

	// ----- Field calculator -----
	overlay::calcEnabled = config.GetBoolValue("calculator", "enabled", true);
	overlay::endingEnabled = config.GetBoolValue("ending", "enabled", true);
	overlay::contactEnabled = config.GetBoolValue("contact_sheet", "enabled", true);
	g_LogVerbose = config.GetBoolValue("log", "verbose", false);
	overlay::watermark = config.GetBoolValue("watermark", "enabled", true);
	overlay::watermarkCorner = static_cast<int>(config.GetLongValue("watermark", "corner", 3));
	if (overlay::watermarkCorner < 0 || overlay::watermarkCorner > 3) overlay::watermarkCorner = 3;
	g_ReportEnabled = config.GetBoolValue("report", "enabled", true);
	g_ReportSeconds = static_cast<float>(config.GetDoubleValue("report", "popup_seconds", 12.0));
	{
		g_ReportTokens.clear();
		std::string list = config.GetValue("report", "ending_maps", "infra_c10_m3_reactor,epilogue,outro,ending");
		size_t start = 0;
		while (start <= list.size()) {
			size_t comma = list.find(',', start);
			std::string tok = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
			// trim spaces
			size_t a = tok.find_first_not_of(" \t");
			size_t b = tok.find_last_not_of(" \t");
			if (a != std::string::npos) g_ReportTokens.push_back(ToLower(tok.substr(a, b - a + 1)));
			if (comma == std::string::npos) break;
			start = comma + 1;
		}
	}
	{
		const std::string sk = ToLower(config.GetValue("calculator", "skin", "ncg"));
		overlay::calcSkin = (sk == "osmo" || sk == "osmo_olut") ? 1 : (sk == "custom") ? 2 : 0; // ncg/default -> 0
		static const char* kCkeys[7] = { "custom_window", "custom_button", "custom_button_hover",
			"custom_button_active", "custom_accent", "custom_display", "custom_display_bg" };
		for (int i = 0; i < 7; ++i) {
			overlay::calcCustom[i] = ParseHexColor(config.GetValue("calculator", kCkeys[i], ""), overlay::calcCustom[i]);
		}
	}
	{
		const std::string cm = ToLower(config.GetValue("calculator", "mode", "basic"));
		if (cm == "scientific")      overlay::calcMode = overlay::CalcMode::Scientific;
		else if (cm == "programmer") overlay::calcMode = overlay::CalcMode::Programmer;
		else if (cm == "text")       overlay::calcMode = overlay::CalcMode::Text;
		else                         overlay::calcMode = overlay::CalcMode::Basic;
	}

	// ----- Camera save toast -----
	mod::functional_camera::saveToast = config.GetBoolValue("camera", "save_toast", true);
	mod::functional_camera::toastSeconds = static_cast<float>(config.GetDoubleValue("camera", "toast_seconds", 0.5));
	mod::functional_camera::toastLabel = config.GetValue("camera", "toast_label", "saved to microSD\\DCIM\\");
	mod::functional_camera::burnIn = config.GetBoolValue("camera", "burn_in", true);
	mod::functional_camera::writeExif = config.GetBoolValue("camera", "exif", true);
	mod::functional_camera::exifDateTime = config.GetValue("camera", "exif_datetime", "2016:08:08 12:00:00");
	mod::functional_camera::exifDateTimeAuto = config.GetBoolValue("camera", "exif_datetime_auto", true);
	{
		const char* cal = config.GetValue("camera", "calibrate", "");
		float cx = 0.0f, cy = 0.0f, cz = 0.0f;
		if (cal && sscanf_s(cal, "%f %f %f", &cx, &cy, &cz) >= 2) {
			mod::functional_camera::originCalibX = cx;
			mod::functional_camera::originCalibY = cy;
			mod::functional_camera::originCalibValid = true;
		} else {
			mod::functional_camera::originCalibValid = false;
		}
		ImVec4 ac = ParseHexColor(config.GetValue("camera", "burn_in_accent_color", "FFEB96"), ImVec4(1.0f, 0.92f, 0.59f, 1.0f));
		ImVec4 tc = ParseHexColor(config.GetValue("camera", "burn_in_text_color", "E6E6E6"), ImVec4(0.90f, 0.90f, 0.90f, 1.0f));
		mod::functional_camera::burnAccent[0] = static_cast<unsigned char>(ac.x * 255.0f);
		mod::functional_camera::burnAccent[1] = static_cast<unsigned char>(ac.y * 255.0f);
		mod::functional_camera::burnAccent[2] = static_cast<unsigned char>(ac.z * 255.0f);
		mod::functional_camera::burnText[0] = static_cast<unsigned char>(tc.x * 255.0f);
		mod::functional_camera::burnText[1] = static_cast<unsigned char>(tc.y * 255.0f);
		mod::functional_camera::burnText[2] = static_cast<unsigned char>(tc.z * 255.0f);
		mod::functional_camera::burnBandAlpha = static_cast<float>(config.GetDoubleValue("camera", "burn_in_band", 0.35));
	}
	mod::functional_camera::subfolders = config.GetBoolValue("camera", "subfolders", true);
	mod::functional_camera::surveyLog = config.GetBoolValue("camera", "survey_log", true);
	mod::functional_camera::assetTag = config.GetBoolValue("camera", "asset_tag", true);
	mod::functional_camera::originOffset = static_cast<int>(config.GetLongValue("camera", "player_origin_offset", 0));
	mod::functional_camera::exifGps = config.GetBoolValue("camera", "exif_gps", true);
	mod::functional_camera::gpsLat0 = config.GetDoubleValue("camera", "gps_lat0", 57.0);
	mod::functional_camera::gpsLon0 = config.GetDoubleValue("camera", "gps_lon0", 19.0);
	mod::functional_camera::gpsUnitsPerDeg = config.GetDoubleValue("camera", "gps_units_per_degree", 100000.0);

	// ----- Colour palette preset -----
	{
		const std::string pal = ToLower(config.GetValue("overlay", "palette", "default"));
		auto setPalette = [](ImVec4 text, ImVec4 complete, ImVec4 title) {
			overlay::fontColor = text;
			overlay::fontColorMax = complete;
			overlay::titleColor = title;
		};
		if (pal == "osmo" || pal == "osmo_olut") {
			setPalette(ImVec4(1.0f, 0.85f, 0.55f, 1.0f), ImVec4(1.0f, 0.72f, 0.20f, 1.0f), ImVec4(0.70f, 0.55f, 0.30f, 1.0f));
		} else if (pal == "ncg") {
			setPalette(ImVec4(0.78f, 0.86f, 1.0f, 1.0f), ImVec4(0.55f, 0.78f, 1.0f, 1.0f), ImVec4(0.45f, 0.55f, 0.75f, 1.0f));
		} else if (pal == "corruption") {
			setPalette(ImVec4(1.0f, 0.72f, 0.72f, 1.0f), ImVec4(1.0f, 0.35f, 0.35f, 1.0f), ImVec4(0.70f, 0.40f, 0.40f, 1.0f));
		}
		// "default" leaves the individually-configured colours untouched.
	}

	// Propagate feature toggles to the overlay so each window draws independently.
	overlay::countersEnabled = g_SuccessCountersEnabled;
	overlay::inventoryEnabled = g_InventoryOverlayEnabled;
}

static void hook_game_functions() {
	g_LogWriter.open("silta.log", std::ios_base::out);
	g_LogWriter << overlay::kModName << " v" << overlay::kVersion << " - LOG BEGIN" << std::endl;
	SetUnhandledExceptionFilter(SiltaCrashFilter);

	load_config();
	overlay::LoadNotes();

	// Set this up here so that hopefully our structures are all ready.
	g_Engine = new infra::InfraEngine();

	g_Engine->hook_server(0x297100, &InitMapStats, &InitMapStats_orig);
	g_Engine->hook_server(0x2974E0, &StatSuccess, &StatSuccess_orig);
	g_Engine->hook_client(0x1CCA30, &CInfraCameraFreezeFrame__OnCommand, &CInfraCameraFreezeFrame__OnCommand_orig);

	GetPlayerByIndex = reinterpret_cast<GetPlayerByIndex_t>(g_Engine->get_client_ptr(0x51DD0));
}

static int(__thiscall* Weapon_Equip_orig)(void*, void*);

static int __fastcall Weapon_Equip_hook(void *thiz, int, void *wep) {
	return Weapon_Equip_orig(thiz, wep);
}

static const char* const kSiltaBanner[] = {
	"                                                                                             \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                               \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                              \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""        \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                              \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""        \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                                            \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                          \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                                  \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                          \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                               \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                 \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                               \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""          \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                             \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""               \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""             \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                           \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                  \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""    \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                          \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                    \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""        \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""    \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                          \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""              \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""       \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""        \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                          \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""    \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""           \xE2\x96\x88""\xE2\x96\x88",
	"                                                            \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                             \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""        \xE2\x96\x88""\xE2\x96\x88""                                                        \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                        \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                  \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                             \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""       \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                                 \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                              \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                              \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""    \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                           \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                    \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""  \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                       \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88"" \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                            \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""   \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                                  \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                              \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                                         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                                            \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                      \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                                                \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                                 \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""                                                                                                                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                             \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                         \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                     \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"                \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"           \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"       \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	" \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	" \xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88""\xE2\x96\x88",
	"",
	"                                                   ___  __          ___  __   __           __   ___ ___           __   __",
	"                                           | |\\ | |__  |__)  /\\      |  /  \\ /  \\ |    __ /__` |__   |      |\\/| /  \\ |  \\",
	"                                           | | \\| |    |  \\ /~~\\     |  \\__/ \\__/ |___    .__/ |___  |      |  | \\__/ |__/ .",
};

// Build the end-of-game N.C.G. survey report from the live counters, write it to
// the game folder, and pop a themed summary card. Fires once per ending-map load.
static void MaybeGenerateSurveyReport(const char* map_name) {
	if (!g_ReportEnabled || map_name == nullptr) return;
	std::string mn = ToLower(map_name);
	bool match = false;
	for (const std::string& tok : g_ReportTokens) {
		if (!tok.empty() && mn.find(tok) != std::string::npos) { match = true; break; }
	}
	if (!match) return;
	if (g_ReportLastMap == map_name) return; // already done for this load
	g_ReportLastMap = map_name;

	static const char* const kNames[overlay::CategoryCount] = {
		"Photographs", "Corruption ", "Repairs    ", "Geocaches  ", "Flow meters" };
	int totCur = 0, totMax = 0;
	std::string rows;
	int pct[overlay::CategoryCount] = { 0 };
	for (int i = 0; i < overlay::CategoryCount; ++i) {
		const int cur = mod::counters::GetCategoryCurrent(i);
		const int mx = mod::counters::GetCategoryMax(i);
		pct[i] = mx > 0 ? (cur * 100 / mx) : 0;
		totCur += cur; totMax += mx;
		char line[96];
		sprintf_s(line, sizeof(line), "%s : %4d / %-4d (%d%%)\r\n", kNames[i], cur, mx, pct[i]);
		rows += line;
	}
	const int totPct = totMax > 0 ? (totCur * 100 / totMax) : 0;

	const bool study = (pct[0] >= 50 && pct[1] >= 50);
	const bool raven = (pct[1] >= 90);
	std::string verdict = study ? "Study published (good ending)" : "Insufficient documentation (bad ending)";
	if (raven) verdict += " + Raven Research contact";

	// Write the report file to the game folder.
	std::string site = overlay::locationName.empty() ? std::string("Stalburg") : overlay::locationName;
	FILE* f = nullptr;
	if (fopen_s(&f, "NCG_Survey_Report.txt", "wb") == 0 && f) {
		// SILTA stylized ASCII logo (user artwork; UTF-8 block characters).
		for (const char* bl : kSiltaBanner) {
			fprintf(f, "%s\r\n", bl);
		}
		fprintf(f, "\r\n");
		fprintf(f, "N.C.G. STRUCTURAL SURVEY - FIELD REPORT\r\n");
		fprintf(f, "=======================================\r\n");
		fprintf(f, "Surveyor : %s\r\n", overlay::surveyorName.c_str());
		fprintf(f, "Date     : %s\r\n", overlay::surveyDate.c_str());
		fprintf(f, "Site     : %s\r\n", site.c_str());
		fprintf(f, "Map      : %s\r\n\r\n", map_name);
		fprintf(f, "%s\r\n", rows.c_str());
		fprintf(f, "Overall  : %d / %d (%d%%)\r\n\r\n", totCur, totMax, totPct);
		fprintf(f, "Outlook  : %s\r\n", verdict.c_str());
		fprintf(f, "\r\nNote: the game scores each Act separately; these are overall totals.\r\n");
		fclose(f);
	}

	// Popup summary.
	char body[256];
	sprintf_s(body, sizeof(body), "Photographs  %d%%\nCorruption   %d%%\nOverall      %d%%\n\n%s",
		pct[0], pct[1], totPct, verdict.c_str());
	overlay::ShowReport("N.C.G. SURVEY REPORT - FILED", body, g_ReportSeconds);
}

void __fastcall InitMapStats(void* this_ptr) {
	InitMapStats_orig(this_ptr);

	if (g_SuccessCountersEnabled) {
		mod::counters::InitMapStats();
	}

	mod::inventory::MapLoaded(g_Engine->get_map_name());

	// Hide the inventory overlay on maps that never use it.
	{
		const char* mn = g_Engine->get_map_name();
		std::string lm = mn ? ToLower(mn) : std::string();
		overlay::inventoryHiddenByMap = false;
		for (const std::string& tok : g_InvHideTokens) {
			if (!tok.empty() && lm.find(tok) != std::string::npos) { overlay::inventoryHiddenByMap = true; break; }
		}
	}

	// End-of-game survey report (fires on the configured ending map).
	MaybeGenerateSurveyReport(g_Engine->get_map_name());

	// Re-apply engine tweaks (cvars/commands the engine may reset between maps).
	apply_tweaks();
}

int __fastcall StatSuccess(void* this_ptr, int, const int event_type, const int count, const bool is_new) {
	const int r = StatSuccess_orig(this_ptr, event_type, count, is_new);

	if (g_SuccessCountersEnabled) {
		mod::counters::StatSuccess(event_type, count, is_new);
	}
	return r;
}


// Purpose: Determine when the CInfraCameraFreezeFrame receives a command.
int __fastcall CInfraCameraFreezeFrame__OnCommand(CInfraCameraFreezeFrame* thiz, int, void* lpKeyValues) {
	int ret;

	ret = CInfraCameraFreezeFrame__OnCommand_orig(thiz, lpKeyValues);

	// It's not a DoFlash command, don't care!
	if (g_Engine->KeyValues__GetInt(lpKeyValues, "DoFlash", 0) != 1) {
		return ret;
	}

	if (g_FunctionalCameraEnabled) {
		mod::functional_camera::OnTakePicture(thiz);
	}

	return ret;
}

HRESULT __stdcall EndScene(const LPDIRECT3DDEVICE9 pDevice) {
	// Live ini reload requested via hotkey (handled here, on the render thread).
	if (overlay::reloadRequested) {
		overlay::reloadRequested = false;
		load_config();
		// Re-read the counters from the current game state too. This also covers
		// loading a save: the counters live in Source's saved global entities, so
		// re-running the read syncs the overlay to the loaded progress.
		if (g_SuccessCountersEnabled) {
			mod::counters::InitMapStats();
		}
		g_LogWriter << "config reloaded via hotkey" << std::endl;
	}

	if (g_FunctionalCameraEnabled) {
		mod::functional_camera::EndScene(pDevice);
	}

	// Flashlight-charge autoscan (no-op unless [inventory] charge_autoscan is on).
	mod::inventory::ChargeAutoscanTick();

	// Origin calibration hunt (no-op unless [camera] calibrate is set and unfound).
	mod::functional_camera::CalibrationTick();

	const bool anyOverlay = g_SuccessCountersEnabled || g_InventoryOverlayEnabled || overlay::notesEnabled || overlay::sketchEnabled || overlay::calcEnabled || overlay::endingEnabled || overlay::contactEnabled;

	// The version watermark shows ONLY in the main menu, where the gameplay
	// overlays are suppressed - so overlay::Render must still run there (its
	// in-menu path draws nothing but the watermark).
	overlay::inMenu = g_Engine->is_in_main_menu() && !g_Engine->loading_screen_visible();

	if ((anyOverlay && overlay::shown && !g_Engine->is_in_main_menu() && !g_Engine->loading_screen_visible())
		|| (overlay::watermark && overlay::inMenu)) {
		overlay::Render(Base::Data::hWindow, pDevice);
	}

	return EndScene_orig(pDevice);
}

LRESULT CALLBACK WndProc(const HWND hWnd, const UINT uMsg, const WPARAM wParam, const LPARAM lParam) {
	if (g_SuccessCountersEnabled || g_InventoryOverlayEnabled || overlay::notesEnabled || overlay::sketchEnabled || overlay::calcEnabled || overlay::endingEnabled || overlay::contactEnabled) {
		overlay::WndProc(hWnd, uMsg, wParam, lParam);
	}

	// When the notes box is open and focused, keep keyboard/mouse out of the game.
	if (overlay::WantsToCapture(uMsg)) {
		return 0;
	}

	if (uMsg == WM_KEYDOWN && wParam == VK_DELETE) {
		void* globalBills = g_Engine->CGlobalEntityList__FindEntityByName(nullptr, "env_global_bills");

		g_LogWriter << "GlobalBills addr: " << globalBills << std::endl;

		CINFRA_Player* pPlayer = reinterpret_cast<CINFRA_Player*>(
			g_Engine->CGlobalEntityList__FindEntityByName(nullptr, "!player")
		);
		const CGlobalEntityList* pEntityList = g_Engine->server_entity_list();
		g_LogWriter << "Player Entity " << pPlayer << std::endl;
		g_LogWriter << "Entity List " << pEntityList << std::endl;

		/*
		for (int i = 0; i < NUM_ENT_ENTRIES; i++) {
			CEntInfo info = pEntityList->m_EntPtrArray[i];

			g_LogWriter << "Entity " << i << " class: " << info.m_iClassName << ", name: " << info.m_iName << std::endl;
		}*/

		g_LogWriter << "First weapon " << pEntityList->LookupEntity(&(pPlayer->m_Weapon0)) << std::endl;
		g_LogWriter << "Second weapon " << pEntityList->LookupEntity(&(pPlayer->m_Weapon1)) << std::endl;
		g_LogWriter << "Third weapon " << pEntityList->LookupEntity(&(pPlayer->m_Weapon2)) << std::endl;
	}

	return CallWindowProc(Base::Data::oWndProc, hWnd, uMsg, wParam, lParam);
}

HRESULT __stdcall DeviceReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
	if (overlay::imGuiInitialized) {
		LogI("d3d: device reset - releasing overlay objects");
		ImGui_ImplDX9_InvalidateDeviceObjects();
	}
	const HRESULT hr = DeviceReset_orig(pDevice, pPresentationParameters);
	if (overlay::imGuiInitialized) {
		if (SUCCEEDED(hr)) {
			ImGui_ImplDX9_CreateDeviceObjects();
			LogI("d3d: device reset OK - overlay objects recreated");
		} else {
			LogE("d3d: device Reset FAILED");
		}
	}
	return hr;
}

bool Base::Hooks::Init() {
	if (MH_Initialize() != MH_OK) {
		MessageBoxA(nullptr, "Failed to initialize MinHook!", "Error!", MB_OK);
		return false;
	}

	if (GetD3D9Device((void**)Data::pDeviceTable, D3DDEV9_LEN)) {
		void* pEndScene = Data::pDeviceTable[42];
		void* pReset = Data::pDeviceTable[16];

		MH_CreateHook(pEndScene, &EndScene, reinterpret_cast<LPVOID*>(&EndScene_orig));
		MH_EnableHook(pEndScene);
		MH_CreateHook(pReset, &DeviceReset, reinterpret_cast<LPVOID*>(&DeviceReset_orig));
		MH_EnableHook(pReset);

		Data::oWndProc  = (WndProc_t)SetWindowLongPtr(Data::hWindow, WNDPROC_INDEX, (LONG_PTR)WndProc);
		hook_game_functions();	

		return true;
	}

	return false;
}

bool Base::Hooks::Shutdown() {
	void* pEndScene = Data::pDeviceTable[42];
	void* pReset = Data::pDeviceTable[16];
	MH_DisableHook(pReset);

	if (overlay::imGuiInitialized) {
		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}

	delete g_Engine;

	MH_DisableHook(pEndScene);
	MH_RemoveHook(pEndScene);

	SetWindowLongPtr(Data::hWindow, WNDPROC_INDEX, (LONG_PTR)Data::oWndProc);

	MH_Uninitialize();
	return true;
}

BOOL CALLBACK EnumWindowsCallback(const HWND handle, LPARAM lParam) {
	DWORD wndProcId = 0;
	GetWindowThreadProcessId(handle, &wndProcId);

	if (GetCurrentProcessId() != wndProcId)
		return TRUE;

	Base::Data::hWindow = handle;
	return FALSE;
}

HWND GetProcessWindow() {
	Base::Data::hWindow = (HWND)nullptr;
	EnumWindows(EnumWindowsCallback, NULL);
	return Base::Data::hWindow;
}

bool GetD3D9Device(void** pTable, const size_t Size) {
	while (true) {
		void *base = GetModuleAddress("shaderapidx9.dll");

		IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(PTR_ADD(base, 0x17E70C));

		if (!dev) {
			Sleep(500);
			continue;
		}

		memcpy(pTable, *reinterpret_cast<void***>(dev), Size * sizeof(void*));
		break;
	}

	GetProcessWindow();
	return true;
}
