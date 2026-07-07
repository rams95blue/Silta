#pragma once
#include "infra.h"
#include "stdafx.h"
#include <string>
#include <vector>

namespace mod {
	namespace functional_camera {
		// Configurable from silta.ini ([camera] section).
		extern int  outputWidth;   // saved photo width in px (0 = native)
		extern int  outputHeight;  // saved photo height in px (0 = follow aspect)
		extern bool aspectCrop;    // true = center-crop to target aspect, false = stretch
		extern bool backbufferCapture; // EXPERIMENTAL: capture from back buffer, not freeze-frame
		extern bool engineCounterProbe;    // EXPERIMENTAL: log engine.dll+offset each shot (read-only)
		extern unsigned int engineCounterOffset; // offset into engine.dll (0 = disabled)
		extern std::string exifMake;     // EXIF camera make
		extern std::string exifModel;    // EXIF camera model
		extern std::string exifSoftware; // EXIF software (empty = SILTA v<version>)
		// (legacy comment) width px; height follows the
		                           // game aspect ratio. 0 = native captured width.
		extern bool savePng;       // true = PNG (lossless), false = JPG
		extern bool linearFilter;  // true = linear scaling, false = point/nearest
		extern bool cameraNaming;  // true = DSC#####.ext (Sony Cyber-shot style),
		                           // false = <map>_<timestamp>.ext (original)
		extern bool saveToast;     // show an IMAGE-IN toast when a photo saves
		extern float toastSeconds; // toast lifetime in seconds (0 = disabled)
		extern std::string toastLabel; // cosmetic prefix before the photo name

		extern bool burnIn;        // stamp a visible survey caption onto the photo
		extern bool writeExif;     // embed EXIF metadata (JPEG only)
		extern std::string exifDateTime; // EXIF timestamp "YYYY:MM:DD HH:MM:SS"
		extern bool exifDateTimeAuto;    // derive the time-of-day from the map's chapter
		extern int  originOffset;  // byte offset of the player origin Vector (0 = off/auto)
		extern float originCalibX; // a getpos X/Y reading used to auto-find originOffset
		extern float originCalibY;
		extern bool  originCalibValid;
		extern unsigned char burnAccent[3]; // caption top-line colour (RGB)
		extern unsigned char burnText[3];   // caption bottom-line colour (RGB)
		extern float burnBandAlpha;         // caption band darkness (0=black .. 1=clear)
		extern bool  subfolders;   // sort photos into per-site DCIM subfolders
		extern bool  surveyLog;    // append each photo to DCIM\survey_log.txt
		extern bool  assetTag;     // include an N.C.G. asset tag in the EXIF

		// Full (wide) paths of photos saved this session, for the contact sheet.
		std::vector<std::wstring>& SessionPhotos();

		// Call once per frame: while [camera] calibrate is set and the offset is
		// unknown, scans the player entity for the pasted getpos X/Y (throttled).
		void CalibrationTick();
		extern bool exifGps;       // also map coords to EXIF GPS tags
		extern double gpsLat0;     // GPS latitude at world origin
		extern double gpsLon0;     // GPS longitude at world origin
		extern double gpsUnitsPerDeg; // world units per degree of lat/long

		void OnTakePicture(infra::structs::CInfraCameraFreezeFrame* freezeFrame);
		void EndScene(LPDIRECT3DDEVICE9 pDevice);
		void ResetPendingCapture(); // clear a latched photo on map/save load
	}
}