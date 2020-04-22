/* $Id$ */

/*
 *  Copyright (c) 2003-2009 Axel Andersson
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

#include <wired/wired.h>

#include "accounts.h"
#include "files.h"
#include "index.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"

#define WD_TRACKERS_REGISTER_INTERVAL	3600.0
#define WD_TRACKERS_UPDATE_INTERVAL		60.0


struct _wd_tracker {
	wi_runtime_base_t					base;
	
	wi_lock_t							*register_lock;

	wi_boolean_t						active;

	wi_cipher_t							*cipher;
	wi_address_t						*address;

	wi_array_t							*addresses;

	wi_string_t							*host;
	wi_string_t							*user;
	wi_string_t							*password;
	wi_string_t							*category;
};
typedef struct _wd_tracker				wd_tracker_t;


static void								wd_trackers_register_with_timer(wi_timer_t *);
static void								wd_trackers_update_with_timer(wi_timer_t *);
static void								wd_trackers_register_thread(wi_runtime_instance_t *);
static void								wd_trackers_update(void);

static wd_tracker_t *					wd_tracker_alloc(void);
static wd_tracker_t *					wd_tracker_init_with_url(wd_tracker_t *, wi_url_t *);
static void								wd_tracker_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_tracker_description(wi_runtime_instance_t *);

static void								wd_tracker_register(wd_tracker_t *);
static void								wd_tracker_update(wd_tracker_t *);

static wi_p7_message_t *				wd_tracker_read_message(wd_tracker_t *, wi_p7_socket_t *);
static wi_boolean_t						wd_tracker_write_message(wd_tracker_t *, wi_p7_socket_t *, wi_p7_message_t *);


static wi_array_t						*wd_trackers;

static wi_timer_t						*wd_trackers_register_timer;
static wi_timer_t						*wd_trackers_update_timer;

static wi_runtime_id_t					wd_tracker_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_tracker_runtime_class = {
	"wd_tracker_t",
	wd_tracker_dealloc,
	NULL,
	NULL,
	wd_tracker_description,
	NULL
};



void wd_trackers_initialize(void) {
	wd_tracker_runtime_id = wi_runtime_register_class(&wd_tracker_runtime_class);

	wd_trackers = wi_array_init(wi_mutable_array_alloc());

	wd_trackers_register_timer =
		wi_timer_init_with_function(wi_timer_alloc(),
									wd_trackers_register_with_timer,
									WD_TRACKERS_REGISTER_INTERVAL,
									true);
	
	wd_trackers_update_timer =
		wi_timer_init_with_function(wi_timer_alloc(),
									wd_trackers_update_with_timer,
									WD_TRACKERS_UPDATE_INTERVAL,
									true);
}



void wd_trackers_apply_settings(wi_set_t *changes) {
	wi_enumerator_t		*enumerator;
	wi_array_t			*trackers;
	wi_string_t			*string;
	wi_url_t			*url;
	wd_tracker_t		*tracker;
	wi_boolean_t		changed = false;
	
	if(wi_set_contains_data(changes, WI_STR("tracker"))) {
		wi_array_wrlock(wd_trackers);
		
		if(wi_array_count(wd_trackers) > 0) {
			wi_mutable_array_remove_all_data(wd_trackers);
			
			changed = true;
		}
		
		trackers = wi_config_stringlist_for_name(wd_config, WI_STR("tracker"));
		
		if(trackers) {
			enumerator = wi_array_data_enumerator(trackers);
			
			while((string = wi_enumerator_next_data(enumerator))) {
				url = wi_autorelease(wi_url_init_with_string(wi_url_alloc(), string));

				if(!wi_url_is_valid(url)) {
					wi_log_error(WI_STR("Could not parse tracker URL \"%@\""),
						string);
					
					continue;
				}

				tracker	= wi_autorelease(wd_tracker_init_with_url(wd_tracker_alloc(), url));
				
				if(!tracker)
					continue;
				
				wi_mutable_array_add_data(wd_trackers, tracker);
			}
		}
		
		wi_array_unlock(wd_trackers);
	}
		
	if(changed)
		wd_trackers_register();
}



void wd_trackers_schedule(void) {
	if(wi_config_bool_for_name(wd_config, WI_STR("register"))) {
		wi_timer_schedule(wd_trackers_register_timer);
		wi_timer_schedule(wd_trackers_update_timer);
	} else {
		wi_timer_invalidate(wd_trackers_register_timer);
		wi_timer_invalidate(wd_trackers_update_timer);
	}
}



#pragma mark -

static void wd_trackers_register_with_timer(wi_timer_t *timer) {
	wd_trackers_register();
}



void wd_trackers_register(void) {
	if(wi_config_bool_for_name(wd_config, WI_STR("register")) && wi_array_count(wd_trackers) > 0) {
		wi_log_info(WI_STR("Registering with trackers..."));

		if(!wi_thread_create_thread(wd_trackers_register_thread, NULL))
			wi_log_error(WI_STR("Could not create a register thread: %m"));
	}
}



static void wd_trackers_register_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wi_enumerator_t		*enumerator;
	wd_tracker_t		*tracker;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	wi_array_rdlock(wd_trackers);
	
	enumerator = wi_array_data_enumerator(wd_trackers);
	
	while((tracker = wi_enumerator_next_data(enumerator)))
		wd_tracker_register(tracker);
	
	wi_array_unlock(wd_trackers);
	
	wi_release(pool);
}



static void wd_trackers_update_with_timer(wi_timer_t *timer) {
	wd_trackers_update();
}



static void wd_trackers_update(void) {
	wi_enumerator_t		*enumerator;
	wd_tracker_t		*tracker;
	
	wi_array_rdlock(wd_trackers);
	
	enumerator = wi_array_data_enumerator(wd_trackers);
	
	while((tracker = wi_enumerator_next_data(enumerator))) {
		if(tracker->active)
			wd_tracker_update(tracker);
	}

	wi_array_unlock(wd_trackers);
}



#pragma mark -

static wd_tracker_t * wd_tracker_alloc(void) {
	return wi_runtime_create_instance(wd_tracker_runtime_id, sizeof(wd_tracker_t));
}



static wd_tracker_t * wd_tracker_init_with_url(wd_tracker_t *tracker, wi_url_t *url) {
	wi_enumerator_t		*enumerator;
	wi_address_t		*address;
	wi_string_t			*path, *user, *password;
	wi_uinteger_t		port;
	
	path = wi_url_path(url);
	
	if(!path || wi_string_length(path) == 0)
		path = WI_STR("/");
	
	user = wi_url_user(url);
	
	if(!user || wi_string_length(user) == 0)
		user = WI_STR("guest");

	password = wi_url_password(url);
	
	if(!password)
		password = WI_STR("");
	
	if(wi_string_length(password) != 40)
		password = wi_string_sha1(password);
	
	tracker->host		= wi_retain(wi_url_host(url));
	tracker->user		= wi_retain(user);
	tracker->password	= wi_retain(password);
	tracker->category	= wi_retain(wi_string_substring_from_index(path, 1));
	tracker->addresses	= wi_retain(wi_host_addresses(wi_host_with_string(tracker->host)));
	
	if(!tracker->addresses) {
		wi_log_error(WI_STR("Could not resolve \"%@\": %m"), tracker->host);
		
		wi_release(tracker);
		
		return NULL;
	}
	
	port = wi_url_port(url);
	
	if(port == 0)
		port = WD_SERVER_PORT;
	
	enumerator = wi_array_data_enumerator(tracker->addresses);
	
	while((address = wi_enumerator_next_data(enumerator)))
		wi_address_set_port(address, port);
	
	tracker->register_lock = wi_lock_init(wi_lock_alloc());
	
	return tracker;
}



static void wd_tracker_dealloc(wi_runtime_instance_t *instance) {
	wd_tracker_t		*tracker = instance;

	wi_release(tracker->register_lock);

	wi_release(tracker->cipher);
	wi_release(tracker->address);

	wi_release(tracker->addresses);
	
	wi_release(tracker->host);
	wi_release(tracker->user);
	wi_release(tracker->password);
	wi_release(tracker->category);
}



static wi_string_t * wd_tracker_description(wi_runtime_instance_t *instance) {
	wd_tracker_t		*tracker = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{host = %@, category = %@, active = %d}"),
		wi_runtime_class_name(tracker),
		tracker,
		tracker->host,
		tracker->category,
		tracker->active);
}




#pragma mark -

static void wd_tracker_register(wd_tracker_t *tracker) {
	wi_pool_t			*pool;
	wi_enumerator_t		*enumerator;
	wi_p7_socket_t		*p7_socket;
	wi_p7_message_t		*message;
	wi_address_t		*address;
	wi_socket_t			*socket;
	wi_string_t			*string, *ip, *error;
	
	if(!wi_lock_trylock(tracker->register_lock))
		return;
	
	enumerator = wi_array_data_enumerator(tracker->addresses);
	
	pool = wi_pool_init(wi_pool_alloc());

	while((address = wi_enumerator_next_data(enumerator))) {
		wi_pool_drain(pool);
		
		tracker->active		= false;
		tracker->address	= NULL;
		ip					= wi_address_string(address);
		
		socket = wi_autorelease(wi_socket_init_with_address(wi_socket_alloc(), address, WI_SOCKET_TCP));

		if(!socket) {
			wi_log_error(WI_STR("Could not create socket for tracker \"%@\": %m"),
				tracker->host);
			
			continue;
		}

		if(!wi_socket_connect(socket, 30.0)) {
			wi_log_error(WI_STR("Could not connect to tracker \"%@\": %m"),
				tracker->host);
			
			continue;
		}
		
		p7_socket = wi_autorelease(wi_p7_socket_init_with_socket(wi_p7_socket_alloc(), socket, wd_p7_spec));
		
		if(!wi_p7_socket_connect(p7_socket,
								 30.0,
								 WI_P7_COMPRESSION_DEFLATE | WI_P7_ENCRYPTION_RSA_AES256_SHA1 | WI_P7_CHECKSUM_SHA1,
								 WI_P7_BINARY,
								 tracker->user,
								 tracker->password)) {
			wi_log_error(WI_STR("Could not connect to tracker \"%@\": %m"),
				tracker->host);
			
			continue;
		}
		
		if(!wd_tracker_write_message(tracker, p7_socket, wd_client_info_message()))
			continue;
		
		message = wd_tracker_read_message(tracker, p7_socket);
		
		if(!message)
			continue;
		
		if(!wi_is_equal(wi_p7_message_name(message), WI_STR("wired.server_info"))) {
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received unexpected message \"%@\" (expected wired.server_info)"),
				tracker->host, wi_p7_message_name(message));
			
			break;
		}

		message = wi_p7_message_with_name(WI_STR("wired.user.set_nick"), wd_p7_spec);
		wi_p7_message_set_string_for_name(message, wi_config_string_for_name(wd_config, WI_STR("name")), WI_STR("wired.user.nick"));
		
		if(!wd_tracker_write_message(tracker, p7_socket, message))
			break;
		
		message = wd_tracker_read_message(tracker, p7_socket);
		
		if(!message)
			break;
		
		message = wi_p7_message_with_name(WI_STR("wired.send_login"), wd_p7_spec);
		wi_p7_message_set_string_for_name(message, tracker->user, WI_STR("wired.user.login"));
		wi_p7_message_set_string_for_name(message, tracker->password, WI_STR("wired.user.password"));
		
		if(!wd_tracker_write_message(tracker, p7_socket, message))
			break;

		message = wd_tracker_read_message(tracker, p7_socket);
		
		if(!message)
			break;
		
		if(wi_is_equal(wi_p7_message_name(message), WI_STR("wired.error"))) {
			error = wi_p7_message_enum_name_for_name(message, WI_STR("wired.error"));
			
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received error \"%@\""),
				tracker->host, error);
			
			break;
		}
		else if(!wi_is_equal(wi_p7_message_name(message), WI_STR("wired.login"))) {
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received unexpected message \"%@\" (expected wired.login)"),
				tracker->host, wi_p7_message_name(message));
			
			break;
		}

		message = wd_tracker_read_message(tracker, p7_socket);
		
		if(!message)
			break;
		
		if(wi_is_equal(wi_p7_message_name(message), WI_STR("wired.error"))) {
			error = wi_p7_message_enum_name_for_name(message, WI_STR("wired.error"));
			
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received error \"%@\""),
				tracker->host, error);
			
			break;
		}
		else if(!wi_is_equal(wi_p7_message_name(message), WI_STR("wired.account.privileges"))) {
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received unexpected message \"%@\" (expected wired.account.privileges)"),
				tracker->host, wi_p7_message_name(message));
			
			break;
		}
		
		message = wi_p7_message_with_name(WI_STR("wired.tracker.send_register"), wd_p7_spec);
		wi_p7_message_set_bool_for_name(message, wi_config_bool_for_name(wd_config, WI_STR("enable tracker")), WI_STR("wired.tracker.tracker"));
		wi_p7_message_set_string_for_name(message, tracker->category, WI_STR("wired.tracker.category"));
		wi_p7_message_set_uint32_for_name(message, wd_port, WI_STR("wired.tracker.port"));
		wi_p7_message_set_uint32_for_name(message, wi_array_count(wd_chat_users(wd_public_chat)), WI_STR("wired.tracker.users"));
		wi_p7_message_set_string_for_name(message, wi_config_string_for_name(wd_config, WI_STR("name")), WI_STR("wired.info.name"));
		wi_p7_message_set_string_for_name(message, wi_config_string_for_name(wd_config, WI_STR("description")), WI_STR("wired.info.description"));
		wi_p7_message_set_uint64_for_name(message, wd_index_files_count, WI_STR("wired.info.files.count"));
		wi_p7_message_set_uint64_for_name(message, wd_index_files_size, WI_STR("wired.info.files.size"));
		
		string = wi_config_string_for_name(wd_config, WI_STR("ip"));
		
		if(string)
			wi_p7_message_set_string_for_name(message, string, WI_STR("wired.tracker.ip"));
		
		if(!wd_tracker_write_message(tracker, p7_socket, message))
			break;
		
		message = wd_tracker_read_message(tracker, p7_socket);
		
		if(!message)
			break;
		
		if(wi_is_equal(wi_p7_message_name(message), WI_STR("wired.error"))) {
			error = wi_p7_message_enum_name_for_name(message, WI_STR("wired.error"));
			
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received error \"%@\""),
				tracker->host, error);
			
			break;
		}
		else if(!wi_is_equal(wi_p7_message_name(message), WI_STR("wired.okay"))) {
			wi_log_error(WI_STR("Could not register with tracker \"%@\": Received unexpected message \"%@\" (expected wired.okay)"),
				tracker->host, wi_p7_message_name(message));
			
			break;
		}
		
		wi_release(tracker->cipher);
		tracker->cipher = wi_retain(wi_p7_socket_cipher(p7_socket));
		
		wi_release(tracker->address);
		tracker->address = wi_retain(address);
		
		wi_log_info(WI_STR("Registered with the tracker \"%@\""),
			tracker->host);
		
		tracker->active = true;
		
		break;
	}
	
	wi_release(pool);
	
	wi_lock_unlock(tracker->register_lock);
}



static void wd_tracker_update(wd_tracker_t *tracker) {
	wi_p7_message_t		*message;
	wi_socket_t			*socket;
	wi_data_t			*data;
	wi_integer_t		bytes;
	
	socket = wi_autorelease(wi_socket_init_with_address(wi_socket_alloc(), tracker->address, WI_SOCKET_UDP));
	
	if(!socket) {
		wi_log_error(WI_STR("Could not create a socket for tracker \"%@\": %m"),
			tracker->host);
		
		return;
	}
	
	message = wi_p7_message_with_name(WI_STR("wired.tracker.send_update"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(message, wi_array_count(wd_chat_users(wd_public_chat)), WI_STR("wired.tracker.users"));
	wi_p7_message_set_uint64_for_name(message, wd_index_files_count, WI_STR("wired.info.files.count"));
	wi_p7_message_set_uint64_for_name(message, wd_index_files_size, WI_STR("wired.info.files.size"));

	data = wi_p7_message_data_with_serialization(message, WI_P7_BINARY);
	
	if(tracker->cipher) {
		data = wi_cipher_encrypt(tracker->cipher, data);
	
		if(!data) {
			wi_log_error(WI_STR("Could not encrypt message to tracker \"%@\": %m"),
				tracker->host);
			
			return;
		}
	}
	
	bytes = wi_socket_sendto_data(socket, data);
	
	if(bytes < 0) {
		wi_log_error(WI_STR("Could not send message to tracker \"%@\": %m"),
			tracker->host);
		
		return;
	}
}



#pragma mark -

static wi_p7_message_t * wd_tracker_read_message(wd_tracker_t *tracker, wi_p7_socket_t *p7_socket) {
	wi_p7_message_t		*message;
	
	message = wi_p7_socket_read_message(p7_socket, 30.0);
	
	if(!message) {
		wi_log_error(WI_STR("Could not read message from tracker \"%@\": %m"),
			tracker->host);
		
		return NULL;
	}
	
	if(!wi_p7_spec_verify_message(wd_p7_spec, message)) {
		wi_log_error(WI_STR("Could not verify message from tracker \"%@\": %m"),
			tracker->host);
		
		return NULL;
	}
	
	return message;
}



static wi_boolean_t wd_tracker_write_message(wd_tracker_t *tracker, wi_p7_socket_t *p7_socket, wi_p7_message_t *message) {
	if(!wi_p7_socket_write_message(p7_socket, 30.0, message)) {
		wi_log_error(WI_STR("Could not write message to tracker \"%@\": %m"),
			tracker->host);
		
		return false;
	}
	
	return true;
}
