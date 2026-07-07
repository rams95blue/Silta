#include "functional_camera.h"
#include <strsafe.h>
#include "infra.h"
#include "base.h"
#include "overlay.h"
#include <fstream>
#include <vector>
#include <cstdio>
#include <cctype>
#include <cmath>
#include <thread>
#include <mutex>
#include <objidl.h>
// stdafx.h defines NOMINMAX, but the GDI+ headers use unqualified min/max.
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

using infra::Engine;
using namespace infra::structs;
extern std::ofstream g_LogWriter;

// Camera config (defaults; overridden from the ini in load_config()).
int  mod::functional_camera::outputWidth = 660;   // Crystal-shot 480p (lore default)
int  mod::functional_camera::outputHeight = 480;
bool mod::functional_camera::aspectCrop = true;
bool mod::functional_camera::backbufferCapture = false;
bool mod::functional_camera::engineCounterProbe = false;
unsigned int mod::functional_camera::engineCounterOffset = 0;
std::string mod::functional_camera::exifMake = "IMAGE-IN";
std::string mod::functional_camera::exifModel = "Crystal-shot";
std::string mod::functional_camera::exifSoftware = "";
bool mod::functional_camera::savePng = false;
bool mod::functional_camera::linearFilter = true;
bool mod::functional_camera::cameraNaming = true;
bool mod::functional_camera::saveToast = true;
float mod::functional_camera::toastSeconds = 0.5f;
std::string mod::functional_camera::toastLabel = "saved to microSD\\DCIM\\";
bool mod::functional_camera::burnIn = true;
bool mod::functional_camera::writeExif = true;
std::string mod::functional_camera::exifDateTime = "2016:08:08 12:00:00";
bool mod::functional_camera::exifDateTimeAuto = true;
int  mod::functional_camera::originOffset = 0;
float mod::functional_camera::originCalibX = 0.0f;
float mod::functional_camera::originCalibY = 0.0f;
bool  mod::functional_camera::originCalibValid = false;
unsigned char mod::functional_camera::burnAccent[3] = { 255, 235, 150 };
unsigned char mod::functional_camera::burnText[3]   = { 230, 230, 230 };
float mod::functional_camera::burnBandAlpha = 0.35f;
bool  mod::functional_camera::subfolders = true;
bool  mod::functional_camera::surveyLog = true;
bool  mod::functional_camera::assetTag = true;

static std::vector<std::wstring> g_SessionPhotos;
std::vector<std::wstring>& mod::functional_camera::SessionPhotos() { return g_SessionPhotos; }
bool mod::functional_camera::exifGps = true;
double mod::functional_camera::gpsLat0 = 57.0;
double mod::functional_camera::gpsLon0 = 19.0;
double mod::functional_camera::gpsUnitsPerDeg = 100000.0;

static CInfraCameraFreezeFrame* g_FreezeFrame;
static unsigned int g_ShouldSaveImage; // Set to 1 by our other thread when it's time to save the image, then the Direct3D thread does the actual saving.

// Scan a DCIM directory for existing DSC##### photos and return the next free
// index. The in-game camera is the IMAGE-IN Crystal-shot (480p, per its box art);
// the DSC##### naming is kept as the standard digital-camera file convention.
static int FindNextCameraIndex(const TCHAR* dir) {
	WIN32_FIND_DATA fd;
	int maxIdx = 0;
	TCHAR pat[MAX_PATH];
	swprintf(pat, MAX_PATH, TEXT("%sDSC*.*"), dir);

	HANDLE h = FindFirstFile(pat, &fd);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			if (_wcsnicmp(fd.cFileName, TEXT("DSC"), 3) == 0) {
				const int v = _wtoi(fd.cFileName + 3); // parses digits, stops at '.'
				if (v > maxIdx) {
					maxIdx = v;
				}
			}
		} while (FindNextFile(h, &fd));
		FindClose(h);
	}

	return maxIdx + 1;
}

// Folder-safe token for the current site (friendly location, else map name).
static std::string PhotoSubdirToken() {
	std::string t = overlay::locationName;
	if (t.empty()) { const char* mn = Engine()->get_map_name(); t = mn ? mn : "STALBURG"; }
	std::string out;
	for (char c : t) {
		if (isalnum(static_cast<unsigned char>(c))) out += c;
		else if (c == ' ' || c == '-' || c == '_') out += '_';
	}
	if (out.empty()) out = "STALBURG";
	return out;
}

static TCHAR* GetNextImagePath() {
	static TCHAR buf[MAX_PATH];
	SYSTEMTIME systemTime;

	const TCHAR* ext = mod::functional_camera::savePng ? TEXT("png") : TEXT("jpg");
	memset(buf, 0, sizeof(buf));

	CreateDirectory(TEXT("DCIM"), nullptr);

	// Target directory (optionally a per-site subfolder), with trailing backslash.
	TCHAR dir[MAX_PATH];
	if (mod::functional_camera::subfolders) {
		const std::string tok = PhotoSubdirToken();
		TCHAR sub[MAX_PATH];
		swprintf(sub, MAX_PATH, TEXT("DCIM\\%S"), tok.c_str());
		CreateDirectory(sub, nullptr);
		swprintf(dir, MAX_PATH, TEXT("DCIM\\%S\\"), tok.c_str());
	}
	else {
		StringCchCopy(dir, MAX_PATH, TEXT("DCIM\\"));
	}

	if (mod::functional_camera::cameraNaming) {
		const int idx = FindNextCameraIndex(dir);
		swprintf(buf, MAX_PATH, TEXT("%sDSC%05d.%s"), dir, idx, ext);
	}
	else {
		GetLocalTime(&systemTime);
		swprintf(
			buf, MAX_PATH, TEXT("%s%S_%d-%02d-%02d_%02d%02d%02d.%s"),
			dir, Engine()->get_map_name(), systemTime.wYear, systemTime.wMonth, systemTime.wDay,
			systemTime.wHour, systemTime.wMinute, systemTime.wSecond, ext
		);
	}

	return buf;
}

// ============================================================================
//  Photo annotation: a visible "burn-in" survey caption stamped onto the saved
//  image, plus embedded EXIF metadata (camera make/model, date, description,
//  optional player coordinates as a comment and GPS tags).
// ============================================================================
namespace {
	// Compact 5x7 font for the burn-in caption (A-Z, 0-9, and a little punctuation).
	struct PFGlyph { char c; unsigned char rows[7]; };
	const PFGlyph kPF[] = {
		{ ' ', {0,0,0,0,0,0,0} },
		{ 'A', {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11} }, { 'B', {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E} },
		{ 'C', {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E} }, { 'D', {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E} },
		{ 'E', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F} }, { 'F', {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10} },
		{ 'G', {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F} }, { 'H', {0x11,0x11,0x11,0x1F,0x11,0x11,0x11} },
		{ 'I', {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E} }, { 'J', {0x07,0x02,0x02,0x02,0x02,0x12,0x0C} },
		{ 'K', {0x11,0x12,0x14,0x18,0x14,0x12,0x11} }, { 'L', {0x10,0x10,0x10,0x10,0x10,0x10,0x1F} },
		{ 'M', {0x11,0x1B,0x15,0x15,0x11,0x11,0x11} }, { 'N', {0x11,0x11,0x19,0x15,0x13,0x11,0x11} },
		{ 'O', {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E} }, { 'P', {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10} },
		{ 'Q', {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D} }, { 'R', {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11} },
		{ 'S', {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E} }, { 'T', {0x1F,0x04,0x04,0x04,0x04,0x04,0x04} },
		{ 'U', {0x11,0x11,0x11,0x11,0x11,0x11,0x0E} }, { 'V', {0x11,0x11,0x11,0x11,0x11,0x0A,0x04} },
		{ 'W', {0x11,0x11,0x11,0x15,0x15,0x1B,0x11} }, { 'X', {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11} },
		{ 'Y', {0x11,0x11,0x0A,0x04,0x04,0x04,0x04} }, { 'Z', {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F} },
		{ '0', {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E} }, { '1', {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E} },
		{ '2', {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F} }, { '3', {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E} },
		{ '4', {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02} }, { '5', {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E} },
		{ '6', {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E} }, { '7', {0x1F,0x01,0x02,0x04,0x08,0x08,0x08} },
		{ '8', {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E} }, { '9', {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C} },
		{ '.', {0,0,0,0,0,0,0x04} }, { '-', {0,0,0,0x1F,0,0,0} }, { ':', {0,0x04,0,0,0,0x04,0} },
		{ '#', {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A} }, { '/', {0x01,0x02,0x04,0x08,0x10,0,0} },
	};
	const PFGlyph* PFFind(char c) {
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
		for (const PFGlyph& g : kPF) if (g.c == c) return &g;
		return nullptr;
	}

	// Stamp opaque text into a locked 32-bit BGRA surface at (x,y), scale px/cell.
	void PFStamp(D3DLOCKED_RECT& lr, int W, int H, int x, int y, int scale, const char* text,
		unsigned char r, unsigned char g, unsigned char b) {
		unsigned char* base = static_cast<unsigned char*>(lr.pBits);
		int cx = x;
		for (const char* p = text; *p; ++p) {
			const PFGlyph* gl = PFFind(*p);
			if (gl) {
				for (int ry = 0; ry < 7; ++ry)
					for (int rxb = 0; rxb < 5; ++rxb)
						if (gl->rows[ry] & (1 << (4 - rxb)))
							for (int sy = 0; sy < scale; ++sy)
								for (int sx = 0; sx < scale; ++sx) {
									int px = cx + rxb * scale + sx, py = y + ry * scale + sy;
									if (px >= 0 && py >= 0 && px < W && py < H) {
										unsigned char* d = base + static_cast<size_t>(py) * lr.Pitch + static_cast<size_t>(px) * 4;
										d[0] = b; d[1] = g; d[2] = r; d[3] = 255;
									}
								}
			}
			cx += 6 * scale;
		}
	}

	// Darken a horizontal band (for caption legibility) in a locked BGRA surface.
	// keep = how much of the underlying pixel to retain (0 = black, 1 = unchanged).
	void PFDarkenBand(D3DLOCKED_RECT& lr, int W, int H, int y0, int y1, float keep) {
		if (keep < 0.0f) keep = 0.0f;
		if (keep > 1.0f) keep = 1.0f;
		if (y0 < 0) y0 = 0;
		if (y1 > H) y1 = H;
		unsigned char* base = static_cast<unsigned char*>(lr.pBits);
		for (int y = y0; y < y1; ++y) {
			unsigned char* row = base + static_cast<size_t>(y) * lr.Pitch;
			for (int x = 0; x < W; ++x) {
				row[x * 4 + 0] = static_cast<unsigned char>(row[x * 4 + 0] * keep);
				row[x * 4 + 1] = static_cast<unsigned char>(row[x * 4 + 1] * keep);
				row[x * 4 + 2] = static_cast<unsigned char>(row[x * 4 + 2] * keep);
			}
		}
	}

	// Offset of the player origin found by calibration this session (0 = none).
	int g_OriginFound = 0;

	// Read the player world origin (3 floats). Default uses the struct member at
	// 0x2C (found via SILTA calibrate). A positive originOffset overrides it
	// (future game patches, re-find via [camera] calibrate); -1 disables.
	bool ReadPlayerOrigin(float& ox, float& oy, float& oz) {
		void* player = Engine()->CGlobalEntityList__FindEntityByName(nullptr, "!player");
		if (!player) return false;

		const int cfg = mod::functional_camera::originOffset;
		if (cfg < 0) return false;
		int off = 0x2C; // built-in
		if (cfg > 0) off = cfg;
		else if (g_OriginFound > 0) off = g_OriginFound; // fresh calibrate wins over builtin
		if (off > 0x1800) return false;
		const float* v = reinterpret_cast<const float*>(reinterpret_cast<const unsigned char*>(player) + off);
		ox = v[0]; oy = v[1]; oz = v[2];
		return true;
	}

	// EXIF capture time. With exif_datetime_auto, the time-of-day is approximated
	// from the map's chapter (INFRA runs across one canon day, morning -> ~23:00);
	// the mission index within the chapter nudges the clock forward (+20 min per
	// map) so shots from different maps get distinct times. The date comes from
	// the configured exif_datetime. NOT tied to real play time - by design, so a
	// slow playthrough still matches the canon timeline.
	std::string ComputeExifDateTime() {
		if (!mod::functional_camera::exifDateTimeAuto) return mod::functional_camera::exifDateTime;
		const char* mn = Engine()->get_map_name();
		if (!mn) return mod::functional_camera::exifDateTime;
		std::string m = mn;
		int ch = 0, mi = 0;
		size_t p = m.find("_c");
		if (p != std::string::npos) {
			size_t i = p + 2;
			while (i < m.size() && isdigit(static_cast<unsigned char>(m[i]))) { ch = ch * 10 + (m[i] - '0'); ++i; }
		}
		size_t q = m.find("_m");
		if (q != std::string::npos) {
			size_t i = q + 2;
			while (i < m.size() && isdigit(static_cast<unsigned char>(m[i]))) { mi = mi * 10 + (m[i] - '0'); ++i; }
		}
		// Chapter start times in minutes from midnight (canon: ~08:00 -> ~23:00).
		static const int kStart[11] = { 0, 480, 600, 720, 840, 930, 1020, 1110, 1200, 1290, 1380 };
		if (ch < 1 || ch > 10) return mod::functional_camera::exifDateTime;
		int minutes = kStart[ch] + (mi > 0 ? (mi - 1) * 20 : 0);
		const int next = (ch < 10) ? kStart[ch + 1] : 1435;
		if (minutes > next - 5) minutes = next - 5; // stay inside the chapter's window
		char hm[16];
		sprintf_s(hm, sizeof(hm), "%02d:%02d", minutes / 60, minutes % 60);
		std::string date = mod::functional_camera::exifDateTime.substr(0, 10); // YYYY:MM:DD
		if (date.size() < 10) date = "2016:08:08";
		return date + " " + hm + ":00";
	}

	// ---- Minimal EXIF (APP1) builder, little-endian. Validated against piexif. ----
	using Bytes = std::vector<unsigned char>;
	void U16(Bytes& b, unsigned short v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
	void U32(Bytes& b, unsigned int v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); b.push_back((v >> 16) & 0xFF); b.push_back((v >> 24) & 0xFF); }
	struct Ent { unsigned short tag, type; unsigned int count; Bytes data; bool inl; };
	Ent EStr(unsigned short t, const std::string& s) { Bytes d(s.begin(), s.end()); d.push_back(0); Ent e; e.tag = t; e.type = 2; e.count = (unsigned int)d.size(); e.inl = d.size() <= 4; if (e.inl) d.resize(4, 0); e.data = d; return e; }
	Ent EUndef(unsigned short t, const Bytes& raw) { Ent e; e.tag = t; e.type = 7; e.count = (unsigned int)raw.size(); e.inl = raw.size() <= 4; e.data = raw; if (e.inl) e.data.resize(4, 0); return e; }
	Ent ELong(unsigned short t, unsigned int v) { Ent e; e.tag = t; e.type = 4; e.count = 1; e.inl = true; e.data.clear(); U32(e.data, v); return e; }
	Ent EGps(unsigned short t, double deg) { unsigned int d = (unsigned int)deg; double rem = (deg - d) * 60.0; unsigned int m = (unsigned int)rem; double sec = (rem - m) * 60.0; Bytes data; U32(data, d); U32(data, 1); U32(data, m); U32(data, 1); U32(data, (unsigned int)(sec * 10000.0 + 0.5)); U32(data, 10000); Ent e; e.tag = t; e.type = 5; e.count = 3; e.inl = false; e.data = data; return e; }
	Bytes BuildIFD(std::vector<Ent>& es, unsigned int ifdStart, unsigned int nextIFD) {
		const unsigned int nE = (unsigned int)es.size();
		const unsigned int entBytes = 2 + nE * 12 + 4;
		unsigned int dataPos = ifdStart + entBytes;
		Bytes head, data; U16(head, (unsigned short)nE);
		for (auto& e : es) {
			U16(head, e.tag); U16(head, e.type); U32(head, e.count);
			if (e.inl) { for (int i = 0; i < 4; i++) head.push_back(i < (int)e.data.size() ? e.data[i] : 0); }
			else { U32(head, dataPos); data.insert(data.end(), e.data.begin(), e.data.end()); if (data.size() & 1) data.push_back(0); dataPos = ifdStart + entBytes + (unsigned int)data.size(); }
		}
		U32(head, nextIFD); head.insert(head.end(), data.begin(), data.end()); return head;
	}
	Bytes BuildExifApp1(const std::string& make, const std::string& model, const std::string& software,
		const std::string& dateTime, const std::string& desc, const std::string& userComment,
		bool hasGps, double lat, double lon) {
		Bytes tiff; tiff.push_back('I'); tiff.push_back('I'); U16(tiff, 0x002A); U32(tiff, 8);
		std::vector<Ent> ifd0;
		if (!desc.empty())     ifd0.push_back(EStr(0x010E, desc));
		if (!make.empty())     ifd0.push_back(EStr(0x010F, make));
		if (!model.empty())    ifd0.push_back(EStr(0x0110, model));
		if (!software.empty()) ifd0.push_back(EStr(0x0131, software));
		if (!dateTime.empty()) ifd0.push_back(EStr(0x0132, dateTime));
		size_t exifPtr = ifd0.size(); ifd0.push_back(ELong(0x8769, 0));
		size_t gpsPtr = (size_t)-1;
		if (hasGps) { gpsPtr = ifd0.size(); ifd0.push_back(ELong(0x8825, 0)); }
		const unsigned int ifd0Start = 8;
		Bytes ifd0Tmp = BuildIFD(ifd0, ifd0Start, 0);
		const unsigned int exifStart = ifd0Start + (unsigned int)ifd0Tmp.size();
		std::vector<Ent> exif;
		{ Bytes uc; const char cs[8] = { 'A','S','C','I','I',0,0,0 }; uc.insert(uc.end(), cs, cs + 8); uc.insert(uc.end(), userComment.begin(), userComment.end()); exif.push_back(EUndef(0x9286, uc)); }
		Bytes exifSer = BuildIFD(exif, exifStart, 0);
		const unsigned int gpsStart = exifStart + (unsigned int)exifSer.size();
		Bytes gpsSer;
		if (hasGps) {
			std::vector<Ent> gps;
			{ Bytes ver = { 2,3,0,0 }; Ent e; e.tag = 0x0000; e.type = 1; e.count = 4; e.inl = true; e.data = ver; gps.push_back(e); }
			gps.push_back(EStr(0x0001, lat >= 0 ? "N" : "S"));
			gps.push_back(EGps(0x0002, lat < 0 ? -lat : lat));
			gps.push_back(EStr(0x0003, lon >= 0 ? "E" : "W"));
			gps.push_back(EGps(0x0004, lon < 0 ? -lon : lon));
			gpsSer = BuildIFD(gps, gpsStart, 0);
		}
		ifd0[exifPtr] = ELong(0x8769, exifStart);
		if (hasGps) ifd0[gpsPtr] = ELong(0x8825, gpsStart);
		Bytes ifd0Ser = BuildIFD(ifd0, ifd0Start, 0);
		tiff.insert(tiff.end(), ifd0Ser.begin(), ifd0Ser.end());
		tiff.insert(tiff.end(), exifSer.begin(), exifSer.end());
		if (hasGps) tiff.insert(tiff.end(), gpsSer.begin(), gpsSer.end());
		Bytes app1; app1.push_back(0xFF); app1.push_back(0xE1);
		unsigned int segLen = 2 + 6 + (unsigned int)tiff.size();
		app1.push_back((segLen >> 8) & 0xFF); app1.push_back(segLen & 0xFF);
		const char hdr[6] = { 'E','x','i','f',0,0 }; app1.insert(app1.end(), hdr, hdr + 6);
		app1.insert(app1.end(), tiff.begin(), tiff.end());
		return app1;
	}
	// Insert the APP1 right after SOI in the JPEG file at (wide) path.
	bool InsertExifFile(const TCHAR* path, const Bytes& app1) {
		FILE* f = nullptr;
		if (_wfopen_s(&f, path, TEXT("rb")) != 0 || !f) return false;
		fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
		if (n < 2) { fclose(f); return false; }
		Bytes jpg(n); fread(jpg.data(), 1, n, f); fclose(f);
		if (jpg[0] != 0xFF || jpg[1] != 0xD8) return false;
		Bytes out; out.push_back(0xFF); out.push_back(0xD8);
		out.insert(out.end(), app1.begin(), app1.end());
		out.insert(out.end(), jpg.begin() + 2, jpg.end());
		FILE* g = nullptr;
		if (_wfopen_s(&g, path, TEXT("wb")) != 0 || !g) return false;
		fwrite(out.data(), 1, out.size(), g); fclose(g); return true;
	}

	// Stamp the survey caption into a raw BGRA buffer (thread-safe: pure CPU).
	// loc/date/surveyor are passed in so worker threads never touch overlay state.
	void BurnInCaptionRaw(unsigned char* pixels, int pitch, int W, int H, const char* photoName,
		const std::string& locIn, const std::string& date, const std::string& surveyor) {
		D3DLOCKED_RECT lr; lr.Pitch = pitch; lr.pBits = pixels; // same layout the stampers use

		const int scale = (W >= 1600) ? 3 : (W >= 900 ? 2 : 1);
		const int line = 9 * scale;
		const int pad = 6 * scale;
		const int bandH = line * 2 + pad * 2;
		PFDarkenBand(lr, W, H, H - bandH, H, mod::functional_camera::burnBandAlpha);

		std::string loc = locIn.empty() ? std::string("STALBURG") : locIn;
		for (char& c : loc) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
		std::string top = "N.C.G. SURVEY  " + loc;
		std::string bot = std::string(photoName ? photoName : "") + "   " + date + "   " + surveyor;

		const unsigned char* ac = mod::functional_camera::burnAccent;
		const unsigned char* tc = mod::functional_camera::burnText;
		PFStamp(lr, W, H, pad, H - bandH + pad, scale, top.c_str(), ac[0], ac[1], ac[2]);
		PFStamp(lr, W, H, pad, H - bandH + pad + line, scale, bot.c_str(), tc[0], tc[1], tc[2]);
	}

	// Legacy surface path (sync fallback when the fast buffer path can't run).
	void BurnInCaption(IDirect3DSurface9* surf, int W, int H, const char* photoName) {
		D3DSURFACE_DESC sd; if (surf->GetDesc(&sd) != D3D_OK) return;
		if (sd.Format != D3DFMT_X8R8G8B8 && sd.Format != D3DFMT_A8R8G8B8) return;
		D3DLOCKED_RECT lr; if (surf->LockRect(&lr, nullptr, 0) != D3D_OK) return;
		BurnInCaptionRaw(static_cast<unsigned char*>(lr.pBits), lr.Pitch, W, H, photoName,
			overlay::locationName, overlay::surveyDate, overlay::surveyorName);
		surf->UnlockRect();
	}

	// ---- Fully off-thread photo encoding (GDI+) ----
	// D3DXSaveSurfaceToFile JPEG-encodes on the render thread (the capture hitch).
	// Instead the render thread only memcpys raw BGRA out of the SCRATCH surface;
	// a worker thread does burn-in + GDI+ encode + EXIF + log. GDI+ is thread-safe
	// and ships with Windows.
	bool GdipClsid(const WCHAR* mime, CLSID* out) {
		UINT num = 0, size = 0;
		if (Gdiplus::GetImageEncodersSize(&num, &size) != Gdiplus::Ok || size == 0) return false;
		std::vector<unsigned char> buf(size);
		Gdiplus::ImageCodecInfo* info = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buf.data());
		if (Gdiplus::GetImageEncoders(num, size, info) != Gdiplus::Ok) return false;
		for (UINT i = 0; i < num; ++i) {
			if (wcscmp(info[i].MimeType, mime) == 0) { *out = info[i].Clsid; return true; }
		}
		return false;
	}
	bool GdipEncodeToFile(const std::wstring& path, unsigned char* bgra, int w, int h, int pitch, bool png) {
		static ULONG_PTR s_token = 0;
		static bool s_started = false;
		static std::mutex s_initMx;
		{
			std::lock_guard<std::mutex> lk(s_initMx);
			if (!s_started) {
				Gdiplus::GdiplusStartupInput in;
				s_started = (Gdiplus::GdiplusStartup(&s_token, &in, nullptr) == Gdiplus::Ok);
			}
		}
		if (!s_started) return false;
		// 32bppRGB = BGRX in memory, matching X8R8G8B8/A8R8G8B8 (alpha ignored).
		Gdiplus::Bitmap bmp(w, h, pitch, PixelFormat32bppRGB, bgra);
		CLSID clsid;
		if (!GdipClsid(png ? L"image/png" : L"image/jpeg", &clsid)) return false;
		if (png) {
			return bmp.Save(path.c_str(), &clsid, nullptr) == Gdiplus::Ok;
		}
		ULONG q = 92; // JPEG quality
		Gdiplus::EncoderParameters ep;
		ep.Count = 1;
		ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
		ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
		ep.Parameter[0].NumberOfValues = 1;
		ep.Parameter[0].Value = &q;
		return bmp.Save(path.c_str(), &clsid, &ep) == Gdiplus::Ok;
	}
}


static void StretchAndSaveCameraImage(LPDIRECT3DDEVICE9 dev, IDirect3DSurface9* pSrcSurface, IDirect3DTexture9* texForFallback) {
	IDirect3DSurface9* pDestSurface = nullptr;
	RECT rect;
	float aspectRatio;
	D3DSURFACE_DESC srcDesc;
	int outWidth;
	int outHeight;
	DWORD d3dxFilter;
	D3DXIMAGE_FILEFORMAT fmt;
	TCHAR* path;
	bool saved = false;

#define CHECK_D3D_RESULT(func, msg) \
	if ((func) != D3D_OK) { \
		g_LogWriter << (msg) << std::endl; \
		g_LogWriter.flush(); \
		goto release; \
	}

	if (!GetClientRect(Base::Data::hWindow, &rect)) {
		g_LogWriter << "StretchAndSaveCameraImage(): GetClientRect() failed (error: " << std::hex << GetLastError() << std::dec << ")" << std::endl;
		rect = Base::Data::HACK_clientRect;
	}

	aspectRatio = static_cast<float>(rect.bottom - rect.top) / static_cast<float>(rect.right - rect.left);

	CHECK_D3D_RESULT(
		pSrcSurface->GetDesc(&srcDesc), "StretchAndSaveCameraImage(): Failed to GetDesc() of source surface"
	)

	// After a save-load the freeze-frame texture is often the game's 1x1 "not
	// loaded" placeholder for a moment - the photo isn't ready yet. Saving it
	// would produce a solid 660x480 blank (and may be tied to the hard freeze on
	// snap). Reject anything degenerate; the shot is skipped, not corrupted.
	// (pDestSurface isn't created yet and pSrcSurface is the caller's, so a plain
	// return leaks nothing.)
	if (srcDesc.Width < 16 || srcDesc.Height < 16) {
		g_LogWriter << "StretchAndSaveCameraImage(): source is " << srcDesc.Width << "x" << srcDesc.Height
			<< " - freeze-frame not ready (placeholder); photo skipped" << std::endl;
		g_LogWriter.flush();
		overlay::ShowToast("Photo not saved: camera not ready after load. Load the save again to fix this.", 4.0f);
		return;
	}

	// Output resolution. Defaults to the Crystal-shot's in-lore 480p (660x480);
	// photo_width/photo_height 0/0 = native capture, height 0 alone = follow the
	// in-game aspect (the pre-1.2 behaviour, also used for legacy output_width).
	outWidth = mod::functional_camera::outputWidth;
	outHeight = mod::functional_camera::outputHeight;
	if (outWidth <= 0 && outHeight <= 0) {
		outWidth = static_cast<int>(srcDesc.Width);
		outHeight = static_cast<int>(srcDesc.Height);
	} else if (outHeight <= 0) {
		outHeight = static_cast<int>(outWidth * aspectRatio);
		if (outHeight <= 0) outHeight = static_cast<int>(srcDesc.Height);
	} else if (outWidth <= 0) {
		outWidth = aspectRatio > 0.0001f ? static_cast<int>(outHeight / aspectRatio) : static_cast<int>(srcDesc.Width);
	}

	// Aspect handling when target aspect != capture aspect: a real camera crops
	// its sensor readout, it doesn't stretch - so 'crop' takes a centered source
	// window matching the target aspect (default). 'stretch' distorts instead.
	// Plain declaration + assignments (an initializer here would be jumped over
	// by the error-path 'goto release' above, which C++ forbids / MSVC C4533).
	RECT srcCrop;
	srcCrop.left = 0; srcCrop.top = 0;
	srcCrop.right = static_cast<LONG>(srcDesc.Width);
	srcCrop.bottom = static_cast<LONG>(srcDesc.Height);
	const RECT* pSrcCrop;
	pSrcCrop = nullptr;
	if (mod::functional_camera::aspectCrop && outWidth > 0 && outHeight > 0) {
		const double ta = static_cast<double>(outWidth) / static_cast<double>(outHeight);
		const double sa = static_cast<double>(srcDesc.Width) / static_cast<double>(srcDesc.Height);
		if (sa > ta * 1.001) {          // source wider -> crop left/right
			const int cw = static_cast<int>(srcDesc.Height * ta);
			srcCrop.left = (static_cast<int>(srcDesc.Width) - cw) / 2;
			srcCrop.right = srcCrop.left + cw;
			pSrcCrop = &srcCrop;
		} else if (sa < ta * 0.999) {   // source taller -> crop top/bottom
			const int chh = static_cast<int>(srcDesc.Width / ta);
			srcCrop.top = (static_cast<int>(srcDesc.Height) - chh) / 2;
			srcCrop.bottom = srcCrop.top + chh;
			pSrcCrop = &srcCrop;
		}
	}

	fmt = mod::functional_camera::savePng ? D3DXIFF_PNG : D3DXIFF_JPG;
	d3dxFilter = mod::functional_camera::linearFilter ? D3DX_FILTER_LINEAR : D3DX_FILTER_POINT;
	path = GetNextImagePath(); // compute once so the fallback reuses the same name

	g_LogWriter << "StretchAndSaveCameraImage(): saving " << outWidth << "x" << outHeight
		<< " (source " << srcDesc.Width << "x" << srcDesc.Height << ")" << std::endl;

	// Scale via a SCRATCH surface. Unlike StretchRect, D3DXLoadSurfaceFromSurface
	// does a software copy/scale, so it works regardless of the source's pool or
	// whether it's a render target (the freeze-frame texture is neither an RT nor
	// 1024px - it's a small plain texture, which is why StretchRect refused it).
	//
	// FAST PATH (no hitch): the render thread only memcpys the raw BGRA pixels out
	// of the SCRATCH surface (~1-2 ms); burn-in, JPEG/PNG encode (GDI+), EXIF and
	// the survey log all run on a detached worker thread. The old synchronous
	// D3DXSaveSurfaceToFile path remains as a fallback for non-32-bit formats.
	bool fastPathSpawned = false;
	if (dev->CreateOffscreenPlainSurface(static_cast<UINT>(outWidth), static_cast<UINT>(outHeight), srcDesc.Format, D3DPOOL_SCRATCH, &pDestSurface, nullptr) == D3D_OK) {
		if (D3DXLoadSurfaceFromSurface(pDestSurface, nullptr, nullptr, pSrcSurface, nullptr, pSrcCrop, d3dxFilter, 0) == D3D_OK) {
			const bool is32 = (srcDesc.Format == D3DFMT_X8R8G8B8 || srcDesc.Format == D3DFMT_A8R8G8B8);
			D3DLOCKED_RECT lr;
			if (is32 && pDestSurface->LockRect(&lr, nullptr, D3DLOCK_READONLY) == D3D_OK) {
				LogV("camera: pixel copy start");
				std::vector<unsigned char> pixels(static_cast<size_t>(outWidth) * outHeight * 4);
				for (int y = 0; y < outHeight; ++y) {
					memcpy(&pixels[static_cast<size_t>(y) * outWidth * 4],
						static_cast<const unsigned char*>(lr.pBits) + static_cast<size_t>(y) * lr.Pitch,
						static_cast<size_t>(outWidth) * 4);
				}
				pDestSurface->UnlockRect();
				LogV("camera: pixel copy done, spawning encode worker");

				// ---- Gather everything the worker needs (game-thread reads) ----
				const std::wstring wpath = path;
				const TCHAR* wbase = path;
				for (const TCHAR* p = path; *p; ++p) if (*p == TEXT('\\') || *p == TEXT('/')) wbase = p + 1;
				char narrow[80] = { 0 };
				WideCharToMultiByte(CP_UTF8, 0, wbase, -1, narrow, sizeof(narrow) - 1, nullptr, nullptr);
				const std::string nname = narrow;
				const std::string loc = overlay::locationName.empty() ? std::string("Stalburg") : overlay::locationName;
				const std::string assetTagStr = "NCG-" + PhotoSubdirToken() + "-" + nname;
				const std::string exifDT = ComputeExifDateTime();     // reads map name: game thread only
				const std::string surveyor = overlay::surveyorName;
				const std::string sdate = overlay::surveyDate;
				float ox = 0.0f, oy = 0.0f, oz = 0.0f;
				const bool hasOrigin = ReadPlayerOrigin(ox, oy, oz);  // touches the entity: game thread only
				const bool doBurn = mod::functional_camera::burnIn;
				const bool png = mod::functional_camera::savePng;
				const bool doExif = mod::functional_camera::writeExif && !png;
				const bool doLog = mod::functional_camera::surveyLog;
				const bool wantTag = mod::functional_camera::assetTag;
				const bool wantGps = mod::functional_camera::exifGps;
				const double gLat0 = mod::functional_camera::gpsLat0, gLon0 = mod::functional_camera::gpsLon0;
				const double gUnits = mod::functional_camera::gpsUnitsPerDeg;
				const std::string exMake = mod::functional_camera::exifMake;
				const std::string exModel = mod::functional_camera::exifModel;
				std::string exSoft = mod::functional_camera::exifSoftware;
				if (exSoft.empty()) exSoft = std::string(overlay::kModName) + " v" + overlay::kVersion;
				const int W = outWidth, H = outHeight;

				g_SessionPhotos.push_back(path);
				if (mod::functional_camera::saveToast && mod::functional_camera::toastSeconds > 0.0f) {
					overlay::ShowToast(mod::functional_camera::toastLabel + nname, mod::functional_camera::toastSeconds);
				}

				std::thread([pix = std::move(pixels), wpath, nname, loc, assetTagStr, exifDT, surveyor, sdate,
					exMake, exModel, exSoft,
					doBurn, png, doExif, doLog, wantTag, wantGps, gLat0, gLon0, gUnits, hasOrigin, ox, oy, oz, W, H]() mutable {
					if (doBurn) {
						BurnInCaptionRaw(pix.data(), W * 4, W, H, nname.c_str(), loc, sdate, surveyor);
					}
					const bool ok = GdipEncodeToFile(wpath, pix.data(), W, H, W * 4, png);
					if (!ok) {
						LogE("camera: GDI+ encode FAILED for " + nname);
						return;
					}
					LogV("camera: worker encoded " + nname);
					if (doExif) {
						std::string desc = "N.C.G. structural survey - " + loc;
						if (wantTag) desc += " [" + assetTagStr + "]";
						std::string comment = "Surveyor: " + surveyor + "; Site: " + loc;
						bool hasGps = false; double lat = 0.0, lon = 0.0;
						if (hasOrigin) {
							char c[128];
							sprintf_s(c, sizeof(c), "; pos X=%.1f Y=%.1f Z=%.1f", ox, oy, oz);
							comment += c;
							if (wantGps && gUnits != 0.0) {
								lat = gLat0 + oy / gUnits;
								lon = gLon0 + ox / gUnits;
								hasGps = true;
							}
						}
						Bytes app1 = BuildExifApp1(exMake.c_str(), exModel.c_str(),
							exSoft.c_str(), exifDT, desc, comment, hasGps, lat, lon);
						InsertExifFile(wpath.c_str(), app1);
					}
					if (doLog) {
						SYSTEMTIME st; GetLocalTime(&st);
						char line[320];
						if (hasOrigin) {
							sprintf_s(line, sizeof(line), "%s\t%s\t%04d-%02d-%02d %02d:%02d:%02d\tX=%.1f Y=%.1f Z=%.1f\r\n",
								nname.c_str(), loc.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, ox, oy, oz);
						} else {
							sprintf_s(line, sizeof(line), "%s\t%s\t%04d-%02d-%02d %02d:%02d:%02d\t-\r\n",
								nname.c_str(), loc.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
						}
						FILE* lf = nullptr;
						if (_wfopen_s(&lf, TEXT("DCIM\\survey_log.txt"), TEXT("ab")) == 0 && lf) {
							fwrite(line, 1, strlen(line), lf);
							fclose(lf);
						}
					}
				}).detach();

				fastPathSpawned = true;
				saved = true; // encode is in flight; failures are logged by the worker
			}
			else {
				// SLOW FALLBACK (non-32-bit format): the old synchronous path.
				if (mod::functional_camera::burnIn) {
					LogV("camera: burn-in start (sync fallback)");
					const TCHAR* nb = path;
					for (const TCHAR* p = path; *p; ++p) if (*p == TEXT('\\') || *p == TEXT('/')) nb = p + 1;
					char nbn[64] = { 0 };
					WideCharToMultiByte(CP_UTF8, 0, nb, -1, nbn, sizeof(nbn) - 1, nullptr, nullptr);
					BurnInCaption(pDestSurface, outWidth, outHeight, nbn);
				}
				LogV("camera: sync encode start (fallback)");
				if (D3DXSaveSurfaceToFile(path, fmt, pDestSurface, nullptr, nullptr) == D3D_OK) {
					saved = true;
				}
				LogV("camera: sync encode done");
			}
		}
	}

	// Fallback: save the source texture directly at its native resolution. This
	// path always works even if scaling failed, so a photo still gets written.
	if (!saved) {
		g_LogWriter << "StretchAndSaveCameraImage(): scaled save failed; saving native "
			<< srcDesc.Width << "x" << srcDesc.Height << std::endl;
		const HRESULT fh = texForFallback != nullptr
			? D3DXSaveTextureToFile(path, fmt, texForFallback, nullptr)
			: D3DXSaveSurfaceToFile(path, fmt, pSrcSurface, nullptr, nullptr);
		if (fh == D3D_OK) {
			saved = true;
		}
	}

	if (saved && !fastPathSpawned) {
		// Sync-fallback bookkeeping (fast path did all of this before spawning).
		g_LogWriter << "StretchAndSaveCameraImage(): Successfully saved image!" << std::endl;
		g_SessionPhotos.push_back(path);
		const TCHAR* wbase2 = path;
		for (const TCHAR* p = path; *p; ++p) if (*p == TEXT('\\') || *p == TEXT('/')) wbase2 = p + 1;
		char narrow2[80] = { 0 };
		WideCharToMultiByte(CP_UTF8, 0, wbase2, -1, narrow2, sizeof(narrow2) - 1, nullptr, nullptr);
		if (mod::functional_camera::saveToast && mod::functional_camera::toastSeconds > 0.0f) {
			overlay::ShowToast(mod::functional_camera::toastLabel + narrow2, mod::functional_camera::toastSeconds);
		}
	} else if (!saved) {
		g_LogWriter << "StretchAndSaveCameraImage(): FAILED to save image" << std::endl;
	}
	g_LogWriter.flush();
#undef CHECK_D3D_RESULT

release:
	if (pDestSurface != nullptr) pDestSurface->Release();
	// pSrcSurface is owned by the caller.
}

// *** EXPERIMENTAL / READ-ONLY *** Log a DWORD at engine.dll+offset each shot,
// for RE of the load-counter <-> freeze-frame correlation. Bounds-checked against
// engine.dll's mapped image size so a wrong offset just logs instead of faulting.
// Never writes. Verbose-only. Machine/build-specific - the offset from one build
// is meaningless on another, which is why this is a probe, not a fix.
static void LogEngineCounterProbe() {
	if (!mod::functional_camera::engineCounterProbe || mod::functional_camera::engineCounterOffset == 0) {
		return;
	}
	HMODULE base = GetModuleHandleA("engine.dll");
	if (base == nullptr) { LogV("engine-probe: engine.dll not loaded"); return; }
	const BYTE* b0 = reinterpret_cast<const BYTE*>(base);
	const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(b0);
	const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(b0 + dos->e_lfanew);
	const DWORD imgSize = nt->OptionalHeader.SizeOfImage;
	const unsigned int off = mod::functional_camera::engineCounterOffset;
	if (off + sizeof(DWORD) > imgSize) {
		char m[128];
		sprintf_s(m, sizeof(m), "engine-probe: offset 0x%X out of engine.dll range (image size 0x%X)", off, imgSize);
		LogV(m);
		return;
	}
	const DWORD val = *reinterpret_cast<const DWORD*>(b0 + off);
	char m[128];
	sprintf_s(m, sizeof(m), "engine-probe: [engine.dll+0x%X] = %u (0x%X)", off, val, val);
	LogV(m);
}

static void ExtractAndSaveCameraImageInner(LPDIRECT3DDEVICE9 pDevice, const CInfraCameraFreezeFrame* freezeFrame) {
	if (freezeFrame == nullptr || freezeFrame->m_pImage == nullptr) {
		return;
	}

	const char* map_name = Engine()->get_map_name();
	const int texId = freezeFrame->m_pImage->m_nTextureId;
	{
		// Diagnostic: log the freeze-frame texture id each shot. If this climbs
		// across save-loads and the failures track high ids, the id is running
		// past the texture dictionary (out of range) rather than pointing at a
		// live-but-recycled entry - which would mean a bounds check could replace
		// the SEH skip for that case.
		char b[64];
		sprintf_s(b, sizeof(b), "camera: freeze-frame texture id = %d", texId);
		LogV(b);
	}
	LogEngineCounterProbe();
	// Cheap pre-guard: reject an obviously out-of-range id before it turns into a
	// wild pointer inside MaterialSystem_GetTextureById. (An in-range but stale
	// entry still faults and still needs the SEH guard - you can't tell a freed
	// entry from a live one just by looking at the id.)
	if (texId < 0 || texId > 1000000) {
		g_LogWriter << "camera: freeze-frame texture id out of range (" << texId << ") - photo skipped" << std::endl;
		g_LogWriter.flush();
		return;
	}

	const CMatSystemTexture* tex = Engine()->MaterialSystem_GetTextureById(texId);

	if (tex == nullptr) {
		g_LogWriter << "tex was null in " << map_name << std::endl;
		return;
	}

	const CMaterial* mat = tex->m_pMaterial;

	if (mat == nullptr) {
		g_LogWriter << "mat was null in " << map_name << std::endl;
		return;
	}

	const CTexture* repTex = mat->m_representativeTexture;

	if (repTex == nullptr) {
		g_LogWriter << "repTex was null in " << map_name << std::endl;
		return;
	}

	Texture_t** texHandles = repTex->m_pTextureHandles;

	if (texHandles == nullptr) {
		g_LogWriter << "texHandles was null in " << map_name << std::endl;
		return;
	}

	if (texHandles[0] == nullptr) {
		g_LogWriter << "texHandles[0] was null in " << map_name << std::endl;
		return;
	}

	IDirect3DTexture9* srcTex = texHandles[0]->m_pTexture0;
	IDirect3DSurface9* srcSurf = nullptr;
	if (srcTex == nullptr || srcTex->GetSurfaceLevel(0, &srcSurf) != D3D_OK || srcSurf == nullptr) {
		g_LogWriter << "camera: failed to get freeze-frame surface" << std::endl;
		return;
	}
	StretchAndSaveCameraImage(pDevice, srcSurf, srcTex);
	srcSurf->Release();
}

// *** EXPERIMENTAL *** Capture the photo from the back buffer instead of walking
// the game's freeze-frame texture chain. The chain dereferences game-owned
// material/texture pointers that dangle after a save-load (rebuilt texture
// dictionary), which the SEH guard has to skip. Reading the presented frame
// avoids those pointers entirely - at the cost of capturing whatever is on
// screen at the capture instant (the frozen photo, ideally). Uses
// GetRenderTargetData to pull the back buffer into a system-memory surface,
// then the shared encode path.
static void CaptureBackBufferAndSave(LPDIRECT3DDEVICE9 dev) {
	IDirect3DSurface9* bb = nullptr;
	IDirect3DSurface9* resolved = nullptr; // non-MSAA RT for the resolve
	IDirect3DSurface9* sysSurf = nullptr;
	if (dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb) != D3D_OK || bb == nullptr) {
		g_LogWriter << "camera: GetBackBuffer failed (backbuffer capture)" << std::endl;
		g_LogWriter.flush();
		return;
	}
	D3DSURFACE_DESC d;
	if (bb->GetDesc(&d) == D3D_OK) {
		// Back buffers are usually multisampled, and GetRenderTargetData refuses
		// MSAA surfaces. Resolve first: StretchRect the back buffer into a plain
		// (non-MSAA) render target, then pull THAT into system memory.
		IDirect3DSurface9* copySrc = bb;
		if (dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &resolved, nullptr) == D3D_OK
			&& resolved != nullptr
			&& dev->StretchRect(bb, nullptr, resolved, nullptr, D3DTEXF_NONE) == D3D_OK) {
			copySrc = resolved; // resolved (non-MSAA) copy
		}
		if (dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sysSurf, nullptr) == D3D_OK) {
			if (dev->GetRenderTargetData(copySrc, sysSurf) == D3D_OK) {
				StretchAndSaveCameraImage(dev, sysSurf, nullptr);
			} else {
				g_LogWriter << "camera: GetRenderTargetData failed even after resolve - "
					"backbuffer capture unavailable here" << std::endl;
				g_LogWriter.flush();
			}
		}
	}
	if (sysSurf != nullptr) sysSurf->Release();
	if (resolved != nullptr) resolved->Release();
	if (bb != nullptr) bb->Release();
}

// SEH-guarded wrapper. The freeze-frame -> material -> texture walk above reads
// game-owned pointers that can dangle after loading a save (the material system
// rebuilds its texture cache), so an access violation there would crash the
// game. Catch it, log it, skip the photo. No C++ unwinding objects live in this
// frame (only 'crashed'), which is what makes __try/__except legal here.
static bool ExtractAndSaveCameraImage(LPDIRECT3DDEVICE9 pDevice, const CInfraCameraFreezeFrame* freezeFrame) {
	bool crashed = false;
	__try {
		ExtractAndSaveCameraImageInner(pDevice, freezeFrame);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		crashed = true;
	}
	if (crashed) {
		// No std::string temporaries here: this frame uses __try, so it must not
		// contain anything requiring object unwinding. The caller shows the toast.
		g_LogWriter << "camera: access violation caught during photo save - photo "
			"skipped (stale freeze-frame/texture pointer, typically after loading a save or "
			"alt-tabbing - the game rebuilds its textures and recreates them lazily)"
			<< std::endl;
		g_LogWriter.flush();
	}
	return crashed;
}

// Per-frame calibration hunt (throttled to 2 Hz). While [camera] calibrate holds
// a getpos reading and no offset is known, scan the player entity for two
// consecutive floats matching that X/Y. Runs continuously, so the documented
// flow works exactly as written: stand still, paste, F6 - within a second the
// offset locks on and is reported (toast + silta.log). One-time, any map.
void mod::functional_camera::CalibrationTick() {
	if (g_OriginFound > 0) return;
	if (mod::functional_camera::originOffset > 0) return;
	if (!mod::functional_camera::originCalibValid) return;

	static unsigned long long s_lastMs = 0;
	const unsigned long long now = GetTickCount64();
	if (now - s_lastMs < 500) return;
	s_lastMs = now;

	void* player = Engine()->CGlobalEntityList__FindEntityByName(nullptr, "!player");
	if (!player) return;
	const unsigned char* base = reinterpret_cast<const unsigned char*>(player);

	const float cx = mod::functional_camera::originCalibX;
	const float cy = mod::functional_camera::originCalibY;
	for (int o = 0; o + 12 <= 0x1800; o += 4) {
		const float* v = reinterpret_cast<const float*>(base + o);
		if (fabsf(v[0] - cx) < 1.0f && fabsf(v[1] - cy) < 1.0f) {
			g_OriginFound = o;
			char msg[160];
			sprintf_s(msg, sizeof(msg),
				"calibrate: PLAYER ORIGIN FOUND at offset %d (0x%X) - set [camera] player_origin_offset = %d",
				o, o, o);
			LogI(msg);
			overlay::ShowToast(std::string("SILTA: origin found, offset ") + std::to_string(o)
				+ " - coordinates active. See silta.log", 6.0f);
			return;
		}
	}
}

void mod::functional_camera::OnTakePicture(CInfraCameraFreezeFrame *freezeFrame) {
	g_FreezeFrame = freezeFrame;

	// OnCommand actually gets run from a different thread than the DirectX thread, so we latch this
	// in order to then check from the DirectX thread and do the save.
	InterlockedCompareExchange(&g_ShouldSaveImage, 1, 0);
}

void mod::functional_camera::EndScene(LPDIRECT3DDEVICE9 pDevice) {
	if (InterlockedCompareExchange(&g_ShouldSaveImage, 0, 1)) {
		if (mod::functional_camera::backbufferCapture) {
			CaptureBackBufferAndSave(pDevice);
		} else {
			// Toast here (not in the SEH wrapper - that frame can't hold a
			// std::string temporary) so the player knows the shot was skipped.
			if (ExtractAndSaveCameraImage(pDevice, g_FreezeFrame)) {
				overlay::ShowToast("Photo not saved: camera not ready after load. Load the save again to fix this.", 4.0f);
			}
		}
	}
}

// Drop any pending capture. Called on map/save load: a photo latched on the game
// thread just before the load must not fire against a freeze-frame that now
// points into a rebuilt texture cache.
void mod::functional_camera::ResetPendingCapture() {
	InterlockedExchange(&g_ShouldSaveImage, 0);
	g_FreezeFrame = nullptr;
}