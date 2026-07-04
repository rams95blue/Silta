#include "counters.h"
#include "overlay.h"
#include "metadata.h"
#include <fstream>
#include <sstream>
#include <cstdlib>

extern std::ofstream g_LogWriter;

namespace mod {
	namespace counters {
		using infra::Engine;
		using infra::functions::GLOBALESTATE;

		static nlohmann::json current_mapdata;

		// Per-category current/max, kept for the overall-completion total line.
		static int g_catCurrent[overlay::CategoryCount] = { 0 };
		static int g_catMax[overlay::CategoryCount] = { 0 };

		int GetCategoryCurrent(int category) {
			return (category >= 0 && category < overlay::CategoryCount) ? g_catCurrent[category] : 0;
		}
		int GetCategoryMax(int category) {
			return (category >= 0 && category < overlay::CategoryCount) ? g_catMax[category] : 0;
		}

		// The full per-map dataset actually in use. Starts as the (FIX-corrected)
		// embedded g_mapdata, and is overwritten once at startup if an external
		// "mapdata.txt" is found next to infra.exe. This makes MrMagnetix's
		// "INFRA Success Counters FIX" file (a mapdata.txt) a drop-in replacement
		// and lets counts be edited without rebuilding the DLL.
		static nlohmann::json g_active_mapdata;
		static bool g_mapdata_loaded = false;

		void LoadMapData() {
			if (g_mapdata_loaded) {
				return;
			}
			g_mapdata_loaded = true;

			// Default to the embedded, already-FIX-corrected dataset.
			g_active_mapdata = g_mapdata;

			// Prefer an external mapdata.txt next to infra.exe if present and valid.
			// (Same filename/location the speedrun.com FIX instructs you to use.)
			std::ifstream f("mapdata.txt");
			if (!f.is_open()) {
				g_LogWriter << "counters: no external mapdata.txt, using embedded (FIX-corrected) data" << std::endl;
				return;
			}

			std::stringstream ss;
			ss << f.rdbuf();

			try {
				nlohmann::json parsed = nlohmann::json::parse(ss.str());
				g_active_mapdata = std::move(parsed);
				g_LogWriter << "counters: loaded external mapdata.txt (" << g_active_mapdata.size() << " maps)" << std::endl;
			}
			catch (const std::exception& e) {
				g_LogWriter << "counters: mapdata.txt failed to parse (" << e.what()
					<< "), falling back to embedded data" << std::endl;
				g_active_mapdata = g_mapdata;
			}
		}

		const char* GetMapStatName(int event_type) {
			const char* result = NULL;

			switch (event_type)
			{
			case 0:
				result = "successful_photos";
				break;
			case 1:
				result = "corruption_uncovered";
				break;
			case 2:
				result = "spots_repaired";
				break;
			case 3:
				result = "mistakes_made";
				break;
			case 4:
				result = "geocaches_found";
				break;
			case 5:
				result = "water_flow_meters_repaired";
				break;
			default:
				break;
			}
			return result;
		}

		std::string get_counter_name(const char* map_name, const char* stat_name) {
			return std::string(map_name) + "_counter_" + std::string(stat_name);
		}

		std::string format_stat(int value, int max_value) {
			return std::to_string(value) + " / " + std::to_string(max_value);
		}

		int get_max_value(int event_type, const char* map_name) {
			const char* v = "";

			switch (event_type)
			{
			case 0:
				v = "camera_targets";
				break;
			case 1:
				v = "corruption_targets";
				break;
			case 2:
				v = "repair_targets";
				break;
			case 3:
				v = "mistake_targets";
				break;
			case 4:
				v = "geocaches";
				break;
			case 5:
				v = "water_flow_meter_targets";
				break;
			default:
				break;
			}

			std::string s = map_name;
			transform(s.begin(), s.end(), s.begin(), ::tolower);
			return current_mapdata[s][v];
		}

		void update_gui_table(const int event_type, const char* map_name, const int value) {
			int max_value = 0;

			try {
				max_value = get_max_value(event_type, map_name);
			}
			catch (std::exception e) {
			}

			// Map the event type to a display row index (Defects, Corruption,
			// Repairs, Geocaches, Flow meters). Event type 3 (mistakes) isn't shown.
			int idx;
			switch (event_type) {
			case 0: idx = 0; break;
			case 1: idx = 1; break;
			case 2: idx = 2; break;
			case 4: idx = 3; break;
			case 5: idx = 4; break;
			default: return;
			}

			static const char* const kLabelsShort[overlay::CategoryCount] = {
				"Defects:     ",
				"Corruption:  ",
				"Repairs:     ",
				"Geocaches:   ",
				"Flow meters: ",
			};
			// The game's own wording (achievement terms).
			static const char* const kLabelsCanon[overlay::CategoryCount] = {
				"Photo spots:    ",
				"Corruption docs:",
				"Repairable:     ",
				"Geocaches:      ",
				"Water meters:   ",
			};
			const char* const* kLabels = overlay::canonLabels ? kLabelsCanon : kLabelsShort;

			// Use the per-category colour as the base. How a finished category is
			// shown depends on overlay::completeStyle.
			const ImVec4 base = overlay::categoryColor[idx];
			const bool complete = (max_value > 0 && value >= max_value);

			ImVec4 color = base;
			std::string valueStr = format_stat(value, max_value);

			switch (overlay::completeStyle) {
			case overlay::CompleteStyle::Check:
				if (complete) {
					valueStr += "  \xE2\x9C\x93"; // append a U+2713 check mark
				}
				break;
			case overlay::CompleteStyle::Dim:
				if (complete) {
					color = ImVec4(base.x, base.y, base.z, base.w * 0.40f);
				}
				break;
			case overlay::CompleteStyle::Color:
			default:
				if (value == max_value) {
					color = overlay::fontColorMax;
				}
				break;
			}

			overlay::lines[idx] = overlay::OverlayLine_t(
				kLabels[idx],
				valueStr,
				color,
				color
			);

			// Update the overall-completion totals.
			g_catCurrent[idx] = value;
			g_catMax[idx] = max_value;
			overlay::catCurrent[idx] = value;
			overlay::catMax[idx] = max_value;
			int tc = 0, tm = 0;
			for (int k = 0; k < overlay::CategoryCount; ++k) {
				tc += g_catCurrent[k];
				tm += g_catMax[k];
			}
			overlay::totalCurrent = tc;
			overlay::totalMax = tm;
		}

		void init_counter(const char* map_name, int event_type) {
			const std::string name = get_counter_name(map_name, GetMapStatName(event_type));
			const int index = Engine()->GlobalEntity_AddEntity(name.c_str(), map_name, GLOBALESTATE::GLOBAL_OFF);
			int value = 0;

			if (Engine()->GlobalEntity_GetState(index) == GLOBALESTATE::GLOBAL_OFF) {
				Engine()->GlobalEntity_SetCounter(index, 0);
				Engine()->GlobalEntity_SetState(index, GLOBALESTATE::GLOBAL_ON);
			}
			else {
				value = Engine()->GlobalEntity_GetCounter(index);
			}

			update_gui_table(event_type, map_name, value);
		}

		void exclude_inactive_photo_spot(const std::string& map_name, const std::string& spot_name, nlohmann::json& mapdata) {
			const int index = Engine()->GlobalEntity_AddEntity(spot_name.c_str(), map_name.c_str(), GLOBALESTATE::GLOBAL_OFF);

			if (Engine()->GlobalEntity_GetState(index) == GLOBALESTATE::GLOBAL_ON) {
				int num = mapdata[map_name]["camera_targets"];

				if (num > 0) {
					--num;
				}

				mapdata[map_name]["camera_targets"] = num;
			}
		}

		auto exclude_inactive_photo_spots(const std::string& map_name, nlohmann::json mapdata) {
			if (map_name == "infra_c2_m2_reserve2") {
				exclude_inactive_photo_spot(map_name, "infra_reserve1_dam_picture_taken", mapdata);
			}
			else if (map_name == "infra_c3_m2_tunnel2") {
				exclude_inactive_photo_spot(map_name, "infra_tunnel_elevator_picture_taken", mapdata);
				exclude_inactive_photo_spot(map_name, "infra_tunnel_cracks_picture_taken", mapdata);
			}
			else if (map_name == "infra_c5_m1_watertreatment") {
				exclude_inactive_photo_spot(map_name, "infra_watertreatment_steam_picture_taken", mapdata);
			}

			return mapdata;
		}

		// Map the internal map name (lowercase) to a friendly in-world location.
		std::string LocationForMap(const std::string& m) {
			struct Loc { const char* key; const char* name; };
			static const Loc kLoc[] = {
				{ "infra_c1_m1_office",         "Alcista Building - NCG Office" },
				{ "infra_c2_m1_reserve1",       "Hammer Valley Reservoir" },
				{ "infra_c2_m2_reserve2",       "Hammer Valley Reservoir" },
				{ "infra_c2_m3_reserve3",       "Hammer Valley Reservoir" },
				{ "infra_c3_m1_tunnel",         "Bergmann Tunnels" },
				{ "infra_c3_m2_tunnel2",        "Bergmann Tunnels" },
				{ "infra_c3_m3_tunnel3",        "Bergmann Tunnels" },
				{ "infra_c3_m4_tunnel4",        "Bergmann Tunnels" },
				{ "infra_c4_m2_furnace",        "Stalburg Steel - Furnace" },
				{ "infra_c4_m3_tower",          "Stalburg Steel - Tower" },
				{ "infra_c5_m1_watertreatment", "Pitheath Water Treatment Plant" },
				{ "infra_c5_m2_sewer",          "Pitheath Sewers" },
				{ "infra_c5_m2b_sewer2",        "Pitheath Sewers" },
				{ "infra_c6_m1_sewer3",         "Stalburg Sewers" },
				{ "infra_c6_m2_metro",          "Stalburg Metro" },
				{ "infra_c6_m3_metroride",      "Stalburg Metro" },
				{ "infra_c6_m4_waterplant",     "Central Water Plant" },
				{ "infra_c6_m5_minitrain",      "Service Railway" },
				{ "infra_c6_m6_central",        "Central Water Plant" },
				{ "infra_c7_m1_servicetunnel",  "Service Tunnel" },
				{ "infra_c7_m1b_skyscraper",    "Skyscraper" },
				{ "infra_c7_m2_bunker",         "S.N.W. Bunker" },
				{ "infra_c7_m3_stormdrain",     "Storm Drain" },
				{ "infra_c7_m4_cistern",        "Cistern" },
				{ "infra_c7_m5_powerstation",   "Power Station" },
				{ "infra_c8_m1_powerstation2",  "Power Station" },
				{ "infra_c8_m3_isle1",          "Castle Rock Island" },
				{ "infra_c8_m4_isle2",          "Castle Rock Island" },
				{ "infra_c8_m5_isle3",          "Castle Rock Island" },
				{ "infra_c8_m6_business",       "Point Elias - Business District" },
				{ "infra_c8_m7_business2",      "Point Elias - Business District" },
				{ "infra_c8_m8_officeblackout", "Alcista Building (Blackout)" },
				{ "infra_c9_m1_rails",          "Railway" },
				{ "infra_c9_m2_tenements",      "Tenements" },
				{ "infra_c9_m3_river",          "Stalburg River" },
				{ "infra_c9_m4_villa",          "Rosenthal Villa" },
				{ "infra_c9_m5_field",          "Outskirts" },
				{ "infra_c10_m1_npp",           "Black Rock Nuclear Power Plant" },
				{ "infra_c10_m2_reactor",       "Black Rock NPP - Reactor" },
				{ "infra_c10_m3_roof",          "Black Rock NPP - Roof" },
				{ "infra_c11_ending_1",         "Epilogue" },
				{ "infra_c11_ending_2",         "Epilogue" },
				{ "infra_c11_ending_3",         "Epilogue" },
			};
			for (const Loc& l : kLoc) {
				if (m == l.key) {
					return l.name;
				}
			}
			return "";
		}

		/*
			This is called once before a map is loaded
		*/
		void InitMapStats() {
			if (Engine()->is_in_main_menu()) {
				return;
			}

			LoadMapData();

			overlay::lines.resize(overlay::CategoryCount);

			for (int i = 0; i < overlay::CategoryCount; ++i) {
				overlay::lines[i].nameColor = overlay::categoryColor[i];
				overlay::lines[i].valueColor = overlay::categoryColor[i];
				overlay::lines[i].blinksLeft = 0;
				g_catCurrent[i] = 0;
				g_catMax[i] = 0;
			}
			overlay::totalCurrent = 0;
			overlay::totalMax = 0;

			const char* map_name = Engine()->get_map_name();
			std::string& s = overlay::title.value;
			s.assign(map_name);
			transform(s.begin(), s.end(), s.begin(), ::tolower);

			// Friendly location name (used by the title and the NCG notes memo).
			overlay::locationName = LocationForMap(s);
			if (overlay::locationNames && !overlay::locationName.empty()) {
				overlay::title.value = overlay::locationName;
			}

			// Infer act from the chapter number: c1-c5 = Act 1, c6-c7 = Act 2, c8+ = Act 3.
			{
				int chapter = 0;
				if (s.rfind("infra_c", 0) == 0) {
					chapter = atoi(s.c_str() + 7);
				}
				overlay::currentAct = (chapter <= 5) ? 1 : (chapter <= 7 ? 2 : 3);
			}

			current_mapdata = exclude_inactive_photo_spots(s, g_active_mapdata);

			for (int i = 0; i < 6; ++i) {
				init_counter(map_name, i);
			}
		}

		void StatSuccess(int event_type, int count, bool is_new) {
			if (!is_new || count == 0) {
				return;
			}

			const char* stat_name = GetMapStatName(event_type);

			if (!stat_name) {
				return;
			}

			const char* map_name = Engine()->get_map_name();
			const std::string name = get_counter_name(map_name, stat_name);

			const int index = Engine()->GlobalEntity_AddEntity(name.c_str(), map_name, GLOBALESTATE::GLOBAL_OFF);
			const int value = Engine()->GlobalEntity_AddToCounter(index, 1);

			update_gui_table(event_type, map_name, value);

			int line_idx = 0;

			switch (event_type)
			{
			case 0: line_idx = 0; break;
			case 1:	line_idx = 1; break;
			case 2:	line_idx = 2; break;
			case 4:	line_idx = 3; break;
			case 5:	line_idx = 4; break;
			default: break;
			}

			overlay::lines[line_idx].blinksLeft = 7;
		}
	}
}