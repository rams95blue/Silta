#pragma once

#include "targetver.h"

#if _MSC_VER < 1700
#define _BIND_TO_CURRENT_CRT_VERSION 1

#ifdef NDEBUG
#define _SECURE_SCL 0
#endif
#endif

#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#define STRICT
#define NOMINMAX

#include <windows.h>
#include "xinput.h"
#include <dinput.h>

//#include "Common.h"


#include <iostream>
#include <d3d9.h>
#include <d3dx9.h>
#include <MinHook.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_dx9.h>
#pragma comment(lib, "D3DX9.LIB")

#define PTR_ADD(P, A) ((void *)(((uint32_t) (P)) + (A)))
