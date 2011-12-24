/* $Id$ */

/*
 *  Copyright (c) 2004-2009 Axel Andersson
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <wired/wired.h>

#include "main.h"
#include "server.h"
#include "servers.h"
#include "settings.h"
#include "users.h"

#define WD_SERVERS_UPDATE_INTERVAL		60.0
#define WD_SERVERS_MIN_UPDATE_INTERVAL	360.0


struct _wd_server {
	wi_runtime_base_t					base;
	
	wi_boolean_t						active;
	
	wi_string_t							*ip;
	wi_time_interval_t					register_time;
	wi_time_interval_t					update_time;
	
	wi_cipher_t							*cipher;
	
	wi_boolean_t						tracker;
	wi_string_t							*category;
	wi_mutable_string_t					*url;
	wi_string_t							*display_ip;
	wi_p7_uint32_t						port;
	wi_string_t							*name;
	wi_string_t							*description;
	wi_p7_uint32_t						users;
	wi_p7_uint64_t						files_count;
	wi_p7_uint64_t						files_size;
};


static void								wd_servers_create_tables(void);
static void								wd_servers_load_servers(void);
static void								wd_servers_update_servers(wi_timer_t *);

static void								wd_servers_add_server(wd_server_t *);
static void								wd_servers_remove_server(wd_server_t *);
static void								wd_servers_add_stats_for_server(wd_server_t *);
static void								wd_servers_remove_stats_for_server(wd_server_t *);

static wd_server_t *					wd_server_alloc(void);
static wd_server_t *					wd_server_init_with_sqlite3_results(wd_server_t *, wi_dictionary_t *);
static wd_server_t *					wd_server_init_with_message(wd_server_t *, wi_p7_message_t *);
static void								wd_server_dealloc(wi_runtime_instance_t *);


static wi_timer_t						*wd_servers_timer;

static wi_mutable_dictionary_t			*wd_servers;

static wi_runtime_id_t					wd_server_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_server_runtime_class = {
	"wd_server_t",
	wd_server_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};



void wd_servers_initialize(void) {
	wd_servers_create_tables();
	
	wi_fs_delete_path(WI_STR("servers"));
	
	wd_server_runtime_id = wi_runtime_register_class(&wd_server_runtime_class);

	wd_servers = wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	wd_servers_timer = wi_timer_init_with_function(wi_timer_alloc(),
												   wd_servers_update_servers,
												   WD_SERVERS_UPDATE_INTERVAL,
												   true);
	
	wd_servers_load_servers();
}



void wd_servers_schedule(void) {
	wi_timer_schedule(wd_servers_timer);
}



#pragma mark -

static void wd_servers_create_tables(void) {
	wi_uinteger_t		version;
	
	version = wd_database_version_for_table(WI_STR("servers"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE servers ( "
																 "ip TEXT PRIMARY KEY NOT NULL, "
																 "port INTEGER NOT NULL, "
																 "cipher INTEGER, "
																 "key BLOB, "
																 "iv BLOB "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("servers"));
}



static void wd_servers_load_servers(void) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wd_server_t					*server;
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT ip, port, cipher, key, iv FROM servers"), NULL);
	
	if(!statement)
		wi_log_fatal(WI_STR("Could not execute database statement: %m"));
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		server = wi_autorelease(wd_server_init_with_sqlite3_results(wd_server_alloc(), results));

		if(server)
			wi_mutable_dictionary_set_data_for_key(wd_servers, server, server->ip);
	}
	
	if(!results)
		wi_log_fatal(WI_STR("Could not execute database statement: %m"));
}



static void wd_servers_update_servers(wi_timer_t *timer) {
	wi_enumerator_t		*enumerator;
	wd_server_t			*server;
	wi_time_interval_t	interval, update;
	wi_boolean_t		changed = false;

	wi_dictionary_rdlock(wd_servers);
		
	if(wi_dictionary_count(wd_servers) > 0) {
		interval = wi_time_interval();

		enumerator = wi_dictionary_data_enumerator(wd_servers);
		
		while((server = wi_enumerator_next_data(enumerator))) {
			if(server->active) {
				update = server->update_time > 0.0 ? server->update_time : server->register_time;
				
				if(interval - update > WD_SERVERS_MIN_UPDATE_INTERVAL) {
					if(server->update_time > 0.0) {
						wi_log_warn(WI_STR("Removing server \"%@\": Last update was %.0f seconds ago"),
							server->name, interval - update);
					} else {
						wi_log_warn(WI_STR("Removing server \"%@\": Never received an update"),
							server->name);
					}

					wd_servers_remove_stats_for_server(server);
					
					server->active	= false;
					changed			= true;
				}
			}
		}
	}

	wi_dictionary_unlock(wd_servers);

	if(changed) {
		wi_lock_lock(wd_status_lock);
		wd_write_status(true);
		wi_lock_unlock(wd_status_lock);
	}
}



#pragma mark -

static void wd_servers_add_server(wd_server_t *server) {
	wi_dictionary_wrlock(wd_servers);
	wi_mutable_dictionary_set_data_for_key(wd_servers, server, server->ip);
	wi_dictionary_unlock(wd_servers);
}



static void wd_servers_remove_server(wd_server_t *server) {
	wi_dictionary_wrlock(wd_servers);
	wi_mutable_dictionary_remove_data_for_key(wd_servers, server->ip);
	wi_dictionary_unlock(wd_servers);
}



static void wd_servers_add_stats_for_server(wd_server_t *server) {
	wi_lock_lock(wd_status_lock);
	wd_tracker_current_servers++;
	wd_tracker_current_users += server->users;
	wd_tracker_current_files += server->files_count;
	wd_tracker_current_size += server->files_size;
	wd_write_status(false);
	wi_lock_unlock(wd_status_lock);
}



static void wd_servers_remove_stats_for_server(wd_server_t *server) {
	wi_lock_lock(wd_status_lock);
	wd_tracker_current_servers--;
	wd_tracker_current_users -= server->users;
	wd_tracker_current_files -= server->files_count;
	wd_tracker_current_size -= server->files_size;
	wi_lock_unlock(wd_status_lock);
}



#pragma mark -

wd_server_t * wd_servers_server_for_ip(wi_string_t *ip) {
	wd_server_t		*server;

	wi_dictionary_rdlock(wd_servers);
	server = wi_autorelease(wi_retain(wi_dictionary_data_for_key(wd_servers, ip)));
	wi_dictionary_unlock(wd_servers);
	
	return server;
}



#pragma mark -

void wd_servers_register_server(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_address_t			*address;
	wi_array_t				*categories;
	wi_dictionary_t			*results;
	wd_server_t				*server, *existing_server;
	
	address					= wi_socket_address(wd_user_socket(user));
	server					= wi_autorelease(wd_server_init_with_message(wd_server_alloc(), message));
	server->url				= wi_string_init_with_format(wi_mutable_string_alloc(), WI_STR("wired://"));
	server->register_time	= wi_time_interval();
	server->ip				= wi_retain(wi_address_string(address));
	
	if(!server->display_ip)
		server->display_ip	= wi_retain(server->ip);
	
	if(wi_address_family(address) == WI_ADDRESS_IPV6)
		wi_mutable_string_append_format(server->url, WI_STR("[%@]"), server->display_ip);
	else
		wi_mutable_string_append_string(server->url, server->display_ip);
	
	if(server->port != WD_SERVER_PORT)
		wi_mutable_string_append_format(server->url, WI_STR(":%u/"), server->port);
	else
		wi_mutable_string_append_string(server->url, WI_STR("/"));
	
	categories = wi_config_stringlist_for_name(wd_config, WI_STR("category"));
	
	if(wi_string_length(server->category) > 0 && !wi_array_contains_data(categories, server->category)) {
		wi_release(server->category);
		server->category = wi_retain(WI_STR(""));
	}
	
	server->cipher = wi_retain(wi_p7_socket_cipher(wd_user_p7_socket(user)));

	existing_server = wd_servers_server_for_ip(server->ip);
	
	if(existing_server) {
		wd_servers_remove_server(existing_server);
		wd_servers_remove_stats_for_server(existing_server);
	}
	
	wd_servers_add_server(server);
	wd_servers_add_stats_for_server(server);
	
	reply = wi_p7_message_with_name(WI_STR("wired.okay"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM servers WHERE ip = ?"), server->ip, NULL))
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO servers (ip, port, cipher, key, iv) VALUES (?, ?, ?, ?, ?)"),
										   server->ip,
										   WI_INT32(server->port),
										   server->cipher ? WI_INT32(wi_cipher_type(server->cipher)) : wi_null(),
										   server->cipher ? wi_cipher_key(server->cipher) : wi_null(),
										   server->cipher && wi_cipher_iv(server->cipher) ? wi_cipher_iv(server->cipher) : wi_null(),
										   NULL);
	
	if(!results)
		wi_log_error(WI_STR("Could not execute database statement: %m"));

	wi_lock_lock(wd_status_lock);
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);
}



wi_boolean_t wd_servers_update_server(wi_string_t *ip, wd_user_t *user, wi_p7_message_t *message) {
	wd_server_t			*server;
	
	server = wd_servers_server_for_ip(ip);
	
	if(!server || !server->active) {
		if(user)
			wd_user_reply_error(user, WI_STR("wired.error.not_registered"), message);
		
		return false;
	}
	
	wd_servers_remove_stats_for_server(server);

	server->update_time = wi_time_interval();

	wi_p7_message_get_uint32_for_name(message, &server->users, WI_STR("wired.tracker.users"));
	wi_p7_message_get_uint64_for_name(message, &server->files_count, WI_STR("wired.info.files.count"));
	wi_p7_message_get_uint64_for_name(message, &server->files_size, WI_STR("wired.info.files.size"));

	wd_servers_add_stats_for_server(server);

	wi_lock_lock(wd_status_lock);
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);
	
	return true;
}



void wd_servers_reply_categories(wd_user_t *user, wi_p7_message_t *message) {
	wi_array_t			*categories;
	wi_p7_message_t		*reply;
	
	categories = wi_config_stringlist_for_name(wd_config, WI_STR("category"));
	
	reply = wi_p7_message_with_name(WI_STR("wired.tracker.categories"), wd_p7_spec);
	wi_p7_message_set_list_for_name(reply, categories, WI_STR("wired.tracker.categories"));
	wd_user_reply_message(user, reply, message);
}



void wd_servers_reply_server_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*reply;
	wd_server_t			*server;
	
	wi_dictionary_rdlock(wd_servers);
	
	enumerator = wi_dictionary_data_enumerator(wd_servers);
	
	while((server = wi_enumerator_next_data(enumerator))) {
		if(server->active) {
			reply = wi_p7_message_with_name(WI_STR("wired.tracker.server_list"), wd_p7_spec);
			wi_p7_message_set_bool_for_name(reply, server->tracker, WI_STR("wired.tracker.tracker"));
			wi_p7_message_set_string_for_name(reply, server->category, WI_STR("wired.tracker.category"));
			wi_p7_message_set_string_for_name(reply, server->url, WI_STR("wired.tracker.url"));
			wi_p7_message_set_uint32_for_name(reply, server->users, WI_STR("wired.tracker.users"));
			wi_p7_message_set_string_for_name(reply, server->name, WI_STR("wired.info.name"));
			wi_p7_message_set_string_for_name(reply, server->description, WI_STR("wired.info.description"));
			wi_p7_message_set_uint64_for_name(reply, server->files_count, WI_STR("wired.info.files.count"));
			wi_p7_message_set_uint64_for_name(reply, server->files_size, WI_STR("wired.info.files.size"));
			wd_user_reply_message(user, reply, message);
		}
	}
	
	wi_dictionary_unlock(wd_servers);

	reply = wi_p7_message_with_name(WI_STR("wired.tracker.server_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



#pragma mark -

static wd_server_t * wd_server_alloc(void) {
	return wi_runtime_create_instance(wd_server_runtime_id, sizeof(wd_server_t));
}



static wd_server_t * wd_server_init_with_sqlite3_results(wd_server_t *server, wi_dictionary_t *results) {
	wi_runtime_instance_t		*cipher, *key, *iv;
	
	server->ip		= wi_retain(wi_dictionary_data_for_key(results, WI_STR("ip")));
	server->port	= wi_number_integer(wi_dictionary_data_for_key(results, WI_STR("port")));
	
	cipher			= wi_dictionary_data_for_key(results, WI_STR("cipher"));
	key				= wi_dictionary_data_for_key(results, WI_STR("key"));
	iv				= wi_dictionary_data_for_key(results, WI_STR("iv"));
	
	if(cipher != wi_null())
		server->cipher = wi_cipher_init_with_key(wi_cipher_alloc(), wi_number_integer(cipher), key, iv == wi_null() ? NULL : iv);
	
	return server;
}



static wd_server_t * wd_server_init_with_message(wd_server_t *server, wi_p7_message_t *message) {
	server->active			= true;
	server->display_ip		= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.tracker.ip")));
	server->category		= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.tracker.category")));
	server->name			= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.name")));
	server->description		= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.description")));

	wi_p7_message_get_bool_for_name(message, &server->tracker, WI_STR("wired.tracker.tracker"));
	wi_p7_message_get_uint32_for_name(message, &server->port, WI_STR("wired.tracker.port"));
	wi_p7_message_get_uint32_for_name(message, &server->users, WI_STR("wired.tracker.users"));
	wi_p7_message_get_uint64_for_name(message, &server->files_count, WI_STR("wired.info.files.count"));
	wi_p7_message_get_uint64_for_name(message, &server->files_size, WI_STR("wired.info.files.size"));
	
	return server;
}



static void wd_server_dealloc(wi_runtime_instance_t *instance) {
	wd_server_t		*server = instance;
	
	wi_release(server->ip);
	
	wi_release(server->cipher);
	
	wi_release(server->category);
	wi_release(server->display_ip);
	wi_release(server->url);
	wi_release(server->name);
	wi_release(server->description);
}



#pragma mark -

wi_boolean_t wd_server_is_active(wd_server_t *server) {
	return server->active;
}



wi_cipher_t * wd_server_cipher(wd_server_t *server) {
	return server->cipher;
}



wi_uinteger_t wd_server_port(wd_server_t *server) {
	return server->port;
}
