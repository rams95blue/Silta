#pragma once
#include "stdafx.h"

#define _HOOKER_PROTOTYPE(BASE) template <typename T> void hook_##BASE(const int32_t offset, LPVOID pDetour, T **pOriginal);
#define _GETTER_PROTOTYPE(BASE) void* get_##BASE##_ptr(const int32_t offset) const;

namespace infra {
	// Various structures reverse-engineered from Infra / its version of the Source engine.
	namespace structs {
		// I made these with ReClass.NET
		class BitmapImage
		{
		public:
			char pad_0000[4]; //0x0000
			int32_t _upstairsPosX; //0x0004
			int32_t _upstairsPosY; //0x0008
			int32_t _upstairsSizeX; //0x000C
			int32_t _upstairsSizeY; //0x0010
			int32_t _color; //0x0014
			int32_t m_nTextureId; //0x0018
			int32_t m_clr; //0x001C
			char pad_0020[8]; //0x0020
			int32_t sizeX; //0x0028
			int32_t sizeY; //0x002C
			char pad_0030[4]; //0x0030
			bool b_useViewport; //0x0034
		}; //Size: 0x0035

		class CInfraCameraFreezeFrame
		{
		public:
			char pad_0000[80]; //0x0000
			void* _vpanel; //0x0050
			char* _panelName; //0x0054
			char pad_0058[260]; //0x0058
			BitmapImage* m_pImage; //0x015C
		}; //Size: 0x033C

		class CMaterialVar
		{
		public:
			char pad_0000[4]; //0x0000
			char* m_pStringVal; //0x0004
			int32_t m_intVal; //0x0008
			float m_VecVal[4]; //0x000C
			uint8_t m_Flags; //0x001C
			uint16_t m_Name; //0x001D
		}; //Size: 0x001F


		class Texture_t
		{
		public:
			uint8_t m_UTexWrap; //0x0000
			uint8_t m_VTexWrap; //0x0001
			uint8_t m_WTexWrap; //0x0002
			uint8_t m_MagFilter; //0x0003
			uint8_t m_MinFilter; //0x0004
			uint8_t m_MipFilter; //0x0005
			uint8_t m_NumLevels; //0x0006
			uint8_t m_SwitchNeeded; //0x0007
			uint8_t m_NumCopies; //0x0008
			uint8_t m_CurrentCopy; //0x0009
			char pad_000A[2]; //0x000A
			IDirect3DTexture9* m_pTexture0; //0x000C
			void* m_pTexture1; //0x0010
			int32_t N000003E4; //0x0014
			int32_t m_CreationFlags; //0x0018
			uint16_t m_DebugName; //0x001C
			uint16_t m_TextureGroupName; //0x001E
			void* m_pTextureGroupCounterGlobal; //0x0020
			void* m_pTextureGroupCounterFrame; //0x0024
			int32_t m_SizeBytes; //0x0028
			int32_t m_SizeTexels; //0x002C
			int32_t m_LastBoundFrame; //0x0030
			int32_t m_nTimesBoundMax; //0x0034
			int32_t m_nTimesBoundThisFrame; //0x0038
			int16_t m_Width; //0x003C
			int16_t m_Height; //0x003E
			int16_t m_Depth; //0x0040
			uint16_t m_Flags; //0x0042
		}; //Size: 0x0044

		class CTexture
		{
		public:
			char pad_0000[4]; //0x0000
			float m_vecReflectivity[3]; //0x0004
			int16_t m_Name; //0x0010
			int16_t m_TextureGroupName; //0x0012
			uint32_t m_nFlags; //0x0014
			uint32_t m_nInternalFlags; //0x0018
			int32_t m_nRefCount; //0x001C
			int32_t m_ImageFormat; //0x0020
			uint16_t m_nMappingWidth; //0x0024
			uint16_t m_nMappingHeight; //0x0026
			uint16_t m_nMappingDepth; //0x0028
			uint16_t m_nActualWidth; //0x002A
			uint16_t m_nActualHeight; //0x002C
			uint16_t m_nActualDepth; //0x002E
			uint16_t m_nActualMipCount; //0x0030
			uint16_t m_nFrameCount; //0x0032
			uint16_t m_nOriginalRTWidth; //0x0034
			uint16_t m_nOriginalRTHeight; //0x0036
			int16_t m_nDesiredDimensionLimit; //0x0038
			int16_t m_nDesiredTempDimensionLimit; //0x003A
			int16_t m_nActualDimensionLimit; //0x003C
			uint16_t m_nMipSkipCount; //0x003E
			Texture_t** m_pTextureHandles; //0x0040
			void* m_pTempTextureHandles; //0x0044
			char* m_pLowResImage; //0x0048
			void* m_nOriginalRenderTargetType; //0x004C
		}; //Size: 0x0050

		class CMaterial
		{
		public:
			char pad_0000[44]; //0x0000
			void* m_pShader; //0x002C
			int16_t m_Name; //0x0030
			int16_t m_TextureGroupName; //0x0032
			int32_t m_RefCount; //0x0034
			uint16_t m_Flags; //0x0038
			uint8_t m_VarCount; //0x003A
			uint8_t m_ProxyCount; //0x003B
			CMaterialVar** m_pShaderParams; //0x003C
			char pad_0040[64]; //0x0040
			CTexture* m_representativeTexture; //0x0080
		}; //Size: 0x0084

		class CMatSystemTexture
		{
		public:
			float m_s0; //0x0000
			float m_t0; //0x0004
			float m_s1; //0x0008
			float m_t1; //0x000C
			int32_t m_crcFile; //0x0010
			CMaterial* m_pMaterial; //0x0014
			CTexture* m_pTexture; //0x0018
			int32_t m_Texture2; //0x001C
			int32_t m_iWide; //0x0020
			int32_t m_iTall; //0x0024
			int32_t m_iInputWide; //0x0028
			int32_t m_iInputTall; //0x002C
			int32_t m_ID; //0x0030
			int32_t m_Flags; //0x0034
			void* m_pRegen; //0x0038
		}; //Size: 0x003C

		class CBaseEntity {
		};

		class CMathCounter
		{
		public:
			char pad_0000[208]; //0x0000
			char* m_szName; //0x00D0
			char pad_00D4[664]; //0x00D4
			float m_CounterValue; //0x036C
		}; //Size: 0x0370

#define NUM_SERIAL_NUM_BITS		16 // (32 - NUM_ENT_ENTRY_BITS)
#define NUM_SERIAL_NUM_SHIFT_BITS (32 - NUM_SERIAL_NUM_BITS)
#define ENT_ENTRY_MASK			(( 1 << NUM_SERIAL_NUM_BITS) - 1)
		class CBaseHandle {
		public:
			uint32_t m_Index;
			int GetEntryIndex() const {
				return this->m_Index & ENT_ENTRY_MASK;
			}

			int GetSerialNumber() const
			{
				return m_Index >> NUM_SERIAL_NUM_SHIFT_BITS;
			}
		};

		class CEntInfo
		{
		public:
			void* m_pEntity;
			int				m_SerialNumber;
			CEntInfo* m_pPrev;
			CEntInfo* m_pNext;
			char*		m_iName;
			char*		m_iClassName;
		};

		// How many bits to use to encode an edict.
#define	MAX_EDICT_BITS				11			// # of bits needed to represent max edicts
// Max # of edicts in a level
#define	MAX_EDICTS					(1<<MAX_EDICT_BITS)
#define NUM_ENT_ENTRY_BITS		(MAX_EDICT_BITS + 2)
#define NUM_ENT_ENTRIES			(1 << NUM_ENT_ENTRY_BITS)
		// This is actually CBaseEntityList. Pretty sure NUM_ENT_ENTRIES is too big.
		class CGlobalEntityList {
		public:
			char pad_0000[4];
			CEntInfo m_EntPtrArray[NUM_ENT_ENTRIES];

			void *LookupEntity(const CBaseHandle *handle) const {
				const CEntInfo* pInfo = &m_EntPtrArray[handle->GetEntryIndex()];
				if (pInfo->m_SerialNumber == handle->GetSerialNumber())
					return pInfo->m_pEntity;
				else
					return NULL;
			}
		};

		class CINFRA_Player
		{
		public:
			char pad_0000[44]; //0x0000
			float m_vecOrigin[3]; //0x002C - world position X/Y/Z (found via SILTA
			                      // calibrate; matches getpos X/Y, Z is feet not eyes)
			char pad_0038[1944]; //0x0038
			class CBaseHandle m_Weapon0; //0x07D0
			class CBaseHandle m_Weapon1; //0x07D4
			class CBaseHandle m_Weapon2; //0x07D8
			char pad_07DC[3804]; //0x07DC
			void* m_Shared; //0x16B8
			char pad_16BC[400]; //0x16BC
			int32_t m_nFlashlightCharge; //0x184C - live charge of the current battery,
			                             // 0..100 percent (found via SILTA autoscan)
			int32_t m_nFlashlightBatteries; //0x1850
			int32_t m_nCameraBatteries; //0x1854
		}; //Size: 0x1858

		class CUtlMemory
		{
		public:
			void* m_pMemory; //0x0000
			int32_t m_nAllocationCount; //0x0004
			int32_t m_nGrowSize; //0x0008
		}; //Size: 0x000C

		template <typename T = void>
		class CUtlVector : public CUtlMemory
		{
		public:
			int32_t m_Size; //0x000C
			T *m_pElements; //0x0010
		}; //Size: 0x0014

		class CUtlHandleTable
		{
		public:
			uint32_t m_nValidHandles; //0x0000
			class CUtlVector<> m_list; //0x0004
		}; //Size: 0x0018

		class CVGui
		{
		public:
			char pad_0000[4]; //0x0000
			class CUtlHandleTable m_handleTable; //0x0004
		}; //Size: 0x001C
		class VPanel
		{
		public:
			char pad_0000[4]; //0x0000
			class CUtlVector<> _childDar; //0x0004
			void* _parent; //0x0018
			void* _plat; //0x001C
			uint64_t _hPanel; //0x0020
			void* _clientPanel; //0x0028
			int16_t _pos0; //0x002C
			int16_t _pos1; //0x002E
			int16_t _size0; //0x0030
			int16_t _size1; //0x0032
			int16_t _minimumSize0; //0x0034
			int16_t _minimumSize1; //0x0036
			int16_t _inset0; //0x0038
			int16_t _inset1; //0x003A
			int16_t _inset2; //0x003C
			int16_t _inset3; //0x003E
			int16_t _clipRect0; //0x0040
			int16_t _clipRect1; //0x0042
			int16_t _clipRect2; //0x0044
			int16_t _clipRect3; //0x0046
			int16_t _absPos0; //0x0048
			int16_t _absPos1; //0x004A
			int16_t _zpos; //0x004C
			char pad_004E[10]; //0x004E
			void* _pinsibling; //0x0058
			char pad_005C[4]; //0x005C
		}; //Size: 0x0060

		class CHudElement
		{
		public:
			char pad_0000[24]; //0x0000
			char* clazz; //0x0018
			char pad_001C[56]; //0x001C
			char* name; //0x0054
		}; //Size: 0x0058

		class CHud
		{
		public:
			char pad_0000[4]; //0x0000
			int32_t m_iKeyBits; //0x0004
			float m_flMouseSensitivity; //0x0008
			float m_flMouseSentitivityFactor; //0x000C
			float m_flFOVSensitivityAdjust; //0x0010
			int32_t m_clrNormal; //0x0014
			int32_t m_clrCaution; //0x0018
			int32_t m_clrYellowish; //0x001C
			class CUtlVector<CHudElement *> m_hudList; //0x0020
			class CUtlVector<> m_hudPanelList; //0x0034
			char pad_0048[9184]; //0x0048
		};

		class CCameraAmmo
		{
		public:
			char pad_0000[436]; //0x0000
			int32_t count; //0x01B4
		}; //Size: 0x01B8

		class CFlashlightAmmo
		{
		public:
			char pad_0000[348]; //0x0000
			int32_t count; //0x015C
		}; //Size: 0x0160
	}

	// Functions reverse-engineered from Infra / its version of the Source engine.
	namespace functions {
		using namespace infra::structs;

		typedef enum { GLOBAL_OFF = 0, GLOBAL_ON = 1, GLOBAL_DEAD = 2 } GLOBALESTATE;

		typedef int(__cdecl* GlobalEntity_AddEntity_t)(const char* pGlobalname, const char* pMapName, GLOBALESTATE state);
		typedef void(__cdecl* GlobalEntity_SetCounter_t)(int globalIndex, int counter);
		typedef int(__cdecl* GlobalEntity_GetCounter_t)(int globalIndex);
		typedef int(__cdecl* GlobalEntity_AddToCounter_t)(int globalIndex, int count);
		typedef GLOBALESTATE(__cdecl* GlobalEntity_GetState_t)(int globalIndex);
		typedef void(__cdecl* GlobalEntity_SetState_t)(int globalIndex, GLOBALESTATE state);
		typedef void* (__cdecl* GetPlayerByIndex_t)(int a1);

		typedef int(__thiscall* KeyValues__GetInt_t)(void* thiz, const char* key, int defVal);
		typedef CBaseEntity* (__thiscall* CGlobalEntityList__FindEntityByName_t)(void* thiz, CBaseEntity *pStartEntity, const char* szName, CBaseEntity* pSearchingEntity,
			CBaseEntity* pActivator, CBaseEntity* pCaller, void* pFilter);
	}

	using namespace structs;
	using namespace functions;

	class InfraEngine {
	public:
		InfraEngine();
		~InfraEngine();

		_HOOKER_PROTOTYPE(client)
		_HOOKER_PROTOTYPE(server)

		_GETTER_PROTOTYPE(client)
		_GETTER_PROTOTYPE(server)
		_GETTER_PROTOTYPE(engine)
		_GETTER_PROTOTYPE(vguimatsurface)
		_GETTER_PROTOTYPE(materialsystem)

		// Specific functions
		bool is_in_main_menu();
		bool loading_screen_visible();
		const char* get_map_name();

		// GlobalEntity functions
		int GlobalEntity_AddEntity(const char* pGlobalname, const char* pMapName, GLOBALESTATE state) const;
		void GlobalEntity_SetCounter(int globalIndex, int counter) const;
		int GlobalEntity_GetCounter(int globalIndex) const;
		int GlobalEntity_AddToCounter(int globalIndex, int count) const;
		int GlobalEntity_GetState(int globalIndex) const;
		void GlobalEntity_SetState(int globalIndex, GLOBALESTATE state) const;

		// MaterialSystem functions
		CMatSystemTexture* MaterialSystem_GetTextureById(int id) const;

		// VGui functions
		CVGui* VGui() const;
		CHud* Hud() const;

		// Other stuff
		int KeyValues__GetInt(void* lpKeyValues, const char* name, int defaultValue) const;

		// Server entity stuff
		CGlobalEntityList *server_entity_list() const;
		CBaseEntity* CGlobalEntityList__FindEntityByName(CBaseEntity* pStartEntity, const char* szName,
		                                                 CBaseEntity* pSearchingEntity = nullptr, CBaseEntity* pActivator = nullptr,
		                                                 CBaseEntity* pCaller = nullptr, void* pFilter = nullptr) const;
	private:
		std::vector<void*> enabledHooks;
		void* engine_base;
		void* server_base;
		void* client_base;
		void* vguimatsurface_base;
		void* materialsystem_base;

		GlobalEntity_AddEntity_t pGlobalEntityAddEntity;
		GlobalEntity_SetCounter_t pGlobalEntitySetCounter;
		GlobalEntity_GetCounter_t pGlobalEntityGetCounter;
		GlobalEntity_AddToCounter_t pGlobalEntityAddToCounter;
		GlobalEntity_GetState_t pGlobalEntityGetState;
		GlobalEntity_SetState_t pGlobalEntitySetState;

		KeyValues__GetInt_t pKeyValuesGetInt;
		CGlobalEntityList__FindEntityByName_t pFindEntityByName;
	};

	InfraEngine* Engine();
}

#undef _HOOKER_PROTOTYPE
#undef _GETTER_PROTOTYPE