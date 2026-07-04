#pragma once
#include "stdafx.h"

#define WNDPROC_INDEX GWL_WNDPROC
#define D3DDEV9_LEN 119

typedef HRESULT(__stdcall* EndScene_t)(LPDIRECT3DDEVICE9);
typedef LRESULT(CALLBACK*  WndProc_t) (HWND, UINT, WPARAM, LPARAM);
typedef HRESULT(__stdcall* SetTexture_t)(LPDIRECT3DDEVICE9, DWORD, IDirect3DBaseTexture9*);

extern ImVec4 g_font_color;
extern ImVec4 g_font_color_max;

namespace Base {
	bool Init();
	bool Shutdown();

	namespace Data {
		extern HMODULE           hModule;
		extern LPDIRECT3DDEVICE9 pDxDevice9;
		extern void*             pDeviceTable[D3DDEV9_LEN];
		extern HWND              hWindow;
		extern WndProc_t         oWndProc;
		extern bool              Detached;
		extern bool              ToDetach;
		extern bool Inited;
		extern RECT HACK_clientRect;


		namespace Keys {
			const UINT ToggleMenu = VK_INSERT;
		}
	}

	namespace Hooks {
		bool Init();
		bool Shutdown();
	}
}
