#pragma once
#include "stdafx.h"
#include <string>

namespace overlay {
	// ---- Mod identity (single source of truth for branding) ----
	// SILTA - Finnish for "bridge": Siltanen's field kit, bridging INFRA to your
	// survey tools. Bump kVersion per release.
	constexpr const char* kModName = "SILTA";
	constexpr const char* kVersion = "0.919";
	extern bool  watermark;         // small corner tag with name+version (menu only)
	extern int   watermarkCorner;   // 0 TL, 1 TR, 2 BL, 3 BR
	extern std::string watermarkText; // override text (empty = "SILTA v<ver>")
	extern bool  forceBackbuffer;   // EXPERIMENTAL: force back buffer as RT before draw
	extern bool  usePresentRender;  // EXPERIMENTAL: draw overlay in Present, not EndScene
	extern bool  inMenu;            // true while no map is loaded (main menu)

	enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
	enum class Theme { Debug, Native };
	enum class CompleteStyle { Color, Check, Dim };

	struct OverlayLine_t {
		std::string name;
		std::string value;
		ImVec4 nameColor;
		ImVec4 valueColor;

		// blink data
		int blinksLeft;
		long long lastBlink;

		OverlayLine_t() = default;
		OverlayLine_t(std::string name, std::string value, const ImVec4& nameColor, const ImVec4& valueColor);
	};

	// Configurable style (populated from silta.ini at startup; these
	// are the defaults if the ini is missing or a key is absent).
	extern ImVec4 fontColor;       // normal counter text
	extern ImVec4 fontColorMax;    // counter text when a category is complete
	extern ImVec4 titleColor;      // map-name title + inventory labels
	extern float  backgroundAlpha; // overlay window background opacity 0..1
	extern int    margin;          // px gap from the screen edges
	extern Corner countersCorner;  // which corner the counters overlay anchors to
	extern Corner inventoryCorner; // which corner the inventory overlay anchors to

	extern bool shown;
	extern bool countersEnabled;   // success-counters overlay
	extern bool inventoryEnabled;  // battery / OS-coin overlay

	// Per-counter-category style. Index order matches the displayed rows:
	// 0 Defects, 1 Corruption, 2 Repairs, 3 Geocaches, 4 Flow meters.
	// Configured from the [counters] section of the ini.
	constexpr int CategoryCount = 5;
	extern ImVec4 categoryColor[CategoryCount];   // base text colour per category
	extern bool   categoryVisible[CategoryCount]; // hide a row when false

	// Inventory (battery) overlay colours: 0 flashlight, 1 camera, 2 OS coins.
	extern ImVec4 inventoryColor[3];
	extern bool   invBatteryIcons;  // draw a battery glyph by flashlight/camera lines
	extern bool   inventoryHiddenByMap; // current map is on [inventory] hidden_maps
	extern bool   countersFocusFade;    // counters rest transparent, wake on events
	extern bool   invFocusFade;         // same for the inventory overlay
	extern int    focusTransparency;    // idle transparency percent (90 = faint)
	extern float  focusSeconds;         // fully-visible time after an event
	extern float  focusRamp;            // fade in/out time between states
	extern bool   invCoinIcon;      // draw a coin glyph by OS coins (appears once > 0)
	extern bool   flashGauge;       // on-screen flashlight battery gauge while draining
	extern int    flashGaugeMax;    // battery value treated as 100%
	extern float  flashGaugeSeconds;// seconds the gauge lingers after draining stops
	extern float  flashGaugeFade;   // fade-out duration at the end of the linger
	extern bool   flashGaugeNumbers;// show the % (and spare count) text
	extern int    flashGaugeSkin;   // 0 = instrument panel, 1 = subtle, 2 = custom
	extern ImVec4 gaugeCustom[6];   // custom-skin: bg, frame, label, fill hi/mid/low
	extern float  gaugeX;           // gauge position (-1 = auto bottom-center)
	extern float  gaugeY;           // gauge position (-1 = auto)
	void SaveGaugePos();            // writes gauge_x/gauge_y to silta.ini

	// Free-form notes window (resizable scratchpad for puzzles/secrets/ARGs).
	enum class NotesSkin { Ncg, Pad, Plain, Geolog, Custom };

	extern bool   notesEnabled;     // feature active (responds to its hotkey)
	extern bool   notesShown;       // window currently visible
	extern ImVec4 notesColor;       // notes text colour (plain style)
	extern NotesSkin notesSkin;     // visual skin for the notes window
	extern ImVec4 notesCustom[4];   // custom skin: paper, ink, rule, margin
	extern ImVec4 sketchPaper;      // sketchbook paper (window bg)
	extern ImVec4 sketchToolInk;    // sketchbook toolbar text
	extern ImVec4 sketchGrid;       // survey grid lines (alpha respected)
	extern ImVec4 sketchHead;       // letterhead / title-block ink
	extern bool   notesAutoOpen;    // open the notes window when the cursor is out
	extern float  notesFontScale;   // text scale for the notes window

	extern bool   hintsEnabled;     // show the hotkey tip bar when the cursor is out
	extern bool   tipFade;          // fade the tip bar out after a delay
	extern float  tipFadeSeconds;   // seconds visible before it fades

	extern bool   saveLayout;       // persist dragged window positions across runs
	extern bool   canonLabels;      // use the game's wording for counter rows
	extern bool   locationNames;    // title counters with the real location name

	extern bool   showProgress;     // progress popup (%/next achievement) on phone
	extern float  progressSeconds;  // seconds the progress popup stays

	enum class CalcMode { Basic, Scientific, Programmer, Text, Style };
	extern bool   calcEnabled;      // N.C.G. field calculator panel (incl. cipher tools)
	extern bool   calcShown;
	extern CalcMode calcMode;       // basic / scientific / programmer / text / style
	extern ImVec4 calcCustom[7];    // custom-skin colors: window, button, hover,
	                                // active, accent, display text, display bg
	void SaveCalcCustomColors();    // writes calcCustom + skin=custom to silta.ini
	extern ImVec4 tipKeyColor;      // hotkey tip bar: key column
	extern ImVec4 tipTextColor;     // hotkey tip bar: description column
	extern int    calcSkin;         // 0 = N.C.G. (default), 1 = Osmo Olut

	extern bool   endingEnabled;    // N.C.G. study / ending-outlook panel
	extern bool   endingShown;

	extern bool   contactEnabled;   // photo contact-sheet viewer
	extern bool   contactShown;

	// Sketchbook (NCG survey sketchpad): a paintable square canvas saved as PNG.
	extern bool   sketchEnabled;
	extern bool   sketchShown;
	extern int    sketchSize;       // square fallback (used when width/height unset)
	extern int    sketchW;          // canvas width  in pixels
	extern int    sketchH;          // canvas height in pixels
	extern bool   sketchSurvey;     // draw a survey grid + title block
	extern bool   sketchTransparent;// transparent page (trace over the game)
	extern bool   sketchSmooth;     // bilinear (soft) vs point (crisp) canvas display
	extern int    sketchDefaultBrush; // starting brush size
	extern float  sketchDefaultInk[3];// starting ink colour (RGB 0..1)

	// Shared survey identity (sketch title block + camera photo caption).
	extern std::string surveyorName; // e.g. "M. SILTANEN"
	extern std::string surveyDate;   // e.g. "08.08.2016"

	// Current map's friendly location name (set on map load when known).
	extern std::string locationName;

	// Brief on-screen toast (e.g. camera save confirmation). seconds <= 0 = off.
	void ShowToast(const std::string& text, float seconds);
	void ShowReport(const std::string& title, const std::string& body, float seconds);
	extern int debugReportKey;            // *** DEBUG ONLY *** key to force the report (0 = off)
	extern volatile bool forceReportRequested; // set by the debug key, serviced in EndScene
	void RunTweakKey(int vk);             // run a [tweaks] key-bind command (if any)
}

// Leveled logging into silta.log (defined in infra.cpp). LogV lines only appear
// with [log] verbose = true, which also timestamps + flushes every line.
void LogV(const std::string& msg);
void LogI(const std::string& msg);
void LogE(const std::string& msg);

namespace overlay {

	extern Theme         theme;          // visual style of the overlay windows
	extern CompleteStyle completeStyle;  // how a finished category is marked
	extern bool          showTotal;      // show overall completion line

	// Live total (overall completion across the map), maintained by counters.cpp.
	extern int totalCurrent;
	extern int totalMax;
	extern int catCurrent[CategoryCount]; // per-category progress (for the popup)
	extern int catMax[CategoryCount];
	extern int currentAct;                // 1..3, inferred from the map name

	// Runtime positioning state.
	extern bool locked;           // true = pinned to corners; false = draggable
	extern bool forceReposition;  // one-shot: snap back to corners next frame

	// A reload of the ini was requested via hotkey; serviced on the render thread.
	extern bool reloadRequested;

	// Hotkeys (Win32 virtual-key codes; 0 = disabled).
	struct Hotkeys {
		int reloadConfig;
		int toggleCounters;
		int toggleInventory;
		int cycleCountersCorner;
		int cycleInventoryCorner;
		int toggleLock;
		int resetPosition;
		int toggleNotes;
		int toggleSketch;
		int toggleCalculator;
		int toggleEnding;
		int toggleContact;
	};
	extern Hotkeys hotkeys;

	extern OverlayLine_t title;
	extern std::vector<OverlayLine_t> lines;
	extern bool imGuiInitialized;
	extern int fontSize;

	void WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void DispatchHotkey(int vk);    // run the action bound to a key (shared)
	void PollHotkeys();             // GetAsyncKeyState fallback (render thread)
	extern bool useHotkeyPolling;   // EXPERIMENTAL: poll keys instead of window msgs
	void Render(HWND hWnd, LPDIRECT3DDEVICE9 pDevice);

	// True if a typing window (notes/sketch) is open and ImGui wants to capture
	// this input message (so the host WndProc can withhold it from the game).
	bool WantsToCapture(UINT uMsg);
	void LoadNotes(); // load notes text from disk
	void SaveNotes(); // persist notes text to disk
	void SketchSaveOnUnload(); // save the sketch if it has unsaved content
}