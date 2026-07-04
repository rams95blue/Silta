#include "stdafx.h"
#include <base.h>

ImVec4 g_font_color = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
ImVec4 g_font_color_max = ImVec4(0.00f, 1.00f, 0.00f, 1.00f);

//Data
HMODULE           Base::Data::hModule    = (HMODULE)NULL;
void*             Base::Data::pDeviceTable[D3DDEV9_LEN];
LPDIRECT3DDEVICE9 Base::Data::pDxDevice9 = (LPDIRECT3DDEVICE9)NULL;
HWND              Base::Data::hWindow    = (HWND)NULL;
WndProc_t         Base::Data::oWndProc   = (WndProc_t)NULL;
bool              Base::Data::Detached   = false;
bool              Base::Data::ToDetach   = false;
bool Base::Data::Inited = false;
RECT Base::Data::HACK_clientRect;


bool Base::Init() {
	Hooks::Init();
	return true;
}

