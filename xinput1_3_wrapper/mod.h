#pragma once
#include "plugin.h"

class FloorbSourcePlugin : public sdk::i_server_plugin_callbacks {
public:
	FloorbSourcePlugin();

	// https://github.com/hero622/photon/blob/cdb313fca8dd6a4e8f4d73bc803dbdadf04d8539/src/core/photon.h
	virtual bool load(sdk::create_interface_fn interface_factory, sdk::create_interface_fn game_server_factory);
	virtual void unload();
	virtual void pause();
	virtual void un_pause();
	virtual const char* get_plugin_description();
	virtual void level_init(char const* map_name);
	virtual void server_activate(void* edict_list, int edict_count, int client_max);
	virtual void game_frame(bool simulating);
	virtual void level_shutdown();
	virtual void client_fully_connect(void* edict);
	virtual void client_active(void* entity);
	virtual void client_disconnect(void* entity);
	virtual void client_put_in_server(void* entity, char const* playername);
	virtual void set_command_client(int index);
	virtual void client_settings_changed(void* edict);
	virtual int client_connect(bool* allow_connect, void* entity, const char* name, const char* address, char* reject, int maxrejectlen);
	virtual int client_command(void* entity, const void*& args);
	virtual int network_id_validated(const char* user_name, const char* network_id);
	virtual void on_query_cvar_value_finished(int cookie, void* player_entity, int status, const char* cvar_name, const char* cvar_value);
	virtual void on_edict_allocated(void* edict);
	virtual void on_edict_freed(const void* edict);
};