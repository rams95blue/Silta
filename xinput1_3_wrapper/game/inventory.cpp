#include "inventory.h"

#include "infra.h"
#include "shlwapi.h"
#include "overlay.h"
#include <vector>
#include <string>

using infra::Engine;
using infra::CMathCounter;
using infra::CHud;
using infra::CHudElement;
using infra::CFlashlightAmmo;
using infra::CCameraAmmo;
using infra::CINFRA_Player;

float *mod::inventory::osCoinsCounter = nullptr;
int* mod::inventory::flashlightBatteriesCounter = nullptr;
int* mod::inventory::cameraBatteriesCounter = nullptr;
int* mod::inventory::flashlightChargeCounter = nullptr;
int  mod::inventory::flashlightChargeOffset = 0;
bool mod::inventory::chargeAutoscan = false;
int  mod::inventory::chargeFoundOffset = 0;

// ---- Flashlight-charge autoscan ----
// The game stores the live charge as an int in percent-units (sk_ cvars: 1000
// max, -1 every 2 s while the flashlight is on), somewhere in the player entity.
// Instead of hunting it with Cheat Engine, sample the whole struct every 2.5 s
// and keep only slots whose value sits in 0..1001 and dropped by 1..6 since the
// previous sample. With the flashlight on, the real slot survives every window;
// timers and other counters die off. After 4 consecutive windows with survivors,
// a unique survivor is adopted live and reported (log + on-screen toast).
namespace {
	const int kScanInts = 0x1858 / 4;
	std::vector<int> s_scanPrev;
	std::vector<unsigned char> s_scanAlive;
	int s_scanWindows = 0;
	unsigned long long s_scanLastMs = 0;

	void ScanReset() {
		s_scanPrev.clear();
		s_scanAlive.clear();
		s_scanWindows = 0;
	}
}

void mod::inventory::ChargeAutoscanTick() {
	if (!chargeAutoscan || flashlightChargeOffset > 0 || chargeFoundOffset > 0) return;

	const unsigned long long now = GetTickCount64();
	if (now - s_scanLastMs < 2500) return;
	s_scanLastMs = now;

	CINFRA_Player* player = reinterpret_cast<CINFRA_Player*>(
		Engine()->CGlobalEntityList__FindEntityByName(nullptr, "!player"));
	if (player == nullptr) { ScanReset(); return; }
	const int* cur = reinterpret_cast<const int*>(player);

	if (s_scanPrev.size() != static_cast<size_t>(kScanInts)) {
		s_scanPrev.assign(cur, cur + kScanInts);
		s_scanAlive.assign(kScanInts, 1);
		s_scanWindows = 0;
		LogV("autoscan: baseline snapshot taken (turn the flashlight ON and keep it on)");
		return;
	}

	int aliveCount = 0, lastAlive = -1;
	for (int i = 0; i < kScanInts; ++i) {
		if (!s_scanAlive[i]) continue;
		const int v = cur[i];
		const int delta = s_scanPrev[i] - v; // positive while draining
		if (v >= 0 && v <= 1001 && delta >= 1 && delta <= 6) {
			++aliveCount; lastAlive = i;
		} else {
			s_scanAlive[i] = 0;
		}
	}
	for (int i = 0; i < kScanInts; ++i) s_scanPrev[i] = cur[i];

	if (aliveCount == 0) {
		// Flashlight off / wrong window - start over silently.
		ScanReset();
		return;
	}
	++s_scanWindows;
	LogV("autoscan: window " + std::to_string(s_scanWindows) + ", " + std::to_string(aliveCount) + " candidate(s)");

	if (s_scanWindows >= 4) {
		if (aliveCount == 1) {
			chargeFoundOffset = lastAlive * 4;
			flashlightChargeCounter = reinterpret_cast<int*>(
				reinterpret_cast<unsigned char*>(player) + chargeFoundOffset);
			char msg[160];
			sprintf_s(msg, sizeof(msg),
				"autoscan: FLASHLIGHT CHARGE FOUND at offset %d (0x%X), value %d - set [inventory] flashlight_charge_offset = %d",
				chargeFoundOffset, chargeFoundOffset, cur[lastAlive], chargeFoundOffset);
			LogI(msg);
			overlay::ShowToast(std::string("SILTA: charge found, offset ") + std::to_string(chargeFoundOffset)
				+ " - see silta.log", 6.0f);
		} else if (aliveCount <= 4) {
			// Nearly there - list the finalists so the log is useful either way.
			std::string finalists = "autoscan: finalists:";
			for (int i = 0; i < kScanInts; ++i) {
				if (s_scanAlive[i]) finalists += " off=" + std::to_string(i * 4) + " val=" + std::to_string(cur[i]) + ";";
			}
			LogI(finalists);
		}
	}
}

static bool FindCameraAndFlashlightCounters(int**ppFlashlightCount, int**ppCameraBatteriesCount) {
	CINFRA_Player* player = reinterpret_cast<CINFRA_Player*>(
		Engine()->CGlobalEntityList__FindEntityByName(nullptr, "!player")
	);

	if (player == nullptr) {
		return false;
	}

	*ppFlashlightCount = &(player->m_nFlashlightBatteries);
	*ppCameraBatteriesCount = &(player->m_nCameraBatteries);

	// Live flashlight charge. Default (offset 0) uses the struct member at 0x184C
	// (found via SILTA autoscan, sits right before the battery counters). A
	// positive offset overrides it (future game patches); -1 disables entirely.
	const int off = mod::inventory::flashlightChargeOffset;
	if (off == 0) {
		mod::inventory::flashlightChargeCounter = &(player->m_nFlashlightCharge);
	} else if (off > 0 && off <= 0x1854) {
		mod::inventory::flashlightChargeCounter =
			reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(player) + off);
	}

	return false;
}

static std::string g_LastMapName;

void mod::inventory::RetryTick() {
	if (flashlightBatteriesCounter != nullptr) return; // resolved
	if (g_LastMapName.empty()) return;                 // no map loaded yet

	static unsigned long long lastMs = 0;
	const unsigned long long now = GetTickCount64();
	if (now - lastMs < 1000) return;
	lastMs = now;

	FindCameraAndFlashlightCounters(&flashlightBatteriesCounter, &cameraBatteriesCounter);
	if (flashlightBatteriesCounter != nullptr) {
		LogI("inventory: counters resolved on retry (player spawned after map init)");
		// Coins live on the tenements map only; resolve them here too.
		if (StrStrA(g_LastMapName.c_str(), "tenements") != nullptr) {
			CMathCounter* mathCounter = reinterpret_cast<CMathCounter*>(
				Engine()->CGlobalEntityList__FindEntityByName(nullptr, "Opensewer_coins_counter"));
			if (mathCounter != nullptr) osCoinsCounter = &mathCounter->m_CounterValue;
		}
	}
}

void mod::inventory::MapLoaded(const char *name) {
	g_LastMapName = name ? name : "";
	osCoinsCounter = nullptr;
	flashlightBatteriesCounter = nullptr;
	cameraBatteriesCounter = nullptr;
	flashlightChargeCounter = nullptr;
	chargeFoundOffset = 0;
	ScanReset();

	FindCameraAndFlashlightCounters(&flashlightBatteriesCounter, &cameraBatteriesCounter);

	if (StrStrA(name, "tenements") != nullptr) {
		CMathCounter* mathCounter = reinterpret_cast<CMathCounter*>(
			Engine()->CGlobalEntityList__FindEntityByName(nullptr, "Opensewer_coins_counter")
		);

		if (mathCounter != nullptr) {
			osCoinsCounter = &mathCounter->m_CounterValue;
		}
	}
}
