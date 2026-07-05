#pragma once
#include "stdafx.h"

namespace mod {
	namespace inventory {
		extern float *osCoinsCounter;
		extern int* flashlightBatteriesCounter;
		extern int* cameraBatteriesCounter;

		// Live flashlight charge (0..100 or 0..1000 depending on the game's scale),
		// available only when the user supplies its byte offset in the player
		// entity via [inventory] flashlight_charge_offset. nullptr = unknown.
		extern int* flashlightChargeCounter;
		extern int  flashlightChargeOffset;
		extern bool chargeAutoscan;      // hunt the live-charge offset automatically
		extern int  chargeFoundOffset;   // autoscan result (0 = not found yet)

		// Call once per frame. While the flashlight is draining, scans the player
		// entity for an int matching the drain signature (drops ~1 per 2 s within
		// 0..1000). On a unique match it adopts the offset live and reports it.
		void ChargeAutoscanTick();

		// Call once per frame: if the counter pointers failed to resolve on map
		// load (chapter loads can run InitMapStats before the player entity
		// exists), retry once a second until they do.
		void RetryTick();

		void MapLoaded(const char *name);
	}
}