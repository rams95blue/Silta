#include "mod.h"
#include "stdafx.h"
#include "base.h"
#include "game/overlay.h"
#include <Windows.h>

FloorbSourcePlugin g_Mod;

// Engine interface factory, captured for the [tweaks] console-command runner.
sdk::create_interface_fn g_EngineFactory = nullptr;

expose_single_interface_globalvar(FloorbSourcePlugin, i_server_plugin_callbacks, interfaceversion_iserverplugincallbacks, g_Mod);


static HMODULE GetCurrentModule() {
	HMODULE hModule = NULL;

	GetModuleHandleEx(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		(LPCTSTR)GetCurrentModule,
		&hModule
	);

	return hModule;
}

FloorbSourcePlugin::FloorbSourcePlugin() {
}

bool FloorbSourcePlugin::load(sdk::create_interface_fn interface_factory, sdk::create_interface_fn game_server_factory) {
	g_EngineFactory = interface_factory;
	return true;
}


void FloorbSourcePlugin::unload() {
	overlay::SaveNotes();
	overlay::SketchSaveOnUnload();
	// Probably want to do Base::Data::Detached = true;
	Base::Hooks::Shutdown();
}

void FloorbSourcePlugin::pause() {}
void FloorbSourcePlugin::un_pause() {}
const char* FloorbSourcePlugin::get_plugin_description() {
	return "silta";
}

void FloorbSourcePlugin::level_init(char const* map_name) {
	if (!Base::Data::Inited) {
		Base::Data::hModule = GetCurrentModule();
		Base::Init();
		Base::Data::Inited = true;
	}
}

void FloorbSourcePlugin::server_activate(void* edict_list, int edict_count, int client_max) {}
void FloorbSourcePlugin::game_frame(bool simulating) {}
void FloorbSourcePlugin::level_shutdown() {}
void FloorbSourcePlugin::client_fully_connect(void* edict) {}
void FloorbSourcePlugin::client_active(void* entity) {}
void FloorbSourcePlugin::client_disconnect(void* entity) {}
void FloorbSourcePlugin::client_put_in_server(void* entity, char const* playername) {}
void FloorbSourcePlugin::set_command_client(int index) {}
void FloorbSourcePlugin::client_settings_changed(void* edict) {}
int FloorbSourcePlugin::client_connect(bool* allow_connect, void* entity, const char* name, const char* address, char* reject, int maxrejectlen) {
	return 0;
}
int FloorbSourcePlugin::client_command(void* entity, const void*& args) {
	return 0;
}
int FloorbSourcePlugin::network_id_validated(const char* user_name, const char* network_id) {
	return 0;
}
void FloorbSourcePlugin::on_query_cvar_value_finished(int cookie, void* player_entity, int status, const char* cvar_name, const char* cvar_value) {}
void FloorbSourcePlugin::on_edict_allocated(void* edict) {}
void FloorbSourcePlugin::on_edict_freed(const void* edict) {}