#include "stdafx.h"
#include <base.h>
#include "overlay.h"
#include <font.hpp>
#include "Utils.h"
#include <fstream>
#include <iterator>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include "brad.h" // quack

void LogI(const std::string&); // silta.log (defined in infra.cpp)
void LogV(const std::string&); // silta.log verbose (defined in infra.cpp)
#include "inventory.h"
#include "counters.h"
#include "functional_camera.h"
#include <map>
#include <algorithm>
#include <climits>

ImFont* g_font = NULL;

// Configurable style — defaults here, overridden from the ini in load_config().
ImVec4 overlay::fontColor = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
ImVec4 overlay::fontColorMax = ImVec4(0.00f, 1.00f, 0.00f, 1.00f);
ImVec4 overlay::titleColor = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
float  overlay::backgroundAlpha = 0.30f;
int    overlay::margin = 10;
overlay::Corner overlay::countersCorner = overlay::Corner::BottomRight;
overlay::Corner overlay::inventoryCorner = overlay::Corner::TopLeft;

// Per-category defaults: muted, distinct hues so categories read apart at a
// glance, while still flipping to complete_color when a category is finished.
ImVec4 overlay::categoryColor[overlay::CategoryCount] = {
	ImVec4(0.88f, 0.88f, 0.88f, 1.00f), // Defects    - near white
	ImVec4(1.00f, 0.43f, 0.43f, 1.00f), // Corruption - soft red
	ImVec4(0.43f, 0.82f, 1.00f, 1.00f), // Repairs    - soft blue
	ImVec4(1.00f, 0.85f, 0.43f, 1.00f), // Geocaches  - soft gold
	ImVec4(0.71f, 0.55f, 1.00f, 1.00f), // Flow meters- soft purple
};
bool overlay::categoryVisible[overlay::CategoryCount] = { true, true, true, true, true };

// Inventory overlay colours: flashlight, camera batteries, OS coins.
ImVec4 overlay::inventoryColor[3] = {
	ImVec4(1.00f, 0.88f, 0.54f, 1.00f), // Flashlight - warm amber
	ImVec4(0.54f, 0.82f, 1.00f, 1.00f), // Camera     - soft blue
	ImVec4(1.00f, 0.85f, 0.43f, 1.00f), // OS coins   - gold
};
bool   overlay::invBatteryIcons = true;
bool   overlay::inventoryHiddenByMap = false;
bool   overlay::countersFocusFade = false;
bool   overlay::invFocusFade = false;
int    overlay::focusTransparency = 90;
float  overlay::focusSeconds = 2.5f;
float  overlay::focusRamp = 0.35f;
bool   overlay::invCoinIcon = true;
bool   overlay::flashGauge = true;
int    overlay::flashGaugeMax = 0; // 0 = auto
float  overlay::flashGaugeSeconds = 3.0f;
float  overlay::flashGaugeFade = 0.4f;
bool   overlay::flashGaugeNumbers = false;
int    overlay::flashGaugeSkin = 1; // subtle
float  overlay::gaugeX = -1.0f;
float  overlay::gaugeY = -1.0f;
ImVec4 overlay::gaugeCustom[6] = {
	ImVec4(0.047f, 0.055f, 0.071f, 0.80f), // bg
	ImVec4(0.275f, 0.329f, 0.431f, 1.0f),  // frame
	ImVec4(0.588f, 0.667f, 0.784f, 1.0f),  // label
	ImVec4(0.471f, 0.863f, 0.471f, 1.0f),  // fill high
	ImVec4(0.922f, 0.784f, 0.353f, 1.0f),  // fill mid
	ImVec4(0.922f, 0.353f, 0.314f, 1.0f),  // fill low
};

bool   overlay::notesEnabled = true;
bool   overlay::notesShown = false;
ImVec4 overlay::notesColor = ImVec4(0.92f, 0.92f, 0.86f, 1.00f);
overlay::NotesSkin overlay::notesSkin = overlay::NotesSkin::Ncg;
ImVec4 overlay::notesCustom[4] = {
	ImVec4(0.96f, 0.96f, 0.93f, 1.00f), // paper
	ImVec4(0.10f, 0.12f, 0.20f, 1.00f), // ink
	ImVec4(0.47f, 0.59f, 0.80f, 0.00f), // rule (alpha 0 = no ruling)
	ImVec4(0.82f, 0.35f, 0.35f, 0.00f), // margin (alpha 0 = no margin line)
};
ImVec4 overlay::sketchPaper   = ImVec4(0.97f, 0.96f, 0.92f, 1.00f);
ImVec4 overlay::sketchToolInk = ImVec4(0.08f, 0.10f, 0.16f, 1.00f);
ImVec4 overlay::sketchGrid    = ImVec4(0.431f, 0.490f, 0.588f, 0.431f);
ImVec4 overlay::sketchHead    = ImVec4(0.078f, 0.118f, 0.294f, 1.00f);
bool   overlay::notesAutoOpen = false;
float  overlay::notesFontScale = 1.0f;
bool   overlay::hintsEnabled = true;
bool   overlay::tipFade = false;
float  overlay::tipFadeSeconds = 8.0f;
bool   overlay::saveLayout = true;
bool   overlay::canonLabels = false;
bool   overlay::locationNames = true;
bool   overlay::showProgress = true;
float  overlay::progressSeconds = 5.0f;
bool   overlay::calcEnabled = true;
bool   overlay::calcShown = false;
overlay::CalcMode overlay::calcMode = overlay::CalcMode::Basic;
int    overlay::calcSkin = 0;
ImVec4 overlay::calcCustom[7] = {
	ImVec4(0.11f, 0.12f, 0.13f, 0.96f), // window
	ImVec4(0.20f, 0.22f, 0.25f, 1.0f),  // button
	ImVec4(0.30f, 0.33f, 0.37f, 1.0f),  // button hover
	ImVec4(0.40f, 0.34f, 0.12f, 1.0f),  // button active
	ImVec4(0.55f, 0.62f, 0.75f, 1.0f),  // accent
	ImVec4(1.00f, 0.74f, 0.20f, 1.0f),  // display text
	ImVec4(0.05f, 0.06f, 0.05f, 1.0f),  // display bg
};
ImVec4 overlay::tipKeyColor = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
ImVec4 overlay::tipTextColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
bool   overlay::endingEnabled = true;
bool   overlay::endingShown = false;
bool   overlay::contactEnabled = true;
bool   overlay::contactShown = false;
bool   overlay::watermark = true;
int    overlay::watermarkCorner = 3; // bottom-right
std::string overlay::watermarkText = "";
bool   overlay::forceBackbuffer = false;
bool   overlay::useHotkeyPolling = false;
bool   overlay::usePresentRender = false;
bool   overlay::inMenu = true;
bool   overlay::sketchEnabled = true;
bool   overlay::sketchShown = false;
int    overlay::sketchSize = 1000;
int    overlay::sketchW = 1000;
int    overlay::sketchH = 1000;
bool   overlay::sketchSurvey = true;
bool   overlay::sketchTransparent = false;
bool   overlay::sketchSmooth = true;
int    overlay::sketchDefaultBrush = 4;
float  overlay::sketchDefaultInk[3] = { 0.10f, 0.12f, 0.40f };
std::string overlay::surveyorName = "M. SILTANEN";
std::string overlay::surveyDate = "08.08.2016";
std::string overlay::locationName = "";

// Toast state (brief on-screen confirmation).
static std::string g_ToastText;
static long long    g_ToastShownMs = 0;
static long long    g_ToastDurationMs = 0;
void overlay::ShowToast(const std::string& text, float seconds) {
	if (seconds <= 0.0f) { g_ToastText.clear(); return; }
	g_ToastText = text;
	g_ToastShownMs = CurrentTimeMillis();
	g_ToastDurationMs = static_cast<long long>(seconds * 1000.0f);
}

// End-of-game report card (a larger, centered, themed popup).
static std::string g_ReportTitle;
static std::string g_ReportBody;
static long long    g_ReportShownMs = 0;
static long long    g_ReportDurationMs = 0;
void overlay::ShowReport(const std::string& title, const std::string& body, float seconds) {
	if (seconds <= 0.0f) return;
	g_ReportTitle = title;
	g_ReportBody = body;
	g_ReportShownMs = CurrentTimeMillis();
	g_ReportDurationMs = static_cast<long long>(seconds * 1000.0f);
}

overlay::Theme         overlay::theme = overlay::Theme::Native;
overlay::CompleteStyle overlay::completeStyle = overlay::CompleteStyle::Color;
bool overlay::showTotal = true;
int  overlay::totalCurrent = 0;
int  overlay::totalMax = 0;
int  overlay::catCurrent[overlay::CategoryCount] = { 0 };
int  overlay::catMax[overlay::CategoryCount] = { 0 };
int  overlay::currentAct = 1;
bool overlay::locked = true;
bool overlay::forceReposition = false;
bool overlay::reloadRequested = false;
int overlay::debugReportKey = 0;
volatile bool overlay::forceReportRequested = false;
overlay::Hotkeys overlay::hotkeys = {
	VK_F6,  // reloadConfig
	VK_F7,  // toggleCounters
	VK_F8,  // toggleInventory
	VK_F9,  // cycleCountersCorner
	VK_F10, // cycleInventoryCorner
	VK_F11, // toggleLock (unlock to drag)
	VK_F12, // resetPosition
	VK_F4,  // toggleNotes
	VK_F3,  // toggleSketch
	VK_F2,  // toggleCalculator (borrowed from the old decoder)
	VK_F1,  // toggleEnding (study outlook)
	VK_F5,  // toggleContact (contact sheet)
};

// ----- Notes scratchpad backing store -----
static const char* kNotesFile = "silta_notes.txt";
static char g_NotesBuffer[16384] = { 0 };
static bool g_NotesLoaded = false;

static overlay::Corner NextCorner(overlay::Corner c) {
	switch (c) {
	case overlay::Corner::TopLeft:     return overlay::Corner::TopRight;
	case overlay::Corner::TopRight:    return overlay::Corner::BottomRight;
	case overlay::Corner::BottomRight: return overlay::Corner::BottomLeft;
	default:                           return overlay::Corner::TopLeft;
	}
}

// Apply the chosen visual style to the global ImGui style. Cheap; called each frame.
static void ApplyTheme() {
	ImGuiStyle& style = ImGui::GetStyle();
	if (overlay::theme == overlay::Theme::Native) {
		// Squared, bordered, snug — reads as part of the INFRA HUD.
		style.WindowRounding = 0.0f;
		style.WindowBorderSize = 1.0f;
		style.WindowPadding = ImVec2(8.0f, 6.0f);
		style.ItemSpacing = ImVec2(8.0f, 3.0f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.08f, 0.11f, 1.00f); // dark navy
		style.Colors[ImGuiCol_Border]   = ImVec4(0.32f, 0.40f, 0.50f, 0.55f); // steel blue
	} else {
		// Plain debug look.
		style.WindowRounding = 6.0f;
		style.WindowBorderSize = 0.0f;
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.ItemSpacing = ImVec2(8.0f, 4.0f);
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		style.Colors[ImGuiCol_Border]   = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	}
}

// Anchor a window of the given size to a screen corner, inset by `margin`.
static ImVec2 CornerPos(overlay::Corner corner, float winW, float winH, int clientW, int clientH) {
	const float m = static_cast<float>(overlay::margin);
	const bool left = (corner == overlay::Corner::TopLeft || corner == overlay::Corner::BottomLeft);
	const bool top = (corner == overlay::Corner::TopLeft || corner == overlay::Corner::TopRight);
	const float x = left ? m : (clientW - winW - m);
	const float y = top ? m : (clientH - winH - m);
	return ImVec2(x, y);
}

static int DetermineFontSize(const RECT &rect) {
	int font_size = 12;
	int width = rect.right - rect.left;

	// Auto
	if (overlay::fontSize == 0) {
		switch (width) {
		case 1176:
			font_size = 6;
			break;
		case 1280:
		case 1360:
		case 1366:
			font_size = 7;
			break;
		case 1600:
			font_size = 9;
			break;
		case 1768:
			font_size = 10;
			break;
		case 1920:
			font_size = 12;
			break;
		case 2560:
		case 3440:
			font_size = 18;
			break;
		case 3840:
			font_size = 30;
			break;
		}
	}
	else {
		font_size = overlay::fontSize;
	}

	return font_size;
}

static ImVec2 CalcMaxTextSize(const std::vector<std::string>& values) {
	float maxWidth = 0;
	float maxHeight = 0;

	for (const auto &value : values) {
		const ImVec2 size = ImGui::CalcTextSize(value.c_str());

		if (size.x > maxWidth) {
			maxWidth = size.x;
		}

		if (size.y > maxHeight) {
			maxHeight = size.y;
		}
	}

	return { maxWidth, maxHeight };
}

overlay::OverlayLine_t::OverlayLine_t(std::string name, std::string value, const ImVec4 &nameColor, const ImVec4 &valueColor) :
	name(std::move(name)), value(std::move(value)), nameColor(nameColor), valueColor(valueColor), blinksLeft(0), lastBlink(-1) {
}

bool overlay::shown = true;
bool overlay::countersEnabled = true;
bool overlay::inventoryEnabled = true;
overlay::OverlayLine_t overlay::title = overlay::OverlayLine_t();
std::vector<overlay::OverlayLine_t> overlay::lines = std::vector<overlay::OverlayLine_t>();
bool overlay::imGuiInitialized = false;
int overlay::fontSize = 0;

// Begin an overlay window anchored to a corner. Locked = pinned, borderless,
// fixed size (clean HUD). Unlocked = shows a title bar (a clear drag handle) and
// auto-sizes, so it can be dragged with the mouse while the cursor is free (e.g.
// phone/menu out). Locking or a reset snaps it back to the configured corner.
static void BeginAnchoredWindow(const char* name, const ImVec2& size, overlay::Corner corner, int clientW, int clientH) {
	const ImVec2 pos = CornerPos(corner, size.x, size.y, clientW, clientH);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse;
	if (!overlay::saveLayout) {
		flags |= ImGuiWindowFlags_NoSavedSettings;
	}

	if (overlay::locked) {
		flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
		// Keep the current (possibly dragged) position; only snap on an explicit
		// reset/corner-cycle (forceReposition).
		ImGui::SetNextWindowPos(pos, overlay::forceReposition ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(size, ImGuiCond_Always);
	} else {
		// Title bar acts as the drag handle; auto-resize keeps content fitting.
		flags |= ImGuiWindowFlags_AlwaysAutoResize;
		ImGui::SetNextWindowPos(pos, overlay::forceReposition ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
	}

	ImGui::SetNextWindowBgAlpha(overlay::backgroundAlpha);
	ImGui::Begin(name, nullptr, flags);
}

static bool CursorIsVisible(); // defined below (used for hover reveal)

// ---- Focus fade ----
// Optional immersion mode: the counters / inventory overlays rest at high
// transparency and "wake up" to full visibility for a few seconds whenever
// their numbers change (a pickup, a successful photo...) or the mouse hovers
// them, then sink back. All timings/level via [overlay] focus_* keys.
namespace {
	struct FocusState {
		double start = -1e9;   // when the current wake began
		double until = -1e9;   // when it ends
		bool   hovered = false;
		float  hoverK = 0.0f;  // smoothed hover contribution

		void Trigger() {
			const double now = ImGui::GetTime();
			if (now > until) start = now;  // fresh wake (else just extend)
			until = now + overlay::focusSeconds;
		}
	};
	FocusState g_FocusCounters, g_FocusInv;

	float FocusAlpha(FocusState& f, bool enabled) {
		if (!enabled) return 1.0f;
		const double now = ImGui::GetTime();
		float idle = 1.0f - static_cast<float>(overlay::focusTransparency) / 100.0f;
		if (idle < 0.03f) idle = 0.03f;   // never fully invisible
		if (idle > 1.0f) idle = 1.0f;
		const float ramp = overlay::focusRamp > 0.01f ? overlay::focusRamp : 0.01f;
		float rise = static_cast<float>((now - f.start) / ramp);
		float fall = static_cast<float>((f.until - now) / ramp);
		float k = rise < fall ? rise : fall;
		if (k < 0.0f) k = 0.0f;
		if (k > 1.0f) k = 1.0f;
		// Smooth hover reveal (approaches its target over the ramp time).
		const float target = f.hovered ? 1.0f : 0.0f;
		const float dt = ImGui::GetIO().DeltaTime;
		const float step = dt / ramp;
		f.hoverK += (target - f.hoverK) * (step > 1.0f ? 1.0f : step);
		if (f.hoverK > k) k = f.hoverK;
		return idle + (1.0f - idle) * k;
	}
}

static void RenderCounters() {
	// Focus-fade event detection: wake on any per-category tally change. When
	// the maximums change (a map load), resample silently so loading a level
	// doesn't flash the overlay.
	if (overlay::countersFocusFade) {
		static int lastCur[overlay::CategoryCount];
		static int lastMax[overlay::CategoryCount];
		static bool primed = false;
		bool maxChanged = !primed;
		for (int i = 0; i < overlay::CategoryCount && !maxChanged; ++i) {
			if (lastMax[i] != overlay::catMax[i]) maxChanged = true;
		}
		if (maxChanged) {
			for (int i = 0; i < overlay::CategoryCount; ++i) {
				lastCur[i] = overlay::catCurrent[i];
				lastMax[i] = overlay::catMax[i];
			}
			primed = true;
		} else {
			bool changed = false;
			for (int i = 0; i < overlay::CategoryCount; ++i) {
				if (lastCur[i] != overlay::catCurrent[i]) { changed = true; lastCur[i] = overlay::catCurrent[i]; }
			}
			if (changed) g_FocusCounters.Trigger();
		}
		g_FocusCounters.hovered = false; // set after Begin below
	}

	long long now;

	now = CurrentTimeMillis();

	const ImGuiStyle& style = ImGui::GetStyle();

	// Optional overall-completion line shown at the bottom.
	std::string totalStr;
	if (overlay::showTotal) {
		const int pct = (overlay::totalMax > 0)
			? static_cast<int>((100.0 * overlay::totalCurrent / overlay::totalMax) + 0.5)
			: 100;
		totalStr = "Total: " + std::to_string(overlay::totalCurrent) + " / " +
			std::to_string(overlay::totalMax) + " (" + std::to_string(pct) + "%)";
	}

	// Size the window to the widest visible row (map name, any shown counter row,
	// or the total line), measuring name/value separately to include SameLine()
	// spacing so the "/ max" part never clips. Re-applied every frame.
	// Values render in a fixed column (widest visible name + spacing) so rows
	// stay aligned regardless of label length.
	float nameColWidth = 0.0f;
	float valueColWidth = 0.0f;
	int visibleCount = 0;
	for (size_t i = 0; i < overlay::lines.size(); i++) {
		if (i < static_cast<size_t>(overlay::CategoryCount) && !overlay::categoryVisible[i]) {
			continue;
		}
		const overlay::OverlayLine_t& line = overlay::lines[i];
		nameColWidth = std::max(nameColWidth, ImGui::CalcTextSize(line.name.c_str()).x);
		valueColWidth = std::max(valueColWidth, ImGui::CalcTextSize(line.value.c_str()).x);
		visibleCount++;
	}
	const float valueColumn = style.WindowPadding.x + nameColWidth + style.ItemSpacing.x;
	float contentWidth = std::max(ImGui::CalcTextSize(overlay::title.value.c_str()).x,
		nameColWidth + style.ItemSpacing.x + valueColWidth);
	if (overlay::showTotal) {
		contentWidth = std::max(contentWidth, ImGui::CalcTextSize(totalStr.c_str()).x);
	}

	const int rowCount = visibleCount + 1 + (overlay::showTotal ? 1 : 0); // +1 title
	const float windowWidth = contentWidth + style.WindowPadding.x * 2.0f + 2.0f;
	const float windowHeight = rowCount * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int clientHeight = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;

	BeginAnchoredWindow("Counters", ImVec2(windowWidth, windowHeight), overlay::countersCorner, clientWidth, clientHeight);
	if (overlay::countersFocusFade) {
		g_FocusCounters.hovered = CursorIsVisible() && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	}

	ImGui::TextColored(overlay::titleColor, overlay::title.value.c_str()); // map name

	for (int i = 0; i < static_cast<int>(overlay::lines.size()); i++) {
		if (i < overlay::CategoryCount && !overlay::categoryVisible[i]) {
			continue;
		}
		overlay::OverlayLine_t& line = overlay::lines[i];
		ImVec4 color;

		// Handle blinking the line if necessary
		if (line.blinksLeft > 0) {
			color = ((line.blinksLeft % 2) == 0) ? overlay::fontColorMax : overlay::fontColor;

			if ((now - line.lastBlink) >= 500) {
				line.blinksLeft--;
				line.lastBlink = now;
			}
		}
		else {
			color = line.nameColor;
		}

		ImGui::TextColored(color, line.name.c_str());
		ImGui::SameLine(valueColumn);
		ImGui::TextColored(line.valueColor, line.value.c_str());
	}

	if (overlay::showTotal) {
		ImGui::TextColored(overlay::titleColor, totalStr.c_str());
	}

	ImGui::End();
}

// Little vector glyphs drawn next to inventory lines (no texture/SVG needed).
static void DrawBatteryIcon(ImDrawList* dl, ImVec2 p, float h, ImU32 col) {
	const ImU32 dark = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
	const float y0 = p.y + h * 0.22f, y1 = p.y + h * 0.78f;
	const float x0 = p.x + h * 0.06f, x1 = p.x + h * 0.78f;
	dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), dark, 2.0f);          // shell
	dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col, 2.0f, 0, 1.6f);        // outline
	dl->AddRectFilled(ImVec2(x1, p.y + h * 0.36f), ImVec2(x1 + h * 0.10f, p.y + h * 0.64f), col); // terminal
	// lightning bolt (energy)
	const ImVec2 a(p.x + h * 0.44f, y0 + h * 0.04f), b(p.x + h * 0.26f, p.y + h * 0.52f),
		c(p.x + h * 0.40f, p.y + h * 0.52f), d(p.x + h * 0.30f, y1 - h * 0.02f),
		e(p.x + h * 0.54f, p.y + h * 0.44f), f(p.x + h * 0.40f, p.y + h * 0.44f);
	dl->AddTriangleFilled(a, b, c, col);
	dl->AddTriangleFilled(d, e, f, col);
}

static void DrawCoinIcon(ImDrawList* dl, ImVec2 p, float h, ImU32 col) {
	const ImVec2 ctr(p.x + h * 0.42f, p.y + h * 0.5f);
	const float r = h * 0.34f;
	const ImU32 dark = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
	dl->AddCircleFilled(ctr, r, col, 16);
	dl->AddCircle(ctr, r, dark, 16, 1.6f);
	dl->AddCircle(ctr, r * 0.55f, dark, 12, 1.2f);   // engraved rim
}

static void RenderInventory() {
	// Some maps never use the inventory (the office prologue has no flashlight or
	// camera), so it can be hidden per-map via [inventory] hidden_maps.
	if (overlay::inventoryHiddenByMap) {
		return;
	}
	// Focus-fade event detection: wake on battery-count or coin changes (NOT the
	// live flashlight charge - that ticks every 2 s and would pin it visible).
	// When the counter pointers appear (map load), resample silently.
	if (overlay::invFocusFade) {
		const bool havePtrs = mod::inventory::flashlightBatteriesCounter != nullptr ||
			mod::inventory::cameraBatteriesCounter != nullptr || mod::inventory::osCoinsCounter != nullptr;
		const int f = mod::inventory::flashlightBatteriesCounter ? *mod::inventory::flashlightBatteriesCounter : 0;
		const int c = mod::inventory::cameraBatteriesCounter ? *mod::inventory::cameraBatteriesCounter : 0;
		const float coins = mod::inventory::osCoinsCounter ? *mod::inventory::osCoinsCounter : 0.0f;
		static bool hadPtrs = false;
		static int lastF = 0, lastC = 0;
		static float lastCoins = 0.0f;
		if (havePtrs && !hadPtrs) {
			lastF = f; lastC = c; lastCoins = coins; // map load: silent baseline
		} else if (havePtrs) {
			if (f != lastF || c != lastC || coins > lastCoins + 0.5f || coins < lastCoins - 0.5f) {
				g_FocusInv.Trigger();
				lastF = f; lastC = c; lastCoins = coins;
			}
		}
		hadPtrs = havePtrs;
		g_FocusInv.hovered = false; // set after Begin below
	}
	// Build the list of stuff to render, each with its own colour + icon.
	std::vector<std::string> lines;
	std::vector<ImVec4> colors;
	std::vector<int> kinds;        // 0 = battery, 1 = coin
	std::vector<bool> drawIcon;

	if (mod::inventory::flashlightBatteriesCounter != nullptr) {
		lines.push_back(std::string("Flashlight: ") + std::to_string(*mod::inventory::flashlightBatteriesCounter));
		colors.push_back(overlay::inventoryColor[0]); kinds.push_back(0); drawIcon.push_back(overlay::invBatteryIcons);
	}

	if (mod::inventory::cameraBatteriesCounter != nullptr) {
		lines.push_back(std::string("Camera:     ") + std::to_string(*mod::inventory::cameraBatteriesCounter));
		colors.push_back(overlay::inventoryColor[1]); kinds.push_back(0); drawIcon.push_back(overlay::invBatteryIcons);
	}

	if (mod::inventory::osCoinsCounter != nullptr) {
		const int cv = static_cast<int>(*mod::inventory::osCoinsCounter);
		lines.push_back(std::string("OS Coins:   ") + std::to_string(cv));
		colors.push_back(overlay::inventoryColor[2]); kinds.push_back(1);
		drawIcon.push_back(overlay::invCoinIcon && cv > 0);   // coin glyph only once collected
	}

	if (lines.empty()) {
		return;
	}

	const ImGuiStyle& style = ImGui::GetStyle();
	const ImVec2 maxSize = CalcMaxTextSize(lines);
	const float lineH = ImGui::GetTextLineHeight();
	const float iconCol = (overlay::invBatteryIcons || overlay::invCoinIcon) ? (lineH + 5.0f) : 0.0f;

	const float windowWidth = maxSize.x + iconCol + style.WindowPadding.x * 2.0f + 2.0f;
	const float windowHeight = static_cast<float>(lines.size()) * ImGui::GetTextLineHeightWithSpacing() + style.WindowPadding.y * 2.0f;

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int clientHeight = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;

	BeginAnchoredWindow("Inventory", ImVec2(windowWidth, windowHeight), overlay::inventoryCorner, clientWidth, clientHeight);
	if (overlay::invFocusFade) {
		g_FocusInv.hovered = CursorIsVisible() && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	}

	ImDrawList* dl = ImGui::GetWindowDrawList();
	for (size_t i = 0; i < lines.size(); i++) {
		// Text first, glyph trailing the number: "Flashlight: 3 [icon]".
		ImGui::TextColored(colors[i], lines[i].c_str());
		if (iconCol > 0.0f && drawIcon[i]) {
			ImGui::SameLine(0.0f, 6.0f);
			const ImVec2 p = ImGui::GetCursorScreenPos();
			const ImU32 col = ImGui::GetColorU32(colors[i]);
			if (kinds[i] == 0) DrawBatteryIcon(dl, p, lineH, col);
			else               DrawCoinIcon(dl, p, lineH, col);
			ImGui::Dummy(ImVec2(lineH, lineH)); // reserve the glyph's space on the line
		}
	}

	ImGui::End();
}

static bool CursorIsVisible(); // defined below (used for gauge placement)

// A stylized digital battery gauge near the bottom of the screen.
// Two data sources:
//  - LIVE CHARGE (preferred): the game stores flashlight charge in percent-units
//    (sk_max_aa_batteries 1000 = "10 x 100", draining 1 per 2 s while on), but
//    the offset of that value inside the player entity is build-specific. Once
//    found (developer mode / Cheat Engine) and set in [inventory]
//    flashlight_charge_offset, the gauge tracks the real drain in real time.
//  - COUNT (fallback): without the offset, only the battery COUNT is readable;
//    the gauge then appears for a few seconds when the count changes
//    (pickup / battery consumed).
static void RenderFlashlightGauge() {
	if (!overlay::flashGauge) {
		return;
	}
	const bool live = (mod::inventory::flashlightChargeCounter != nullptr);
	int val = 0;
	if (live) {
		val = *mod::inventory::flashlightChargeCounter;
		if (val < 0 || val > 100000) return; // offset is wrong - don't render garbage
	} else if (mod::inventory::flashlightBatteriesCounter != nullptr) {
		val = *mod::inventory::flashlightBatteriesCounter;
	} else {
		return;
	}

	static int lastVal = INT_MIN;
	static double showUntil = 0.0;
	const double now = ImGui::GetTime();
	// Live charge: any decrease = draining right now. Count: any change is news.
	if (lastVal != INT_MIN && (live ? (val < lastVal) : (val != lastVal))) {
		showUntil = now + static_cast<double>(overlay::flashGaugeSeconds);
	}
	lastVal = val;
	// While overlays are unlocked (F11) the gauge stays visible so it can be
	// dragged into place with the mouse.
	const bool placing = !overlay::locked && CursorIsVisible();
	double remain = showUntil - now;
	if (placing && remain < 0.25) remain = 0.25;
	if (remain <= 0.0) {
		return;
	}

	// Smooth fade-out over the last flashGaugeFade seconds of the linger.
	const float fade = (overlay::flashGaugeFade > 0.01f) ? overlay::flashGaugeFade : 0.01f;
	float alpha = 1.0f;
	if (remain < fade) alpha = static_cast<float>(remain / fade);
	auto A = [alpha](ImU32 c) -> ImU32 {
		unsigned a = (c >> 24) & 0xFF;
		a = static_cast<unsigned>(a * alpha);
		return (c & 0x00FFFFFF) | (a << 24);
	};

	// Sensible scale: explicit config wins; otherwise the live charge is percent
	// of the CURRENT battery (0..100, confirmed at offset 0x184C) and a count
	// tops out around 10.
	int mx = overlay::flashGaugeMax;
	if (mx <= 0) mx = live ? 100 : 10;
	float pct = static_cast<float>(val) / static_cast<float>(mx);
	if (pct < 0.0f) pct = 0.0f;
	if (pct > 1.0f) pct = 1.0f;

	const bool customSkin = (overlay::flashGaugeSkin == 2);
	const ImU32 lvl = customSkin
		? ImGui::GetColorU32((pct > 0.5f) ? overlay::gaugeCustom[3] : (pct > 0.2f) ? overlay::gaugeCustom[4] : overlay::gaugeCustom[5])
		: (pct > 0.5f) ? IM_COL32(120, 220, 120, 255)
		: (pct > 0.2f) ? IM_COL32(235, 200, 90, 255)
		: IM_COL32(235, 90, 80, 255);
	const ImU32 cBg    = customSkin ? ImGui::GetColorU32(overlay::gaugeCustom[0]) : IM_COL32(12, 14, 18, 205);
	const ImU32 cFrame = customSkin ? ImGui::GetColorU32(overlay::gaugeCustom[1]) : IM_COL32(70, 84, 110, 255);
	const ImU32 cLabel = customSkin ? ImGui::GetColorU32(overlay::gaugeCustom[2]) : IM_COL32(150, 170, 200, 255);

	const int clientW = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int clientH = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;

	const bool subtle = (overlay::flashGaugeSkin == 1);
	const float w = subtle ? 140.0f : 168.0f;
	const float h = subtle ? 10.0f : 38.0f;
	float x = (overlay::gaugeX >= 0.0f) ? overlay::gaugeX : clientW * 0.5f - w * 0.5f;
	float y = (overlay::gaugeY >= 0.0f) ? overlay::gaugeY : clientH - (subtle ? 96.0f : 112.0f);

	// Mouse drag while unlocked: grab anywhere on the gauge, release to save the
	// position into silta.ini ([inventory] gauge_x/gauge_y; -1 -1 = auto).
	if (placing) {
		ImGuiIO& io = ImGui::GetIO();
		const float padL = subtle ? 3.0f : 8.0f, padT = subtle ? 3.0f : 16.0f;
		const float padR = subtle ? 3.0f : 14.0f, padB = subtle ? 3.0f : 8.0f;
		static bool dragging = false;
		static ImVec2 grab(0, 0);
		const bool hover = io.MousePos.x >= x - padL && io.MousePos.x <= x + w + padR &&
			io.MousePos.y >= y - padT && io.MousePos.y <= y + h + padB;
		if (!dragging && hover && io.MouseDown[0] && !ImGui::IsAnyItemActive()) {
			dragging = true;
			grab = ImVec2(io.MousePos.x - x, io.MousePos.y - y);
		}
		if (dragging) {
			if (io.MouseDown[0]) {
				x = io.MousePos.x - grab.x;
				y = io.MousePos.y - grab.y;
				if (x < 0) x = 0; if (y < 0) y = 0;
				if (x > clientW - w) x = clientW - w;
				if (y > clientH - h) y = clientH - h;
				overlay::gaugeX = x;
				overlay::gaugeY = y;
			} else {
				dragging = false;
				overlay::SaveGaugePos(); // silent (logged); a toast here was inconsistent
				                         // with every other layout drag, which saves silently
			}
		}
	}

	ImDrawList* dl = ImGui::GetForegroundDrawList();

	if (!subtle) {
		// Instrument panel: frame, label, battery outline + terminal.
		dl->AddRectFilled(ImVec2(x - 8, y - 16), ImVec2(x + w + 14, y + h + 8), A(cBg), 4.0f);
		dl->AddRect(ImVec2(x - 8, y - 16), ImVec2(x + w + 14, y + h + 8), A(cFrame), 4.0f);
		dl->AddText(ImVec2(x - 2, y - 15), A(cLabel), "FLASHLIGHT");
		dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), A(IM_COL32(200, 210, 225, 255)), 3.0f, 0, 2.0f);
		dl->AddRectFilled(ImVec2(x + w, y + h * 0.3f), ImVec2(x + w + 7, y + h * 0.7f), A(IM_COL32(200, 210, 225, 255)));
	} else {
		// Subtle: a faint backing strip only.
		dl->AddRectFilled(ImVec2(x - 3, y - 3), ImVec2(x + w + 3, y + h + 3), A(IM_COL32(10, 12, 16, 110)), 3.0f);
	}

	// Segments
	const int N = 10;
	const float pad = subtle ? 2.0f : 3.0f;
	const float segW = (w - pad * (N + 1)) / N;
	const int lit = static_cast<int>(pct * N + 0.5f);
	const unsigned segAlpha = subtle ? 190u : 255u;
	for (int i = 0; i < N; ++i) {
		const float sx = x + pad + i * (segW + pad);
		ImU32 c = (i < lit) ? lvl : IM_COL32(40, 46, 56, subtle ? 140 : 255);
		if (i < lit) c = (c & 0x00FFFFFF) | (segAlpha << 24);
		dl->AddRectFilled(ImVec2(sx, y + pad), ImVec2(sx + segW, y + h - pad), A(c), 1.5f);
	}

	if (overlay::flashGaugeNumbers) {
		char pctStr[16];
		sprintf_s(pctStr, sizeof(pctStr), "%d%%", static_cast<int>(pct * 100.0f + 0.5f));
		const ImVec2 ts = ImGui::CalcTextSize(pctStr);
		if (subtle) {
			// Small % to the right of the slim bar (it's too thin to hold text).
			dl->AddText(ImVec2(x + w + 8.0f, y + h * 0.5f - ts.y * 0.5f), A(IM_COL32(210, 218, 230, 220)), pctStr);
		} else {
			dl->AddText(ImVec2(x + w * 0.5f - ts.x * 0.5f, y + h * 0.5f - ts.y * 0.5f), A(IM_COL32(20, 24, 30, 255)), pctStr);
		}

		// Spare batteries (live-charge mode only; in count mode the bar IS the count).
		if (live && mod::inventory::flashlightBatteriesCounter != nullptr) {
			char spares[16];
			sprintf_s(spares, sizeof(spares), "x%d", *mod::inventory::flashlightBatteriesCounter);
			const ImVec2 ss = ImGui::CalcTextSize(spares);
			if (subtle) {
				dl->AddText(ImVec2(x - ss.x - 8.0f, y + h * 0.5f - ss.y * 0.5f), A(IM_COL32(160, 170, 190, 200)), spares);
			} else {
				dl->AddText(ImVec2(x + w + 14.0f - ss.x - 4.0f, y - 15.0f), A(cLabel), spares);
			}
		}
	}
}

void overlay::LoadNotes() {
	g_NotesLoaded = true;
	std::ifstream f(kNotesFile, std::ios::binary);
	if (!f.is_open()) {
		return;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (content.size() >= sizeof(g_NotesBuffer)) {
		content.resize(sizeof(g_NotesBuffer) - 1);
	}
	memcpy(g_NotesBuffer, content.data(), content.size());
	g_NotesBuffer[content.size()] = '\0';
}

void overlay::SaveNotes() {
	std::ofstream f(kNotesFile, std::ios::binary | std::ios::trunc);
	if (f.is_open()) {
		f.write(g_NotesBuffer, static_cast<std::streamsize>(strlen(g_NotesBuffer)));
	}
}

// A free-form, resizable/movable scratchpad for jotting puzzle/secret/ARG notes.
// Text persists to disk; the window has its own title bar so it can be dragged,
// resized and closed independently of the locked HUD overlays. With the "pad"
// skin it's drawn as a yellow legal pad with ruled lines and a red margin.
// ---- Brad the duck Easter egg (Notes: type "quack") ----
// When "quack" appears, the notepad briefly shows this little duck, then the
// text you typed returns. The swap is done through the InputText callback
// because ImGui owns the buffer state while the field is focused.
namespace {
	const char* const kQuackDuck =
		"                     __\n"
		"                   <(o )___\n"
		"                    ( ._> /      Q U A C K.\n"
		"                     `---'  \n";
	char   g_QuackBackup[sizeof(g_NotesBuffer)] = { 0 };
	double g_QuackFlourishUntil = 0.0;
	bool   g_QuackHatched = false;
	int    g_QuackAction = 0; // 0 = none, 1 = show duck, 2 = restore text
}

static int NotesQuackCallback(ImGuiInputTextCallbackData* data) {
	if (g_QuackAction == 1) {
		data->DeleteChars(0, data->BufTextLen);
		data->InsertChars(0, kQuackDuck);
		g_QuackAction = 0;
	} else if (g_QuackAction == 2) {
		data->DeleteChars(0, data->BufTextLen);
		data->InsertChars(0, g_QuackBackup);
		g_QuackAction = 0;
	}
	return 0;
}

static void RenderNotes() {
	if (!g_NotesLoaded) {
		overlay::LoadNotes();
	}

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int clientHeight = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;
	const overlay::NotesSkin skin = overlay::notesSkin;

	ImGui::SetNextWindowSize(ImVec2(380.0f, 280.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) * 0.5f - 190.0f, static_cast<float>(clientHeight) * 0.5f - 140.0f), ImGuiCond_FirstUseEver);

	bool ruled = false;
	ImU32 paperCol = 0, ruleCol = 0, marginCol = 0;
	int pushed = 0;

	switch (skin) {
	case overlay::NotesSkin::Ncg:
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.96f, 0.96f, 0.93f, 1.00f)); // memo paper
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.12f, 0.20f, 1.00f));
		pushed = 3;
		break;
	case overlay::NotesSkin::Pad:
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.98f, 0.94f, 0.66f, 1.00f)); // legal-pad yellow
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.12f, 0.12f, 0.22f, 1.00f));
		pushed = 3;
		ruled = true; paperCol = IM_COL32(250, 240, 168, 255); ruleCol = IM_COL32(120, 150, 205, 150); marginCol = IM_COL32(210, 90, 90, 170);
		break;
	case overlay::NotesSkin::Geolog:
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.90f, 0.93f, 0.85f, 1.00f)); // log paper
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.12f, 0.18f, 0.12f, 1.00f));
		pushed = 3;
		ruled = true; paperCol = IM_COL32(228, 236, 214, 255); ruleCol = IM_COL32(120, 160, 120, 150); marginCol = IM_COL32(150, 120, 80, 170);
		break;
	case overlay::NotesSkin::Custom:
		ImGui::PushStyleColor(ImGuiCol_WindowBg, overlay::notesCustom[0]);
		ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, overlay::notesCustom[1]);
		pushed = 3;
		if (overlay::notesCustom[2].w > 0.004f) {
			ruled = true;
			paperCol = ImGui::GetColorU32(overlay::notesCustom[0]);
			ruleCol = ImGui::GetColorU32(overlay::notesCustom[2]);
			marginCol = (overlay::notesCustom[3].w > 0.004f)
				? ImGui::GetColorU32(overlay::notesCustom[3]) : ruleCol;
		}
		break;
	case overlay::NotesSkin::Plain:
	default:
		ImGui::SetNextWindowBgAlpha(overlay::backgroundAlpha < 0.55f ? 0.85f : overlay::backgroundAlpha);
		ImGui::PushStyleColor(ImGuiCol_Text, overlay::notesColor);
		pushed = 1;
		break;
	}

	bool open = true;
	ImGuiWindowFlags notesFlags = ImGuiWindowFlags_NoCollapse;
	if (!overlay::saveLayout) {
		notesFlags |= ImGuiWindowFlags_NoSavedSettings;
	}
	if (ImGui::Begin("Notes", &open, notesFlags)) {
		if (overlay::notesFontScale > 0.0f) {
			ImGui::SetWindowFontScale(overlay::notesFontScale);
		}

		// Letterhead for the themed skins.
		if (skin == overlay::NotesSkin::Ncg) {
			ImGui::TextColored(ImVec4(0.10f, 0.12f, 0.20f, 1.00f), "NATIONAL CONSULTING GROUP");
			ImGui::TextColored(ImVec4(0.32f, 0.34f, 0.42f, 1.00f), "TO: P. Lauwens    FROM: M. Siltanen");
			std::string re = "RE: Structural Survey";
			if (!overlay::locationName.empty()) {
				re += " - " + overlay::locationName;
			}
			ImGui::TextColored(ImVec4(0.32f, 0.34f, 0.42f, 1.00f), "%s", re.c_str());
			ImGui::Separator();
		} else if (skin == overlay::NotesSkin::Geolog) {
			ImGui::TextColored(ImVec4(0.12f, 0.30f, 0.12f, 1.00f), "GEOCACHE LOG");
			ImGui::TextColored(ImVec4(0.25f, 0.40f, 0.25f, 1.00f), "TFTC   -   TB OUT   -   BYOP");
			ImGui::Separator();
		}

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 p0 = ImGui::GetCursorScreenPos();
		const ImVec2 p1 = ImVec2(p0.x + avail.x, p0.y + avail.y);

		if (ruled) {
			ImDrawList* dl = ImGui::GetWindowDrawList();
			dl->AddRectFilled(p0, p1, paperCol);
			const float lh = ImGui::GetTextLineHeight();
			for (float y = p0.y + lh; y < p1.y; y += lh) {
				dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), ruleCol, 1.0f);
			}
			const float marginX = p0.x + 32.0f;
			dl->AddLine(ImVec2(marginX, p0.y), ImVec2(marginX, p1.y), marginCol, 1.0f);
		}

		ImGui::InputTextMultiline("##notes", g_NotesBuffer, sizeof(g_NotesBuffer), avail,
			ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways, NotesQuackCallback);

		// Easter egg: type "quack" in the notes -> Brad flourishes here for a
		// moment (the notepad briefly becomes a little duck) before your text
		// returns, and SILTA writes his colored portrait to brad.ans. Fires ONCE
		// per game run; the scan stops after it hatches. Quack.
		{
			const double now = ImGui::GetTime();
			if (g_QuackFlourishUntil > 0.0) {
				if (now >= g_QuackFlourishUntil) {
					memcpy(g_NotesBuffer, g_QuackBackup, sizeof(g_NotesBuffer));
					g_QuackAction = 2; // also restore via the input callback (field is focused)
					g_QuackFlourishUntil = 0.0;
				}
			} else if (!g_QuackHatched) {
				bool present = false;
				for (const char* p = g_NotesBuffer; *p; ++p) {
					if ((p[0] == 'q' || p[0] == 'Q') &&
						(p[1] == 'u' || p[1] == 'U') &&
						(p[2] == 'a' || p[2] == 'A') &&
						(p[3] == 'c' || p[3] == 'C') &&
						(p[4] == 'k' || p[4] == 'K')) { present = true; break; }
				}
				if (present) {
					memcpy(g_QuackBackup, g_NotesBuffer, sizeof(g_QuackBackup));
					strncpy_s(g_NotesBuffer, sizeof(g_NotesBuffer), kQuackDuck, _TRUNCATE);
					g_QuackAction = 1; // show the duck via the input callback
					g_QuackFlourishUntil = now + 0.6; // split-second

					FILE* qf = nullptr;
					if (fopen_s(&qf, "brad.ans", "wb") == 0 && qf) {
						for (const char* line : kBradAnsi) { fputs(line, qf); fputc('\n', qf); }
						fclose(qf);
					}
					overlay::ShowToast("Quack! Brad is loose", 6.0f);
					LogI("quack:       warning:  Brad is loose");
					g_QuackHatched = true; // once per run; no more scanning
				}
			}
		}
	}
	ImGui::End();

	ImGui::PopStyleColor(pushed);

	if (!open) {
		overlay::notesShown = false;
		overlay::SaveNotes();
	}
}

static bool CursorIsVisible();

bool overlay::WantsToCapture(UINT uMsg) {
	if (!overlay::imGuiInitialized) {
		return false;
	}
	const bool typingWindowOpen =
		(overlay::notesEnabled && (overlay::notesShown || (overlay::notesAutoOpen && CursorIsVisible()))) ||
		(overlay::sketchEnabled && overlay::sketchShown) ||
		(overlay::calcEnabled && overlay::calcShown) ||
		(overlay::endingEnabled && overlay::endingShown) ||
		(overlay::contactEnabled && overlay::contactShown);
	if (!typingWindowOpen) {
		return false;
	}
	const ImGuiIO& io = ImGui::GetIO();
	if (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST) {
		return io.WantCaptureKeyboard;
	}
	if (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) {
		return io.WantCaptureMouse;
	}
	return false;
}

// Human-readable name for a Win32 virtual-key code (e.g. 0x75 -> "F6").
static std::string KeyName(int vk) {
	if (vk == 0) {
		return "";
	}

	bool extended = false;
	switch (vk) {
	case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
	case VK_PRIOR:  case VK_NEXT:   case VK_LEFT: case VK_RIGHT:
	case VK_UP:     case VK_DOWN:   case VK_NUMLOCK: case VK_RCONTROL: case VK_RMENU:
		extended = true;
		break;
	default:
		break;
	}

	const UINT sc = MapVirtualKey(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
	LONG lparam = static_cast<LONG>(sc << 16);
	if (extended) {
		lparam |= (1 << 24);
	}

	char name[64] = { 0 };
	if (GetKeyNameTextA(lparam, name, sizeof(name)) > 0) {
		return name;
	}

	char buf[16];
	sprintf_s(buf, sizeof(buf), "VK %d", vk);
	return buf;
}

// True when the OS cursor is visible — a good proxy for "phone/menu is out".
static bool CursorIsVisible() {
	CURSORINFO ci = { sizeof(CURSORINFO) };
	if (GetCursorInfo(&ci)) {
		return (ci.flags & CURSOR_SHOWING) != 0;
	}
	return false;
}

// Top-centre tip bar listing every active hotkey; shown only while the cursor is
// out (e.g. phone/menu), so it stays out of the way during normal play.
static void RenderHotkeyTips() {
	static long long shownSince = 0;

	if (!overlay::hintsEnabled || !CursorIsVisible()) {
		shownSince = 0; // reset so the timer restarts next time the cursor comes out
		return;
	}

	const long long now = CurrentTimeMillis();
	if (shownSince == 0) {
		shownSince = now;
	}

	// Optional fade-out after a delay.
	float alpha = 1.0f;
	if (overlay::tipFade && overlay::tipFadeSeconds > 0.0f) {
		const long long fadeStartMs = static_cast<long long>(overlay::tipFadeSeconds * 1000.0f);
		const long long fadeDurMs = 800;
		const long long elapsed = now - shownSince;
		if (elapsed >= fadeStartMs + fadeDurMs) {
			return; // fully faded; stays hidden until the cursor is hidden and shown again
		}
		if (elapsed > fadeStartMs) {
			alpha = 1.0f - static_cast<float>(elapsed - fadeStartMs) / static_cast<float>(fadeDurMs);
		}
	}

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const overlay::Hotkeys& hk = overlay::hotkeys;

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::SetNextWindowBgAlpha((overlay::backgroundAlpha < 0.55f ? 0.85f : overlay::backgroundAlpha) * alpha);
	// Starts top-centre, but draggable (only seeded once), so you can reposition it.
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) * 0.5f, 8.0f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
	ImGuiWindowFlags tipFlags =
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_AlwaysAutoResize;
	if (!overlay::saveLayout) {
		tipFlags |= ImGuiWindowFlags_NoSavedSettings;
	}
	ImGui::Begin("Hotkeys", nullptr, tipFlags);

	ImGui::TextColored(overlay::titleColor, "HOTKEYS");
	ImGui::Separator();

	// Collect rows and sort by key code so the list reads F1, F2, F3... instead
	// of feature-addition order.
	std::vector<std::pair<int, std::string>> rows;
	auto row = [&](int vk, const std::string& desc) {
		if (vk != 0) rows.emplace_back(vk, desc);
	};

	row(Base::Data::Keys::ToggleMenu, "Show / hide overlay");
	row(hk.reloadConfig, "Reload config");
	row(hk.toggleCounters, "Toggle counters");
	row(hk.toggleInventory, "Toggle inventory");
	row(hk.cycleCountersCorner, "Cycle counters corner");
	row(hk.cycleInventoryCorner, "Cycle inventory corner");
	row(hk.toggleLock, overlay::locked ? "Unlock (drag overlays)" : "Lock (snap to corners)");
	row(hk.resetPosition, "Reset positions");
	row(hk.toggleNotes, "Notes scratchpad");
	row(hk.toggleSketch, "Sketchbook");
	row(hk.toggleCalculator, "Field calculator");
	row(hk.toggleEnding, "Study outlook");
	row(hk.toggleContact, "Contact sheet");

	std::sort(rows.begin(), rows.end(),
		[](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) { return a.first < b.first; });
	for (const auto& r : rows) {
		ImGui::TextColored(overlay::tipKeyColor, "%s", KeyName(r.first).c_str());
		ImGui::SameLine(90.0f);
		ImGui::TextColored(overlay::tipTextColor, "%s", r.second.c_str());
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

// Small unobtrusive corner tag: "SILTA v1.0" in plain white, with a soft shadow
// so it reads on any scene. Uses the foreground draw list (no window).
static void RenderWatermark() {
	if (!overlay::watermark || !overlay::inMenu) return;
	const int cw = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int chh = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;
	char tag[128];
	if (!overlay::watermarkText.empty()) {
		strncpy_s(tag, sizeof(tag), overlay::watermarkText.c_str(), _TRUNCATE);
	} else {
		sprintf_s(tag, sizeof(tag), "%s v%s", overlay::kModName, overlay::kVersion);
	}
	const ImVec2 sz = ImGui::CalcTextSize(tag);
	const float pad = 8.0f;
	ImVec2 p;
	switch (overlay::watermarkCorner) {
	case 0:  p = ImVec2(pad, pad); break;                                   // TL
	case 2:  p = ImVec2(pad, chh - sz.y - pad); break;                      // BL
	case 3:  p = ImVec2(cw - sz.x - pad, chh - sz.y - pad); break;          // BR
	default: p = ImVec2(cw - sz.x - pad, pad); break;                       // TR
	}
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	dl->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 160), tag);    // shadow
	dl->AddText(p, IM_COL32(255, 255, 255, 210), tag);                      // white
}

// Brief on-screen toast (camera save confirmation, etc.).
static void RenderToast() {
	if (g_ToastText.empty() || g_ToastDurationMs <= 0) {
		return;
	}
	const long long elapsed = CurrentTimeMillis() - g_ToastShownMs;
	if (elapsed >= g_ToastDurationMs) {
		return;
	}

	// Quick fade over the last part of the (short) lifetime.
	float alpha = 1.0f;
	const long long fadeMs = (g_ToastDurationMs < 500) ? g_ToastDurationMs / 2 : 250;
	if (fadeMs > 0 && elapsed > g_ToastDurationMs - fadeMs) {
		alpha = static_cast<float>(g_ToastDurationMs - elapsed) / static_cast<float>(fadeMs);
	}

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int clientHeight = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;

	// Match the counters/inventory HUD look (squared, thin border, dark navy).
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.45f, 0.55f, 0.75f, 0.9f));
	ImGui::SetNextWindowBgAlpha(0.78f * alpha);
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) * 0.5f, static_cast<float>(clientHeight) - 60.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
	ImGui::Begin("##toast", nullptr,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize);
	ImGui::TextColored(overlay::fontColor, "%s", g_ToastText.c_str());
	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(3);
}

// End-of-game N.C.G. report card: a larger, centered, themed popup, headed by
// a font-scaled SILTA wordmark (the mod ships no game artwork).
static void RenderReport() {
	if (g_ReportBody.empty() || g_ReportDurationMs <= 0) {
		return;
	}
	const long long elapsed = CurrentTimeMillis() - g_ReportShownMs;
	if (elapsed >= g_ReportDurationMs) {
		return;
	}
	float alpha = 1.0f;
	const long long fadeMs = 600;
	if (elapsed > g_ReportDurationMs - fadeMs) {
		alpha = static_cast<float>(g_ReportDurationMs - elapsed) / static_cast<float>(fadeMs);
	}
	if (elapsed < fadeMs) {
		alpha = static_cast<float>(elapsed) / static_cast<float>(fadeMs); // fade in
	}

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	const int clientHeight = Base::Data::HACK_clientRect.bottom - Base::Data::HACK_clientRect.top;

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.62f, 0.78f, 0.95f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.10f, 0.14f, 0.94f));
	ImGui::SetNextWindowBgAlpha(0.94f * alpha);
	ImGui::SetNextWindowPos(ImVec2(clientWidth * 0.5f, clientHeight * 0.42f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Always);
	ImGui::Begin("##report", nullptr,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);

	// SILTA wordmark header (font-scaled text; the mod ships no game artwork).
	ImGui::SetWindowFontScale(1.9f);
	const char* wm = "S I L T A";
	const float wmw = ImGui::CalcTextSize(wm).x;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - wmw) * 0.5f);
	ImGui::TextColored(ImVec4(0.85f, 0.89f, 0.97f, 1.0f), "%s", wm);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::Separator();

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.72f, 0.92f, 1.0f));
	ImGui::TextWrapped("%s", g_ReportTitle.c_str());
	ImGui::PopStyleColor();
	ImGui::Separator();
	ImGui::TextColored(overlay::fontColor, "%s", g_ReportBody.c_str());
	ImGui::Spacing();
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.64f, 0.70f, 1.0f));
	ImGui::TextWrapped("Filed to NCG_Survey_Report.txt in the game folder.");
	ImGui::PopStyleColor();

	ImGui::End();
	ImGui::PopStyleColor(2);
	ImGui::PopStyleVar(3);
}

// Progress popup: per-category % and the next achievement milestone. Appears
// briefly while the cursor is out (phone), like a notification.
static void RenderProgress() {
	static long long shownSince = 0;

	if (!overlay::showProgress || !CursorIsVisible()) {
		shownSince = 0;
		return;
	}
	const long long now = CurrentTimeMillis();
	if (shownSince == 0) shownSince = now;

	float alpha = 1.0f;
	const long long fadeStartMs = static_cast<long long>(overlay::progressSeconds * 1000.0f);
	const long long fadeDurMs = 700;
	const long long elapsed = now - shownSince;
	if (overlay::progressSeconds > 0.0f) {
		if (elapsed >= fadeStartMs + fadeDurMs) return; // shown only briefly
		if (elapsed > fadeStartMs) alpha = 1.0f - static_cast<float>(elapsed - fadeStartMs) / static_cast<float>(fadeDurMs);
	}

	// Achievement names per (act, category). Index [act-1][category].
	static const char* const kAch[3][overlay::CategoryCount] = {
		{ "No Stone Unturned", "The Conspiracy Unfolds", "Structural Analyst Extr.", "TFTC",   "Restoring the Flow" },
		{ "Photographer Extr.", "Sign of an Open Eye",   "Above and Beyond",        "TB OUT", "" },
		{ "Night Shift",        "Truthseeker",           "Just Leave It to Me",     "BYOP",   "" },
	};
	static const char* const kLabel[overlay::CategoryCount] = {
		"Photos", "Corruption", "Repairs", "Geocaches", "Flow meters"
	};
	const int act = (overlay::currentAct >= 1 && overlay::currentAct <= 3) ? overlay::currentAct : 1;

	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::SetNextWindowBgAlpha((overlay::backgroundAlpha < 0.6f ? 0.88f : overlay::backgroundAlpha) * alpha);
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) * 0.5f, 40.0f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.0f));
	ImGuiWindowFlags pf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize;
	if (!overlay::saveLayout) pf |= ImGuiWindowFlags_NoSavedSettings;
	ImGui::Begin("##progress", nullptr, pf);

	ImGui::TextColored(overlay::titleColor, "SURVEY PROGRESS  -  ACT %d", act);
	ImGui::Separator();

	for (int i = 0; i < overlay::CategoryCount; ++i) {
		const int cur = overlay::catCurrent[i];
		const int mx = overlay::catMax[i];
		if (mx <= 0) continue;
		const int pct = static_cast<int>((100.0f * cur / mx) + 0.5f);

		ImGui::TextColored(overlay::categoryColor[i], "%-11s", kLabel[i]);
		ImGui::SameLine(110.0f);
		ImGui::TextColored(overlay::fontColor, "%d/%d (%d%%)", cur, mx, pct);

		const char* ach = kAch[act - 1][i];
		ImGui::SameLine(220.0f);
		if (pct >= 90 || (i == 3 && act == 2 && pct >= 100) || (i == 4 && pct >= 100)) {
			ImGui::TextColored(overlay::fontColorMax, "complete");
		} else if (ach && ach[0]) {
			ImGui::TextColored(overlay::titleColor, "-> %s", ach);
		} else {
			ImGui::TextColored(overlay::titleColor, " ");
		}
	}

	ImGui::End();
	ImGui::PopStyleVar();
}

// ============================================================================
//  N.C.G. field calculator - an immediate-execution calculator (4-function or
//  scientific) plus a few structural-analyst helpers (unit conversions, flow,
//  stress, slope). Pure logic; styled to match the survey aesthetic.
// ============================================================================
namespace {
	struct Calc {
		double reg = 0.0; char op = 0; bool fresh = true; std::string buf = "0"; bool err = false;
		static std::string fmt(double v) {
			if (v != v || v > 1e300 || v < -1e300) return "ERR";
			char t[64]; sprintf_s(t, sizeof(t), "%.10g", v); return t;
		}
		double cur() { return atof(buf.c_str()); }
		double apply(double a, double b, char o) {
			switch (o) { case '+': return a + b; case '-': return a - b; case '*': return a * b;
				case '/': if (b == 0.0) { err = true; return 0.0; } return a / b; }
			return b;
		}
		void digit(char c) {
			if (err) clear();
			if (fresh) { buf = (c == '.') ? std::string("0.") : std::string(1, c); fresh = false; }
			else { if (c == '.' && buf.find('.') != std::string::npos) return; buf += c; }
		}
		void setop(char o) { if (err) return; if (op) { reg = apply(reg, cur(), op); } else reg = cur(); buf = fmt(reg); op = o; fresh = true; }
		void equals() { if (err) return; if (op) { reg = apply(reg, cur(), op); buf = fmt(reg); op = 0; fresh = true; } }
		void unary(double v) { if (err) return; buf = fmt(v); fresh = true; }
		void neg() { if (!buf.empty() && buf[0] == '-') buf = buf.substr(1); else if (buf != "0") buf = "-" + buf; }
		void pct() { buf = fmt(cur() / 100.0); fresh = true; }
		void clear() { reg = 0; op = 0; fresh = true; buf = "0"; err = false; }
		std::string disp() { return err ? std::string("ERR") : buf; }
	};
	Calc g_Calc;
}

static void RenderCalculator() {
	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	ImGui::SetNextWindowSize(ImVec2(300.0f, 430.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) - 320.0f, 380.0f), ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!overlay::saveLayout) flags |= ImGuiWindowFlags_NoSavedSettings;

	// Skins: 0 = N.C.G. instrument, 1 = Osmo Olut, 2 = custom (ini colors, live
	// editable in the Style tab).
	const bool osmo = (overlay::calcSkin == 1);
	const bool custom = (overlay::calcSkin == 2);
	const ImVec4 skBg     = custom ? overlay::calcCustom[0] : osmo ? ImVec4(0.16f, 0.11f, 0.05f, 0.96f) : ImVec4(0.11f, 0.12f, 0.13f, 0.96f);
	const ImVec4 skBtn    = custom ? overlay::calcCustom[1] : osmo ? ImVec4(0.34f, 0.24f, 0.09f, 1.0f)  : ImVec4(0.20f, 0.22f, 0.25f, 1.0f);
	const ImVec4 skBtnH   = custom ? overlay::calcCustom[2] : osmo ? ImVec4(0.48f, 0.34f, 0.13f, 1.0f)  : ImVec4(0.30f, 0.33f, 0.37f, 1.0f);
	const ImVec4 skBtnA   = custom ? overlay::calcCustom[3] : osmo ? ImVec4(0.72f, 0.52f, 0.16f, 1.0f)  : ImVec4(0.40f, 0.34f, 0.12f, 1.0f);
	const ImVec4 skAccent = custom ? overlay::calcCustom[4] : osmo ? ImVec4(0.96f, 0.82f, 0.46f, 1.0f)  : ImVec4(0.55f, 0.62f, 0.75f, 1.0f);
	const ImVec4 skDisp   = custom ? overlay::calcCustom[5] : osmo ? ImVec4(1.00f, 0.84f, 0.38f, 1.0f)  : ImVec4(1.00f, 0.74f, 0.20f, 1.0f);
	const ImVec4 skDispBg = custom ? overlay::calcCustom[6] : osmo ? ImVec4(0.09f, 0.06f, 0.02f, 1.0f)  : ImVec4(0.05f, 0.06f, 0.05f, 1.0f);
	const char* skTitle   = custom ? "N.C.G. FIELD CALCULATOR" : osmo ? "OSMO OLUT  -  FIELD UNIT" : "N.C.G. FIELD CALCULATOR";

	ImGui::PushStyleColor(ImGuiCol_WindowBg, skBg);
	ImGui::PushStyleColor(ImGuiCol_Button, skBtn);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, skBtnH);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, skBtnA);

	bool open = true;
	if (ImGui::Begin("N.C.G. Field Calculator", &open, flags)) {
		ImGui::TextColored(skAccent, "%s", skTitle);

		// Mode selector.
		auto modeBtn = [&](const char* label, overlay::CalcMode m) {
			const bool on = (overlay::calcMode == m);
			if (on) ImGui::PushStyleColor(ImGuiCol_Button, skBtnA);
			if (ImGui::Button(label)) overlay::calcMode = m;
			if (on) ImGui::PopStyleColor();
		};
		modeBtn("Basic", overlay::CalcMode::Basic); ImGui::SameLine();
		modeBtn("Sci", overlay::CalcMode::Scientific); ImGui::SameLine();
		modeBtn("Prog", overlay::CalcMode::Programmer); ImGui::SameLine();
		modeBtn("Text", overlay::CalcMode::Text); ImGui::SameLine();
		modeBtn("Style", overlay::CalcMode::Style);
		ImGui::Separator();

		const bool sci = (overlay::calcMode == overlay::CalcMode::Scientific);

		if (overlay::calcMode == overlay::CalcMode::Basic || sci) {
		// Amber 7-seg-ish display, right-aligned.
		ImGui::PushStyleColor(ImGuiCol_ChildBg, skDispBg);
		ImGui::BeginChild("##disp", ImVec2(0, 40), true);
		const std::string d = g_Calc.disp();
		const float tw = ImGui::CalcTextSize(d.c_str()).x;
		ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - tw + ImGui::GetCursorPosX());
		ImGui::TextColored(skDisp, "%s", d.c_str());
		ImGui::EndChild();
		ImGui::PopStyleColor();

		const ImVec2 bsz(56, 34);
		auto btn = [&](const char* label, const ImVec2& sz) { return ImGui::Button(label, sz); };

		// Scientific row(s).
		if (sci) {
			const ImVec2 ssz(46, 30);
			if (btn("sin", ssz)) g_Calc.unary(sin(g_Calc.cur())); ImGui::SameLine();
			if (btn("cos", ssz)) g_Calc.unary(cos(g_Calc.cur())); ImGui::SameLine();
			if (btn("tan", ssz)) g_Calc.unary(tan(g_Calc.cur())); ImGui::SameLine();
			if (btn("v\xC2\xAF", ssz)) g_Calc.unary(sqrt(g_Calc.cur())); ImGui::SameLine();
			if (btn("x^2", ssz)) g_Calc.unary(g_Calc.cur() * g_Calc.cur());
			if (btn("ln", ssz)) g_Calc.unary(log(g_Calc.cur())); ImGui::SameLine();
			if (btn("log", ssz)) g_Calc.unary(log10(g_Calc.cur())); ImGui::SameLine();
			if (btn("1/x", ssz)) g_Calc.unary(g_Calc.cur() == 0 ? (g_Calc.err = true, 0.0) : 1.0 / g_Calc.cur()); ImGui::SameLine();
			if (btn("pi", ssz)) { g_Calc.buf = Calc::fmt(3.14159265358979323846); g_Calc.fresh = true; } ImGui::SameLine();
			if (btn("e", ssz)) { g_Calc.buf = Calc::fmt(2.71828182845904523536); g_Calc.fresh = true; }
			ImGui::Separator();
		}

		// Standard 4x5 keypad.
		struct Key { const char* l; char kind; char val; }; // kind: d=digit . o=op = u
		auto row = [&](std::initializer_list<Key> keys) {
			int i = 0;
			for (const Key& k : keys) {
				if (i++) ImGui::SameLine();
				if (btn(k.l, bsz)) {
					switch (k.kind) {
					case 'd': g_Calc.digit(k.val); break;
					case 'o': g_Calc.setop(k.val); break;
					case '=': g_Calc.equals(); break;
					case 'c': g_Calc.clear(); break;
					case 'n': g_Calc.neg(); break;
					case 'p': g_Calc.pct(); break;
					}
				}
			}
		};
		row({ {"C",'c',0}, {"+/-",'n',0}, {"%",'p',0}, {"/",'o','/'} });
		row({ {"7",'d','7'}, {"8",'d','8'}, {"9",'d','9'}, {"x",'o','*'} });
		row({ {"4",'d','4'}, {"5",'d','5'}, {"6",'d','6'}, {"-",'o','-'} });
		row({ {"1",'d','1'}, {"2",'d','2'}, {"3",'d','3'}, {"+",'o','+'} });
		row({ {"0",'d','0'}, {".",'d','.'}, {"=",'=',0} });

		// Structural-analyst helpers.
		if (ImGui::CollapsingHeader("Field helpers")) {
			ImGui::TextColored(overlay::titleColor, "Unit conversions");
			static float m_val = 1.0f, mm_val = 1.0f, kpa_val = 1.0f, kg_val = 1.0f, c_val = 20.0f;
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("m", &m_val);
			ImGui::SameLine(); ImGui::Text("= %.3f ft  /  %.3f in", m_val * 3.280839895f, m_val * 39.37007874f);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("mm", &mm_val);
			ImGui::SameLine(); ImGui::Text("= %.4f in", mm_val / 25.4f);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("kPa", &kpa_val);
			ImGui::SameLine(); ImGui::Text("= %.4f psi", kpa_val * 0.1450377f);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("kg", &kg_val);
			ImGui::SameLine(); ImGui::Text("= %.4f lb", kg_val * 2.2046226f);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("\xC2\xB0""C", &c_val);
			ImGui::SameLine(); ImGui::Text("= %.2f \xC2\xB0""F", c_val * 9.0f / 5.0f + 32.0f);

			ImGui::Separator();
			ImGui::TextColored(overlay::titleColor, "Flow  Q = V x A");
			static float vel = 1.0f, area = 1.0f;
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("V m/s", &vel);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("A m\xC2\xB2", &area);
			ImGui::Text("Q = %.4f m\xC2\xB3/s  (%.1f L/s)", vel * area, vel * area * 1000.0f);

			ImGui::Separator();
			ImGui::TextColored(overlay::titleColor, "Stress  \xCF\x83 = F / A");
			static float force = 1000.0f, csa = 100.0f;
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("F N", &force);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("A mm\xC2\xB2", &csa);
			ImGui::Text("\xCF\x83 = %.3f MPa", csa != 0.0f ? force / csa : 0.0f);

			ImGui::Separator();
			ImGui::TextColored(overlay::titleColor, "Slope");
			static float rise = 1.0f, run = 20.0f;
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("rise", &rise);
			ImGui::SetNextItemWidth(90); ImGui::InputFloat("run", &run);
			ImGui::Text("= %.2f%%  (%.2f\xC2\xB0)", run != 0.0f ? rise / run * 100.0f : 0.0f,
				run != 0.0f ? atanf(rise / run) * 57.2957795f : 0.0f);
		}
		} // end Basic / Scientific
		else if (overlay::calcMode == overlay::CalcMode::Programmer) {
			static char aBuf[40] = "0", bBuf[40] = "0";
			static int base = 10, shift = 1;
			ImGui::TextColored(overlay::titleColor, "Input base");
			ImGui::RadioButton("DEC", &base, 10); ImGui::SameLine();
			ImGui::RadioButton("HEX", &base, 16); ImGui::SameLine();
			ImGui::RadioButton("BIN", &base, 2);  ImGui::SameLine();
			ImGui::RadioButton("OCT", &base, 8);
			ImGui::SetNextItemWidth(200); ImGui::InputText("A", aBuf, sizeof(aBuf));
			ImGui::SetNextItemWidth(200); ImGui::InputText("B", bBuf, sizeof(bBuf));

			const long long a = strtoll(aBuf, nullptr, base);
			const long long b = strtoll(bBuf, nullptr, base);

			auto toBin = [](long long v) {
				unsigned long long u = static_cast<unsigned long long>(v);
				if (u == 0) return std::string("0");
				std::string s;
				bool lead = true;
				for (int i = 63; i >= 0; --i) {
					bool bit = (u >> i) & 1ULL;
					if (bit) lead = false;
					if (!lead) s += bit ? '1' : '0';
				}
				return s;
			};
			char hx[24], oc[32];
			sprintf_s(hx, sizeof(hx), "%llX", static_cast<unsigned long long>(a));
			sprintf_s(oc, sizeof(oc), "%llo", static_cast<unsigned long long>(a));
			ImGui::Separator();
			ImGui::TextColored(overlay::titleColor, "A in all bases");
			ImGui::Text("DEC  %lld", a);
			ImGui::Text("HEX  %s", hx);
			ImGui::TextWrapped("BIN  %s", toBin(a).c_str());
			ImGui::Text("OCT  %s", oc);
			ImGui::Separator();
			ImGui::TextColored(overlay::titleColor, "Bitwise (A op B)");
			ImGui::Text("A & B = %lld", a & b);
			ImGui::Text("A | B = %lld", a | b);
			ImGui::Text("A ^ B = %lld", a ^ b);
			ImGui::Text("~A    = %lld", ~a);
			ImGui::SetNextItemWidth(90); ImGui::InputInt("shift", &shift);
			if (shift < 0) shift = 0; if (shift > 63) shift = 63;
			ImGui::Text("A << %d = %lld", shift, a << shift);
			ImGui::Text("A >> %d = %lld", shift, a >> shift);
		}
		else if (overlay::calcMode == overlay::CalcMode::Style) {
			// Live skin editor. Edits apply instantly; Save writes them (and
			// skin=custom) to silta.ini so they survive restarts.
			bool useCustom = custom;
			if (ImGui::Checkbox("Use custom skin", &useCustom)) {
				overlay::calcSkin = useCustom ? 2 : 0;
			}
			ImGui::Separator();
			static const char* kNames[7] = {
				"Window", "Button", "Button hover", "Button active",
				"Accent", "Display text", "Display bg" };
			for (int i = 0; i < 7; ++i) {
				if (ImGui::ColorEdit3(kNames[i], reinterpret_cast<float*>(&overlay::calcCustom[i]),
					ImGuiColorEditFlags_NoInputs)) {
					overlay::calcSkin = 2; // editing implies you want to see it
				}
			}
			ImGui::Spacing();
			if (ImGui::Button("Save to silta.ini")) {
				overlay::SaveCalcCustomColors();
				overlay::ShowToast("Custom calculator skin saved to silta.ini", 3.0f);
			}
			ImGui::SameLine();
			ImGui::TextColored(overlay::titleColor, "(F6 reload keeps it)");
		}
		else if (overlay::calcMode == overlay::CalcMode::Text) {
			// Folded-in ARG decoder: hex<->ASCII and Caesar shift.
			static char inBuf[512] = { 0 };
			static int caesar = 13;
			ImGui::TextColored(overlay::titleColor, "Text / cipher input");
			ImGui::InputTextMultiline("##txtin", inBuf, sizeof(inBuf), ImVec2(-1.0f, 56.0f));
			const std::string in = inBuf;

			std::string hexToAscii, asciiToHex, caesarOut, bin;
			{
				std::string hex;
				for (char c : in) if (isxdigit(static_cast<unsigned char>(c))) hex += c;
				for (size_t i = 0; i + 1 < hex.size(); i += 2) {
					int v = static_cast<int>(strtol(hex.substr(i, 2).c_str(), nullptr, 16));
					hexToAscii += (v >= 32 && v < 127) ? static_cast<char>(v) : '.';
				}
			}
			char tmp[8];
			for (char c : in) { sprintf_s(tmp, sizeof(tmp), "%02X ", static_cast<unsigned char>(c)); asciiToHex += tmp; }
			for (char c : in) {
				if (c >= 'a' && c <= 'z') caesarOut += static_cast<char>('a' + (((c - 'a') + caesar) % 26 + 26) % 26);
				else if (c >= 'A' && c <= 'Z') caesarOut += static_cast<char>('A' + (((c - 'A') + caesar) % 26 + 26) % 26);
				else caesarOut += c;
			}
			for (char c : in) { for (int i = 7; i >= 0; --i) bin += ((static_cast<unsigned char>(c) >> i) & 1) ? '1' : '0'; bin += ' '; }

			ImGui::Separator();
			ImGui::TextColored(overlay::titleColor, "Hex -> ASCII:"); ImGui::TextWrapped("%s", hexToAscii.c_str());
			ImGui::TextColored(overlay::titleColor, "ASCII -> Hex:"); ImGui::TextWrapped("%s", asciiToHex.c_str());
			ImGui::TextColored(overlay::titleColor, "ASCII -> Bin:"); ImGui::TextWrapped("%s", bin.c_str());
			ImGui::Separator();
			ImGui::SetNextItemWidth(160); ImGui::SliderInt("Caesar shift", &caesar, 0, 25);
			ImGui::TextColored(overlay::titleColor, "Caesar:"); ImGui::TextWrapped("%s", caesarOut.c_str());
		}
	}
	ImGui::End();
	ImGui::PopStyleColor(4);

	if (!open) overlay::calcShown = false;
}

// ============================================================================
//  N.C.G. study outlook - turns the live photo (Defects) and Corruption tallies
//  into the same thresholds the game uses to pick an ending: >=50% of each gets
//  the "study" (good) ending; 90% corruption adds the Raven Research contact.
//  The game scores per Act; these are overall figures, so treat them as a guide.
// ============================================================================
static void RenderEndingOutlook() {
	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	ImGui::SetNextWindowSize(ImVec2(330.0f, 250.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) - 350.0f, 70.0f), ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!overlay::saveLayout) flags |= ImGuiWindowFlags_NoSavedSettings;

	const int photoCur = mod::counters::GetCategoryCurrent(0), photoMax = mod::counters::GetCategoryMax(0);
	const int corrCur = mod::counters::GetCategoryCurrent(1), corrMax = mod::counters::GetCategoryMax(1);
	const float photoPct = photoMax > 0 ? static_cast<float>(photoCur) / photoMax : 0.0f;
	const float corrPct = corrMax > 0 ? static_cast<float>(corrCur) / corrMax : 0.0f;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.11f, 0.12f, 0.14f, 0.96f));
	bool open = true;
	if (ImGui::Begin("N.C.G. Study Outlook", &open, flags)) {
		ImGui::TextColored(ImVec4(0.55f, 0.62f, 0.75f, 1.0f), "N.C.G. STRUCTURAL STUDY - OUTLOOK");
		ImGui::Separator();

		auto bar = [](const char* label, int cur, int mx, float pct, float threshold) {
			ImGui::TextColored(overlay::titleColor, "%s  %d / %d  (%d%%)", label, cur, mx, static_cast<int>(pct * 100.0f + 0.5f));
			const ImU32 c = (pct >= threshold) ? IM_COL32(120, 210, 120, 255) : IM_COL32(220, 170, 80, 255);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImColor(c).Value);
			ImGui::ProgressBar(pct, ImVec2(-1.0f, 14.0f), "");
			ImGui::PopStyleColor();
		};
		bar("Photographs", photoCur, photoMax, photoPct, 0.50f);
		bar("Corruption ", corrCur, corrMax, corrPct, 0.50f);

		ImGui::Separator();
		const bool study = (photoPct >= 0.50f && corrPct >= 0.50f);
		const bool raven = (corrPct >= 0.90f);
		ImGui::TextColored(overlay::titleColor, "On track for:");
		if (study) ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.55f, 1.0f), "  Study published - 'good' ending");
		else       ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.45f, 1.0f), "  Insufficient documentation - 'bad' ending");
		if (raven) ImGui::TextColored(ImVec4(0.70f, 0.80f, 1.0f, 1.0f), "  + Raven Research contact (90%% corruption)");

		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.68f, 0.72f, 1.0f));
		ImGui::TextWrapped("Needs >=50%% photos and corruption for the study, plus saving the reactor. "
			"The game scores each Act separately - this is your overall total, so aim above the line in every Act.");
		ImGui::PopStyleColor();
	}
	ImGui::End();
	ImGui::PopStyleColor();

	if (!open) overlay::endingShown = false;
}

// ============================================================================
//  Photo contact sheet - a thumbnail grid of the photos taken this session, with
//  a click-to-isolate view (zoom + pan) for closer investigation. Textures are
//  lazily loaded from disk (managed pool, so they survive device resets).
// ============================================================================
namespace {
	struct CSItem { IDirect3DTexture9* tex; int w; int h; bool tried; };
	std::map<std::wstring, CSItem> g_CSCache;
	int  g_CSSelected = -1;
	float g_CSZoom = 1.0f;
	int  g_CSLoadBudget = 0;               // texture decodes allowed this frame
	std::vector<std::wstring> g_CSFiles;   // photos found on disk (DCIM + subfolders)
	bool g_CSScanned = false;              // rescan when the sheet is (re)opened

	// Collect DCIM\*.jpg/png plus one level of subfolders. Fast (directory metadata
	// only, no decoding) - the expensive part is texture creation, which is budgeted.
	void CSAddDir(const std::wstring& dir) {
		WIN32_FIND_DATA fd;
		HANDLE h = FindFirstFile((dir + L"\\*").c_str(), &fd);
		if (h == INVALID_HANDLE_VALUE) return;
		do {
			if (fd.cFileName[0] == L'.') continue;
			const std::wstring full = dir + L"\\" + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue; // subdirs handled by caller
			const size_t len = wcslen(fd.cFileName);
			if (len > 4 && (_wcsicmp(fd.cFileName + len - 4, L".jpg") == 0 ||
				_wcsicmp(fd.cFileName + len - 4, L".png") == 0)) {
				g_CSFiles.push_back(full);
			}
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}
	void CSScan() {
		g_CSFiles.clear();
		CSAddDir(L"DCIM");
		WIN32_FIND_DATA fd;
		HANDLE h = FindFirstFile(L"DCIM\\*", &fd);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				if (fd.cFileName[0] != L'.' && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
					CSAddDir(std::wstring(L"DCIM\\") + fd.cFileName);
				}
			} while (FindNextFile(h, &fd));
			FindClose(h);
		}
		std::sort(g_CSFiles.begin(), g_CSFiles.end());
		g_CSScanned = true;
		LogV("contact: scanned DCIM, " + std::to_string(g_CSFiles.size()) + " photos");
	}

	IDirect3DTexture9* CSLoad(LPDIRECT3DDEVICE9 dev, const std::wstring& path, int& w, int& h, bool& pending) {
		pending = false;
		auto it = g_CSCache.find(path);
		if (it != g_CSCache.end()) { w = it->second.w; h = it->second.h; return it->second.tex; }
		// Not cached yet: only decode if this frame still has budget. Otherwise
		// report "pending" so the caller draws a placeholder and we retry next
		// frame - this is what keeps the sheet from freezing the game.
		if (g_CSLoadBudget <= 0) { pending = true; w = h = 0; return nullptr; }
		--g_CSLoadBudget;
		CSItem item = { nullptr, 0, 0, true };
		D3DXIMAGE_INFO info;
		IDirect3DTexture9* t = nullptr;
		char np[MAX_PATH] = { 0 };
		WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, np, sizeof(np) - 1, nullptr, nullptr);
		LogV(std::string("contact: loading texture ") + np);
		if (D3DXCreateTextureFromFileEx(dev, path.c_str(), D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2,
			1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, &info, nullptr, &t) == D3D_OK) {
			item.tex = t; item.w = static_cast<int>(info.Width); item.h = static_cast<int>(info.Height);
			LogV("contact: texture loaded OK");
		} else {
			LogE(std::string("contact: texture load FAILED for ") + np);
		}
		g_CSCache[path] = item;
		w = item.w; h = item.h;
		return item.tex;
	}

	// Fit (w,h) into a box of side `box`, preserving aspect.
	ImVec2 CSFit(int w, int h, float box) {
		if (w <= 0 || h <= 0) return ImVec2(box, box);
		const float a = static_cast<float>(w) / static_cast<float>(h);
		return (a >= 1.0f) ? ImVec2(box, box / a) : ImVec2(box * a, box);
	}
}

static void RenderContactSheet(LPDIRECT3DDEVICE9 dev) {
	const int clientWidth = Base::Data::HACK_clientRect.right - Base::Data::HACK_clientRect.left;
	ImGui::SetNextWindowSize(ImVec2(560.0f, 460.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(clientWidth) * 0.5f - 280.0f, 80.0f), ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!overlay::saveLayout) flags |= ImGuiWindowFlags_NoSavedSettings;

	// Photos come straight off disk (DCIM + one level of subfolders), scanned once
	// per open - so shots from previous sessions show too. Texture decoding is
	// budgeted to 1 per frame; undecoded thumbs render as placeholders and pop in
	// over the next frames instead of freezing the game.
	if (!g_CSScanned) CSScan();
	g_CSLoadBudget = 1;
	std::vector<std::wstring>& photos = g_CSFiles;

	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.13f, 0.97f));
	bool open = true;
	if (ImGui::Begin("N.C.G. Contact Sheet", &open, flags)) {
		if (g_CSSelected < 0 || g_CSSelected >= static_cast<int>(photos.size())) {
			g_CSSelected = -1;
			// (No inner title - the window titlebar already says it.)
			ImGui::TextColored(overlay::titleColor, "%d photo%s on card",
				static_cast<int>(photos.size()), photos.size() == 1 ? "" : "s");
			ImGui::SameLine();
			if (ImGui::SmallButton("Rescan")) { CSScan(); }
			ImGui::Separator();

			if (photos.empty()) {
				ImGui::TextColored(overlay::titleColor, "No photos on the card - take some with the in-game camera.");
			}

			const float thumb = 128.0f;
			const float avail = ImGui::GetContentRegionAvail().x;
			int cols = static_cast<int>(avail / (thumb + 12.0f));
			if (cols < 1) cols = 1;

			for (int i = 0; i < static_cast<int>(photos.size()); ++i) {
				int w = 0, h = 0;
				bool pending = false;
				IDirect3DTexture9* tex = CSLoad(dev, photos[i], w, h, pending);
				ImGui::PushID(i);
				if (tex) {
					const ImVec2 sz = CSFit(w, h, thumb);
					const ImVec2 p = ImGui::GetCursorScreenPos();
					const bool clicked = ImGui::InvisibleButton("##t", sz);
					ImDrawList* dl = ImGui::GetWindowDrawList();
					dl->AddImage(reinterpret_cast<ImTextureID>(tex), p, ImVec2(p.x + sz.x, p.y + sz.y));
					if (ImGui::IsItemHovered()) {
						dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), IM_COL32(120, 160, 220, 255), 0, 0, 2.0f);
					}
					if (clicked) { g_CSSelected = i; g_CSZoom = 1.0f; LogV("contact: isolate photo"); }
				} else if (pending) {
					// Not decoded yet this frame - placeholder that fills in shortly.
					const ImVec2 p = ImGui::GetCursorScreenPos();
					ImGui::InvisibleButton("##pend", ImVec2(thumb, thumb));
					ImDrawList* dl = ImGui::GetWindowDrawList();
					dl->AddRectFilled(p, ImVec2(p.x + thumb, p.y + thumb), IM_COL32(30, 34, 42, 255), 3.0f);
					dl->AddText(ImVec2(p.x + thumb * 0.5f - 12.0f, p.y + thumb * 0.5f - 8.0f), IM_COL32(120, 130, 150, 255), "...");
				} else {
					ImGui::Button("(load\nfailed)", ImVec2(thumb, thumb));
				}
				ImGui::PopID();
				if ((i % cols) != (cols - 1)) ImGui::SameLine();
			}
		} else {
			// Isolated view: one photo, zoomable + pannable.
			bool exitIsolated = ImGui::Button("< Back");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200.0f);
			ImGui::SliderFloat("Zoom", &g_CSZoom, 0.1f, 6.0f, "%.2fx");
			ImGui::SameLine();
			if (ImGui::Button("Fit")) g_CSZoom = 1.0f;

			if (exitIsolated) {
				// Leave isolated mode and STOP rendering this view for the rest of
				// the frame - falling through here with g_CSSelected = -1 was an
				// out-of-bounds photos[-1] access (crash on Back).
				g_CSSelected = -1;
				LogV("contact: back to grid");
			} else if (g_CSSelected >= 0 && g_CSSelected < static_cast<int>(photos.size())) {
				// filename label
				const std::wstring& wp = photos[g_CSSelected];
				size_t slash = wp.find_last_of(L"\\/");
				std::wstring wname = (slash == std::wstring::npos) ? wp : wp.substr(slash + 1);
				char name[128] = { 0 };
				WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, name, sizeof(name) - 1, nullptr, nullptr);
				ImGui::SameLine();
				ImGui::TextColored(overlay::titleColor, "  %s", name);
				ImGui::Separator();

				int w = 0, h = 0;
				bool pending = false;
				IDirect3DTexture9* tex = CSLoad(dev, wp, w, h, pending);
				ImGui::BeginChild("##isolate", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
				if (tex) {
					ImGui::Image(reinterpret_cast<ImTextureID>(tex),
						ImVec2(static_cast<float>(w) * g_CSZoom, static_cast<float>(h) * g_CSZoom));
				} else if (pending) {
					ImGui::TextColored(overlay::titleColor, "Loading...");
				} else {
					ImGui::TextColored(overlay::titleColor, "Could not load this image.");
				}
				ImGui::EndChild();
			}
		}
	}
	ImGui::End();
	ImGui::PopStyleColor();

	if (!open) overlay::contactShown = false;
}


//  Sketchbook  -  a paintable square canvas (NCG structural-survey sheet) that
//  can be exported to PNG. Drawing is done into a CPU RGBA buffer that's mirrored
//  to a managed D3D texture for display, so saving is just dumping the buffer.
// ============================================================================
namespace {
	std::vector<unsigned char> g_SketchPixels;          // RGBA, sketchW x sketchH
	IDirect3DTexture9*         g_SketchTex = nullptr;    // display mirror (managed)
	int                        g_SketchTexSize = 0;
	bool                       g_SketchBufDirty = false;  // needs re-upload to texture
	bool                       g_SketchUnsaved = false;   // has content not yet saved
	bool                       g_SketchHasInk = false;    // anything drawn at all
	bool                       g_SketchInited = false;
	float                      g_BrushColor[3] = { 0.10f, 0.12f, 0.40f }; // ink blue
	int                        g_BrushSize = 4;
	bool                       g_Eraser = false;
	int                        g_LastPx = -1, g_LastPy = -1;

	// Canvas sampler control: the ImGui DX9 backend samples textures bilinearly,
	// which softens stroke edges (and, with straight alpha, can fringe them). For a
	// crisp, fringe-free canvas we flip the sampler to point filtering around the
	// canvas image via draw-list callbacks, then restore bilinear for everything else.
	LPDIRECT3DDEVICE9 g_SketchDev = nullptr;
	void Cb_PointSampling(const ImDrawList*, const ImDrawCmd*) {
		if (g_SketchDev) {
			g_SketchDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
			g_SketchDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		}
	}
	void Cb_LinearSampling(const ImDrawList*, const ImDrawCmd*) {
		if (g_SketchDev) {
			g_SketchDev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
			g_SketchDev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
		}
	}

	// (Transparent paper pixels use black RGB; see SketchClear for why.)

	// Minimal 5x7 uppercase font (only the glyphs used by the letterhead). Each
	// glyph is 7 rows; the low 5 bits of each row are the pixels (MSB = left).
	struct Glyph { char c; unsigned char rows[7]; };
	const Glyph kFont[] = {
		{ 'N', { 0x11,0x19,0x15,0x15,0x13,0x11,0x11 } },
		{ 'A', { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 } },
		{ 'T', { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 } },
		{ 'I', { 0x1F,0x04,0x04,0x04,0x04,0x04,0x1F } },
		{ 'O', { 0x0E,0x11,0x11,0x11,0x11,0x11,0x0E } },
		{ 'L', { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F } },
		{ 'C', { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E } },
		{ 'S', { 0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E } },
		{ 'U', { 0x11,0x11,0x11,0x11,0x11,0x11,0x0E } },
		{ 'G', { 0x0E,0x11,0x10,0x17,0x11,0x11,0x0F } },
		{ 'R', { 0x1E,0x11,0x11,0x1E,0x14,0x12,0x11 } },
		{ 'P', { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 } },
		{ 'D', { 0x1E,0x11,0x11,0x11,0x11,0x11,0x1E } },
		{ 'E', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F } },
		{ 'Y', { 0x11,0x11,0x0A,0x04,0x04,0x04,0x04 } },
		{ 'V', { 0x11,0x11,0x11,0x11,0x11,0x0A,0x04 } },
		{ 'M', { 0x11,0x1B,0x15,0x15,0x11,0x11,0x11 } },
		{ 'H', { 0x11,0x11,0x11,0x1F,0x11,0x11,0x11 } },
		{ 'B', { 0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E } },
		{ '.', { 0x00,0x00,0x00,0x00,0x00,0x00,0x04 } },
		{ '-', { 0x00,0x00,0x00,0x1F,0x00,0x00,0x00 } },
		{ ':', { 0x00,0x04,0x00,0x00,0x00,0x04,0x00 } },
		{ '0', { 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E } },
		{ '1', { 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E } },
		{ '2', { 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F } },
		{ '3', { 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E } },
		{ '4', { 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 } },
		{ '5', { 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E } },
		{ '6', { 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E } },
		{ '7', { 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 } },
		{ '8', { 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E } },
		{ '9', { 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C } },
		{ ' ', { 0x00,0x00,0x00,0x00,0x00,0x00,0x00 } },
	};

	const Glyph* FindGlyph(char c) {
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
		for (const Glyph& g : kFont) {
			if (g.c == c) return &g;
		}
		return nullptr;
	}

	inline void PutPixel(int x, int y, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
		const int W = overlay::sketchW, H = overlay::sketchH;
		if (x < 0 || y < 0 || x >= W || y >= H) return;
		unsigned char* p = &g_SketchPixels[(static_cast<size_t>(y) * W + x) * 4];
		p[0] = r; p[1] = g; p[2] = b; p[3] = a;
	}

	// Draw a string with the 5x7 font, scaled by `s`, top-left at (x,y).
	void StampText(int x, int y, int s, const char* text, unsigned char r, unsigned char g, unsigned char b) {
		int penX = x;
		for (const char* t = text; *t; ++t) {
			const Glyph* gl = FindGlyph(*t);
			if (gl != nullptr) {
				for (int row = 0; row < 7; ++row) {
					for (int col = 0; col < 5; ++col) {
						if (gl->rows[row] & (1 << (4 - col))) {
							for (int dy = 0; dy < s; ++dy)
								for (int dx = 0; dx < s; ++dx)
									PutPixel(penX + col * s + dx, y + row * s + dy, r, g, b);
						}
					}
				}
			}
			penX += 6 * s; // 5px glyph + 1px gap
		}
	}

	void SketchClear() {
		const int W = overlay::sketchW, H = overlay::sketchH;
		const int R = (W < H ? W : H); // reference dim for scaling
		// Paper is transparent (alpha 0). Its RGB is black so that bilinear filtering
		// at stroke/letter edges bleeds toward black (an invisible dark fringe) instead
		// of toward white (a bright halo). The visible "paper" is the white window
		// background (normal mode) or the game scene (trace mode), both shown through
		// the alpha-0 pixels; on save, normal mode flattens alpha-0 to white.
		for (int i = 0; i < W * H; ++i) {
			g_SketchPixels[i * 4 + 0] = 0;
			g_SketchPixels[i * 4 + 1] = 0;
			g_SketchPixels[i * 4 + 2] = 0;
			g_SketchPixels[i * 4 + 3] = 0;
		}

		const int s = (R >= 800) ? 4 : 2;

		// Colors come from [sketch] config (applied on Clear / new sheet).
		auto B = [](float v) { return static_cast<unsigned char>(v * 255.0f + 0.5f); };
		const unsigned char gr = B(overlay::sketchGrid.x), gg = B(overlay::sketchGrid.y),
			gb = B(overlay::sketchGrid.z), ga = B(overlay::sketchGrid.w);
		const unsigned char hr = B(overlay::sketchHead.x), hg = B(overlay::sketchHead.y),
			hb = B(overlay::sketchHead.z);

		// Optional survey grid.
		if (overlay::sketchSurvey && ga > 0) {
			const int step = R / 20 > 0 ? R / 20 : 1;
			for (int gx = step; gx < W; gx += step)
				for (int y = 0; y < H; ++y) PutPixel(gx, y, gr, gg, gb, ga);
			for (int gy = step; gy < H; gy += step)
				for (int x = 0; x < W; ++x) PutPixel(x, gy, gr, gg, gb, ga);
		}

		// NCG letterhead - fully-opaque ink so it reads on paper and over the
		// game scene alike.
		StampText(s * 6, s * 6, s, "NATIONAL CONSULTING GROUP", hr, hg, hb);
		StampText(s * 6, s * 6 + 9 * s, (s > 2 ? s - 1 : 1), "N.C.G.  STRUCTURAL SURVEY SHEET", hr, hg, hb);
		for (int x = s * 6; x < W - s * 6; ++x) {
			PutPixel(x, s * 6 + 18 * s, hr, hg, hb);
			PutPixel(x, s * 6 + 18 * s + 1, hr, hg, hb);
		}

		// Optional title block, bottom-left.
		if (overlay::sketchSurvey) {
			const int ts = (R >= 800) ? 2 : 1;
			int ty = H - (s * 6) - 26 * ts;
			const int tx = s * 6;
			std::string loc = overlay::locationName.empty() ? std::string("STALBURG") : overlay::locationName;
			for (char& c : loc) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
			StampText(tx, ty, ts, ("SURVEYOR: " + overlay::surveyorName).c_str(), hr, hg, hb); ty += 9 * ts;
			StampText(tx, ty, ts, ("SITE: " + loc).c_str(), hr, hg, hb); ty += 9 * ts;
			StampText(tx, ty, ts, ("DATE: " + overlay::surveyDate + "   N.C.G.").c_str(), hr, hg, hb);
		}

		g_SketchBufDirty = true;
		g_SketchUnsaved = false;
		g_SketchHasInk = false;
	}

	void SketchInit() {
		const int W = overlay::sketchW, H = overlay::sketchH;
		const size_t need = static_cast<size_t>(W) * H * 4;
		if (g_SketchInited && g_SketchPixels.size() == need) return;
		// Apply the configured starting brush once (don't stomp the user's live
		// choice on later re-inits / F6 reloads).
		static bool s_brushApplied = false;
		if (!s_brushApplied) {
			g_BrushSize = overlay::sketchDefaultBrush;
			g_BrushColor[0] = overlay::sketchDefaultInk[0];
			g_BrushColor[1] = overlay::sketchDefaultInk[1];
			g_BrushColor[2] = overlay::sketchDefaultInk[2];
			s_brushApplied = true;
		}
		g_SketchPixels.assign(need, 0);
		SketchClear();
		g_SketchInited = true;
	}

	// Stamp a filled circular brush dab centred at (cx,cy).
	void SketchDab(int cx, int cy) {
		const int rad = g_Eraser ? (g_BrushSize + 2) : g_BrushSize;
		unsigned char r = 0, g = 0, b = 0, a = 255;
		if (g_Eraser) {
			// Erase back to transparent paper (black RGB so edges bleed dark, not white).
			r = 0; g = 0; b = 0; a = 0;
		} else {
			r = static_cast<unsigned char>(g_BrushColor[0] * 255.0f);
			g = static_cast<unsigned char>(g_BrushColor[1] * 255.0f);
			b = static_cast<unsigned char>(g_BrushColor[2] * 255.0f);
			a = 255;
		}
		for (int dy = -rad; dy <= rad; ++dy) {
			for (int dx = -rad; dx <= rad; ++dx) {
				if (dx * dx + dy * dy <= rad * rad) {
					PutPixel(cx + dx, cy + dy, r, g, b, a);
				}
			}
		}
		g_SketchBufDirty = true;
		g_SketchUnsaved = true;
		if (!g_Eraser) g_SketchHasInk = true;
	}

	// Brush along a line from the last point to (px,py) so fast strokes stay solid.
	void SketchStrokeTo(int px, int py) {
		if (g_LastPx < 0) { SketchDab(px, py); g_LastPx = px; g_LastPy = py; return; }
		int x0 = g_LastPx, y0 = g_LastPy, x1 = px, y1 = py;
		int dx = abs(x1 - x0), dy = -abs(y1 - y0);
		int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
		int err = dx + dy;
		for (;;) {
			SketchDab(x0, y0);
			if (x0 == x1 && y0 == y1) break;
			int e2 = 2 * err;
			if (e2 >= dy) { err += dy; x0 += sx; }
			if (e2 <= dx) { err += dx; y0 += sy; }
		}
		g_LastPx = px; g_LastPy = py;
	}

	void SketchUploadToTexture(LPDIRECT3DDEVICE9 dev) {
		const int W = overlay::sketchW, H = overlay::sketchH;
		if (g_SketchTex == nullptr || g_SketchTexSize != W * 100000 + H) {
			if (g_SketchTex) { g_SketchTex->Release(); g_SketchTex = nullptr; }
			if (dev->CreateTexture(W, H, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_SketchTex, nullptr) != D3D_OK) {
				g_SketchTex = nullptr;
				return;
			}
			g_SketchTexSize = W * 100000 + H;
			g_SketchBufDirty = true;
		}

		if (!g_SketchBufDirty) return;

		D3DLOCKED_RECT lr;
		if (g_SketchTex->LockRect(0, &lr, nullptr, 0) == D3D_OK) {
			for (int y = 0; y < H; ++y) {
				unsigned char* dst = static_cast<unsigned char*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch;
				const unsigned char* src = &g_SketchPixels[static_cast<size_t>(y) * W * 4];
				for (int x = 0; x < W; ++x) {
					// our buffer is RGBA; D3DFMT_A8R8G8B8 in memory is BGRA
					dst[x * 4 + 0] = src[x * 4 + 2]; // B
					dst[x * 4 + 1] = src[x * 4 + 1]; // G
					dst[x * 4 + 2] = src[x * 4 + 0]; // R
					dst[x * 4 + 3] = src[x * 4 + 3]; // A
				}
			}
			g_SketchTex->UnlockRect(0);
			g_SketchBufDirty = false;
		}
	}

	void SketchSave(LPDIRECT3DDEVICE9 dev) {
		if (!g_SketchHasInk) return; // nothing drawn -> don't save a blank sheet

		const int W = overlay::sketchW, H = overlay::sketchH;
		IDirect3DSurface9* surf = nullptr;
		if (dev->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SCRATCH, &surf, nullptr) != D3D_OK) {
			return;
		}

		D3DLOCKED_RECT lr;
		if (surf->LockRect(&lr, nullptr, 0) == D3D_OK) {
			const bool keepAlpha = overlay::sketchTransparent;
			for (int y = 0; y < H; ++y) {
				unsigned char* dst = static_cast<unsigned char*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch;
				const unsigned char* src = &g_SketchPixels[static_cast<size_t>(y) * W * 4];
				for (int x = 0; x < W; ++x) {
					const unsigned char a = src[x * 4 + 3];
					if (keepAlpha) {
						// Preserve transparency (trace overlay) in the PNG.
						dst[x * 4 + 0] = src[x * 4 + 2];
						dst[x * 4 + 1] = src[x * 4 + 1];
						dst[x * 4 + 2] = src[x * 4 + 0];
						dst[x * 4 + 3] = a;
					} else if (a == 0) {
						// Transparent paper -> flatten to white.
						dst[x * 4 + 0] = 255; dst[x * 4 + 1] = 255; dst[x * 4 + 2] = 255; dst[x * 4 + 3] = 255;
					} else {
						dst[x * 4 + 0] = src[x * 4 + 2];
						dst[x * 4 + 1] = src[x * 4 + 1];
						dst[x * 4 + 2] = src[x * 4 + 0];
						dst[x * 4 + 3] = 255;
					}
				}
			}
			surf->UnlockRect();

			// Find the next free sketches\NCG_SURVEY_#####.png
			CreateDirectory(TEXT("sketches"), nullptr);
			int idx = 1;
			WIN32_FIND_DATA fd;
			HANDLE h = FindFirstFile(TEXT("sketches\\NCG_SURVEY_*.png"), &fd);
			if (h != INVALID_HANDLE_VALUE) {
				do {
					const wchar_t* digits = fd.cFileName + 11; // after "NCG_SURVEY_"
					int v = _wtoi(digits);
					if (v >= idx) idx = v + 1;
				} while (FindNextFile(h, &fd));
				FindClose(h);
			}
			TCHAR path[MAX_PATH];
			swprintf(path, MAX_PATH, TEXT("sketches\\NCG_SURVEY_%05d.png"), idx);

			if (D3DXSaveSurfaceToFile(path, D3DXIFF_PNG, surf, nullptr, nullptr) == D3D_OK) {
				g_SketchUnsaved = false;
			}
		}
		surf->Release();
	}
} // namespace

void overlay::SketchSaveOnUnload() {
	// Can't save here (no device); the render loop saves on close. This just frees.
	if (g_SketchTex) { g_SketchTex->Release(); g_SketchTex = nullptr; }
}

static void RenderSketch(LPDIRECT3DDEVICE9 dev) {
	SketchInit();
	SketchUploadToTexture(dev);

	// Initial window sized to the canvas aspect (resizable afterwards).
	const float aspect0 = static_cast<float>(overlay::sketchW) / static_cast<float>(overlay::sketchH > 0 ? overlay::sketchH : 1);
	ImGui::SetNextWindowSize(ImVec2(700.0f, 60.0f + 700.0f / (aspect0 > 0.01f ? aspect0 : 1.0f)), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowPos(ImVec2(60.0f, 60.0f), ImGuiCond_FirstUseEver);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
	if (!overlay::saveLayout) flags |= ImGuiWindowFlags_NoSavedSettings;

	// In normal mode the window background is opaque white "paper" (transparent
	// paper pixels show it through). In trace mode the background is clear so the
	// game scene shows through and you can trace over it.
	int pushedCol = 0;
	if (overlay::sketchTransparent) {
		ImGui::SetNextWindowBgAlpha(0.0f);
	} else {
		ImGui::PushStyleColor(ImGuiCol_WindowBg, overlay::sketchPaper);
		pushedCol = 1;
	}

	bool open = true;
	if (ImGui::Begin("NCG Sketchbook", &open, flags)) {
		// All toolbar labels in dark ink so they read on the cream paper (and over
		// the scene in trace mode) instead of the default light text.
		ImGui::PushStyleColor(ImGuiCol_Text, overlay::sketchToolInk);
		// Toolbar
		ImGui::ColorEdit3("Ink", g_BrushColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderInt("Brush", &g_BrushSize, 1, 40);
		ImGui::SameLine();
		ImGui::Checkbox("Eraser", &g_Eraser);
		ImGui::SameLine();
		ImGui::Checkbox("Trace", &overlay::sketchTransparent);
		ImGui::SameLine();
		ImGui::Checkbox("Smooth", &overlay::sketchSmooth);
		ImGui::SameLine();
		if (ImGui::Button("Clear")) { SketchClear(); }
		ImGui::SameLine();
		if (ImGui::Button("Save PNG")) { SketchSave(dev); }
		ImGui::SameLine();
		ImGui::TextColored(overlay::titleColor, g_SketchUnsaved ? "  (unsaved)" : "  (saved)");

		// Canvas: fit the W:H page into the available area, preserving aspect (so the
		// shape never distorts - resizing the window only scales/letterboxes it). An
		// InvisibleButton captures the mouse so dragging paints instead of moving the
		// window.
		if (g_SketchTex != nullptr) {
			const int W = overlay::sketchW, H = overlay::sketchH;
			const ImVec2 avail = ImGui::GetContentRegionAvail();
			const float aspect = static_cast<float>(W) / static_cast<float>(H);
			float dispW = avail.x, dispH = avail.x / aspect;
			if (dispH > avail.y) { dispH = avail.y; dispW = avail.y * aspect; }
			if (dispW < 1.0f) dispW = 1.0f;
			if (dispH < 1.0f) dispH = 1.0f;
			const ImVec2 canvasPos = ImGui::GetCursorScreenPos();

			ImGui::InvisibleButton("##canvas", ImVec2(dispW, dispH));
			ImDrawList* dl = ImGui::GetWindowDrawList();
			// Crisp (point) sampling unless the user asked for smooth (bilinear).
			g_SketchDev = dev;
			if (!overlay::sketchSmooth) dl->AddCallback(Cb_PointSampling, nullptr);
			dl->AddImage(
				reinterpret_cast<ImTextureID>(g_SketchTex),
				canvasPos, ImVec2(canvasPos.x + dispW, canvasPos.y + dispH));
			if (!overlay::sketchSmooth) {
				dl->AddCallback(Cb_LinearSampling, nullptr);
				dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
			}

			if (ImGui::IsItemActive() && dispW > 0.0f && dispH > 0.0f) {
				const ImVec2 m = ImGui::GetIO().MousePos;
				const int px = static_cast<int>((m.x - canvasPos.x) / dispW * W);
				const int py = static_cast<int>((m.y - canvasPos.y) / dispH * H);
				SketchStrokeTo(px, py);
			} else {
				g_LastPx = g_LastPy = -1; // pen lifted
			}
		} else {
			ImGui::TextColored(overlay::titleColor, "Canvas unavailable (texture alloc failed).");
		}
		ImGui::PopStyleColor(); // dark toolbar text
	}
	ImGui::End();

	if (pushedCol > 0) {
		ImGui::PopStyleColor(pushedCol);
	}

	if (!open) {
		overlay::sketchShown = false;
		if (g_SketchUnsaved) SketchSave(dev); // auto-save on close if there's ink
	}
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// Run the action bound to a virtual-key. Shared by the window-message path
// (WndProc) and the GetAsyncKeyState polling fallback, with a short same-key
// dedup so a press seen by both paths only fires once.
void overlay::DispatchHotkey(int vk) {
	static int lastVk = 0;
	static unsigned long long lastMs = 0;
	const unsigned long long now = GetTickCount64();
	if (vk == lastVk && (now - lastMs) < 120) return;
	lastVk = vk; lastMs = now;

	const overlay::Hotkeys& hk = overlay::hotkeys;
	if (vk == Base::Data::Keys::ToggleMenu) {
		overlay::shown = !overlay::shown;
		LogV(overlay::shown ? "hotkey: overlays SHOWN (toggle-menu)"
			: "hotkey: overlays HIDDEN (toggle-menu / Insert) - this hides ALL overlays");
	}
	else if (hk.reloadConfig && vk == hk.reloadConfig) {
		overlay::reloadRequested = true;
	}
	else if (hk.toggleCounters && vk == hk.toggleCounters) {
		overlay::countersEnabled = !overlay::countersEnabled;
	}
	else if (hk.toggleInventory && vk == hk.toggleInventory) {
		overlay::inventoryEnabled = !overlay::inventoryEnabled;
	}
	else if (hk.cycleCountersCorner && vk == hk.cycleCountersCorner) {
		overlay::countersCorner = NextCorner(overlay::countersCorner);
		overlay::forceReposition = true;
	}
	else if (hk.cycleInventoryCorner && vk == hk.cycleInventoryCorner) {
		overlay::inventoryCorner = NextCorner(overlay::inventoryCorner);
		overlay::forceReposition = true;
	}
	else if (hk.toggleLock && vk == hk.toggleLock) {
		overlay::locked = !overlay::locked;
	}
	else if (hk.resetPosition && vk == hk.resetPosition) {
		overlay::forceReposition = true;
	}
	else if (hk.toggleNotes && vk == hk.toggleNotes) {
		overlay::notesShown = !overlay::notesShown;
		if (!overlay::notesShown) overlay::SaveNotes();
	}
	else if (hk.toggleSketch && vk == hk.toggleSketch) {
		overlay::sketchShown = !overlay::sketchShown;
	}
	else if (hk.toggleCalculator && vk == hk.toggleCalculator) {
		overlay::calcShown = !overlay::calcShown;
	}
	else if (hk.toggleEnding && vk == hk.toggleEnding) {
		overlay::endingShown = !overlay::endingShown;
	}
	else if (hk.toggleContact && vk == hk.toggleContact) {
		overlay::contactShown = !overlay::contactShown;
		if (overlay::contactShown) g_CSScanned = false;
	}
	else if (overlay::debugReportKey && vk == overlay::debugReportKey) {
		overlay::forceReportRequested = true;
	}

	overlay::RunTweakKey(vk);
}

// Fallback input path: poll the hotkeys with GetAsyncKeyState from the render
// thread. Some systems / display modes never deliver WM_KEYDOWN to the overlay
// (the verbose log shows zero "hotkey: keydown" lines) even though EndScene runs
// fine - this makes the binds work there. Only polls when the game window is in
// the foreground, so it can't fire while alt-tabbed out.
void overlay::PollHotkeys() {
	HWND gw = Base::Data::hWindow;
	if (gw != nullptr && GetForegroundWindow() != gw) return;

	static bool prev[256] = { false };
	auto edge = [&](int vk) -> bool {
		if (vk <= 0 || vk > 255) return false;
		const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
		const bool rose = down && !prev[vk];
		prev[vk] = down;
		return rose;
	};
	const overlay::Hotkeys& hk = overlay::hotkeys;
	const int keys[] = {
		static_cast<int>(Base::Data::Keys::ToggleMenu), hk.reloadConfig, hk.toggleCounters,
		hk.toggleInventory, hk.cycleCountersCorner, hk.cycleInventoryCorner, hk.toggleLock,
		hk.resetPosition, hk.toggleNotes, hk.toggleSketch, hk.toggleCalculator, hk.toggleEnding,
		hk.toggleContact, overlay::debugReportKey
	};
	for (int vk : keys) {
		if (edge(vk)) {
			char pb[48];
			sprintf_s(pb, sizeof(pb), "hotkey: poll fired vk=0x%02X", vk);
			LogV(pb);
			overlay::DispatchHotkey(vk);
		}
	}
}

void overlay::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	// Act only on the initial key-down, not on auto-repeat (lParam bit 30).
	// F10 and Alt-combos arrive as WM_SYSKEYDOWN, so handle that too.
	if ((uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) && !(lParam & (1 << 30))) {
		const int vk = static_cast<int>(wParam);
		const overlay::Hotkeys& hk = overlay::hotkeys;

		// Verbose: record every key-down the overlay actually receives, with the
		// scancode and extended-key flag so the *physical* key is identifiable.
		// Several physical keys share one virtual-key code - notably Numpad 0 with
		// NumLock OFF sends VK_INSERT (0x2D), same as the dedicated Insert key
		// (ext=1 = dedicated Insert/nav cluster, ext=0 = numpad with NumLock off).
		{
			const int sc = (lParam >> 16) & 0xFF;
			const int ext = (lParam >> 24) & 0x1;
			char kb[80];
			sprintf_s(kb, sizeof(kb), "hotkey: keydown vk=0x%02X scancode=0x%02X ext=%d", vk, sc, ext);
			LogV(kb);
		}

		overlay::DispatchHotkey(vk);
	}

	// Only feed ImGui once it exists (WndProc now runs before the first render).
	if (overlay::imGuiInitialized) {
		ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
	}
}
// Draw the ImGui frame. Default: draw onto whatever render target is bound
// (the historical behaviour). *** EXPERIMENTAL *** force_backbuffer: bind the
// real back buffer first, so the overlay survives render-target changes from
// Source's post-processing pipeline (mat_postprocess_enable 0 otherwise leaves
// a non-backbuffer target bound and the overlay vanishes). Restores the prior
// target and releases both surfaces afterwards.
static void PresentImGui(LPDIRECT3DDEVICE9 dev) {
	if (!overlay::forceBackbuffer || dev == nullptr) {
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
		return;
	}
	IDirect3DSurface9* prevRT = nullptr;
	IDirect3DSurface9* backBuf = nullptr;
	const bool havePrev = (dev->GetRenderTarget(0, &prevRT) == D3D_OK);
	if (dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuf) == D3D_OK && backBuf != nullptr) {
		dev->SetRenderTarget(0, backBuf);
	}
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	if (havePrev && prevRT != nullptr) {
		dev->SetRenderTarget(0, prevRT);
	}
	if (backBuf != nullptr) backBuf->Release();
	if (prevRT != nullptr) prevRT->Release();
}

void overlay::Render(const HWND hWnd, const LPDIRECT3DDEVICE9 pDevice) {
	if (!overlay::imGuiInitialized) {
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
		// When enabled, ImGui persists window positions/sizes to this file so
		// dragged overlays keep their spot across game restarts. The string must
		// outlive the context, so keep it static.
		static std::string layoutPath = "silta_layout.ini";
		if (overlay::saveLayout && GetFileAttributesA(layoutPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
			// First run: seed the recommended default layout (tuned on 1920x1080,
			// coordinates rounded to clean values). Any drag overwrites it.
			static const char* kDefaultLayout =
				"[Window][Debug##Default]\nPos=60,60\nSize=400,400\nCollapsed=0\n\n"
				"[Window][Counters]\nPos=1731,940\nSize=161,117\nCollapsed=0\n\n"
				"[Window][Inventory]\nPos=1774,881\nSize=116,42\nCollapsed=0\n\n"
				"[Window][##progress]\nPos=440,10\nSize=396,102\nCollapsed=0\n\n"
				"[Window][Hotkeys]\nPos=840,10\nSize=235,222\nCollapsed=0\n\n"
				"[Window][N.C.G. Field Calculator]\nPos=1600,380\nSize=300,430\nCollapsed=0\n\n"
				"[Window][N.C.G. Study Outlook]\nPos=1080,10\nSize=330,250\nCollapsed=0\n\n"
				"[Window][Notes]\nPos=80,730\nSize=380,280\nCollapsed=0\n\n"
				"[Window][NCG Sketchbook]\nPos=470,250\nSize=700,760\nCollapsed=0\n\n"
				"[Window][N.C.G. Contact Sheet]\nPos=90,240\nSize=990,658\nCollapsed=0\n";
			std::ofstream lf(layoutPath, std::ios::binary | std::ios::trunc);
			if (lf.is_open()) lf << kDefaultLayout;
		}
		io.IniFilename = overlay::saveLayout ? layoutPath.c_str() : nullptr;
		ImGui_ImplWin32_Init(hWnd);
		ImGui_ImplDX9_Init(pDevice);
		overlay::imGuiInitialized = true;
	}

	if (g_font == nullptr) {
		RECT rect;
		if (GetClientRect(Base::Data::hWindow, &rect)) {
			Base::Data::HACK_clientRect = rect;

			int fontSize = DetermineFontSize(rect);

			// Latin + the check mark (U+2713) so the "complete = check" style renders.
			static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x2713, 0x2713, 0 };

			g_font =  ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF((const void*)DejaVuSansMono_compressed_data,
				DejaVuSansMono_compressed_size, static_cast<float>(fontSize), nullptr, ranges);
		}
	}


	ImGui_ImplDX9_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::PushFont(g_font);

	ApplyTheme();

	// Main menu: only the version watermark; all gameplay overlays skipped.
	if (overlay::inMenu) {
		RenderWatermark();
		ImGui::PopFont();
		ImGui::EndFrame();
		ImGui::Render();
		PresentImGui(pDevice);
		return;
	}

	if (countersEnabled) {
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, FocusAlpha(g_FocusCounters, overlay::countersFocusFade));
			RenderCounters();
			ImGui::PopStyleVar();
		}
	}

	if (inventoryEnabled) {
		{
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, FocusAlpha(g_FocusInv, overlay::invFocusFade));
			RenderInventory();
			ImGui::PopStyleVar();
		}
	}

	// Notes: shown by its toggle, or automatically while the cursor is out when
	// open_with_phone is enabled. Save when it transitions from shown to hidden.
	static bool wasShowingNotes = false;
	const bool showNotes = overlay::notesEnabled &&
		(overlay::notesShown || (overlay::notesAutoOpen && CursorIsVisible()));
	if (showNotes) {
		RenderNotes();
	} else if (wasShowingNotes) {
		overlay::SaveNotes();
	}
	wasShowingNotes = showNotes;

	if (overlay::sketchEnabled && overlay::sketchShown) {
		RenderSketch(pDevice);
	}

	if (overlay::calcEnabled && overlay::calcShown) {
		RenderCalculator();
	}

	if (overlay::endingEnabled && overlay::endingShown) {
		RenderEndingOutlook();
	}

	if (overlay::contactEnabled && overlay::contactShown) {
		RenderContactSheet(pDevice);
	}

	RenderHotkeyTips();
	RenderProgress();
	RenderFlashlightGauge();
	RenderReport();
	RenderToast();

	// The reposition request applied to this frame's windows; consume it.
	overlay::forceReposition = false;

	ImGui::PopFont();
	ImGui::EndFrame();
	ImGui::Render();
	PresentImGui(pDevice);
}