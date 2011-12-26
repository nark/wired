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

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <wired/wired.h>

#include "chats.h"
#include "events.h"
#include "server.h"
#include "settings.h"
#include "transfers.h"
#include "users.h"

#define WD_USERS_THREAD_KEY				"wd_user_t"

#define WD_USERS_IDLE_TIME				600.0
#define WD_USERS_TIMER_INTERVAL			60.0

#define WD_USER_SET_VALUE(user, dst, src)				\
	WI_STMT_START										\
		wi_recursive_lock_lock((user)->user_lock);		\
		(dst) = (src);									\
		wi_recursive_lock_unlock((user)->user_lock);	\
	WI_STMT_END

#define WD_USER_SET_INSTANCE(user, dst, src)			\
	WI_STMT_START										\
		wi_recursive_lock_lock((user)->user_lock);		\
		wi_retain((src));								\
		wi_release((dst));								\
		(dst) = (src);									\
		wi_recursive_lock_unlock((user)->user_lock);	\
	WI_STMT_END

#define WD_USER_RETURN_VALUE(user, src)					\
	WI_STMT_START										\
		typeof(src)		_value;							\
														\
		wi_recursive_lock_lock((user)->user_lock);		\
		_value = (src);									\
		wi_recursive_lock_unlock((user)->user_lock);	\
														\
		return _value;									\
	WI_STMT_END

#define WD_USER_RETURN_INSTANCE(user, src)				\
	WI_STMT_START										\
		typeof(src)		_instance;						\
														\
		wi_recursive_lock_lock((user)->user_lock);		\
		_instance = wi_autorelease(wi_retain((src)));	\
		wi_recursive_lock_unlock((user)->user_lock);	\
														\
		return _instance;								\
	WI_STMT_END



struct _wd_user {
	wi_runtime_base_t					base;
	
	wi_recursive_lock_t					*user_lock;
	wi_recursive_lock_t					*socket_lock;
	
	wi_socket_t							*socket;
	wi_p7_socket_t						*p7_socket;
	
	wd_uid_t							id;
	wd_user_state_t						state;
	wi_boolean_t						idle;
	
	wd_account_t						*account;

	wi_string_t							*nick;
	wi_string_t							*login;
	wi_string_t							*ip;
	wi_string_t							*host;
	wd_client_info_t					*client_info;
	wi_string_t							*status;
	wi_data_t							*icon;
	wi_p7_enum_t						color;
	
	wi_date_t							*login_time;
	wi_date_t							*idle_time;
	
	wi_boolean_t						joined_public_chat;
	
	wi_boolean_t						subscribed_boards;
	wi_boolean_t						subscribed_accounts;
	wi_boolean_t						subscribed_log;
	wi_boolean_t						subscribed_events;
	wi_mutable_set_t					*subscribed_paths;
	wi_mutable_dictionary_t				*subscribed_virtualpaths;
	
	wd_transfer_t						*transfer;
};


static void								wd_users_update_idle(wi_timer_t *);

static wd_user_t *						wd_user_alloc(void);
static wd_user_t *						wd_user_init_with_socket(wd_user_t *, wi_socket_t *);
static void								wd_user_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_user_description(wi_runtime_instance_t *);

static wd_uid_t							wd_user_next_id(void);


static wi_timer_t						*wd_users_timer;

static wd_uid_t							wd_users_current_id;
static wi_lock_t						*wd_users_id_lock;

wi_mutable_dictionary_t					*wd_users;

static wi_runtime_id_t					wd_user_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_user_runtime_class = {
	"wd_user_t",
	wd_user_dealloc,
	NULL,
	NULL,
	wd_user_description,
	NULL
};


struct _wd_client_info {
	wi_runtime_base_t					base;
	
	wi_string_t							*application_name;
	wi_string_t							*application_version;
	wi_string_t                         *application_build;
	wi_string_t							*os_name;
	wi_string_t							*os_version;
	wi_string_t							*arch;
	wi_boolean_t						supports_rsrc;
};


static wd_client_info_t *				wd_client_info_alloc(void);
static wd_client_info_t *				wd_client_info_init_with_message(wd_client_info_t *, wi_p7_message_t *);
static void								wd_client_info_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_client_info_description(wi_runtime_instance_t *);


static wi_runtime_id_t					wd_client_info_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_client_info_runtime_class = {
	"wd_client_info_t",
	wd_client_info_dealloc,
	NULL,
	NULL,
	wd_client_info_description,
	NULL
};



void wd_users_initialize(void) {
	wd_user_runtime_id = wi_runtime_register_class(&wd_user_runtime_class);
	wd_client_info_runtime_id = wi_runtime_register_class(&wd_client_info_runtime_class);

	wd_users = wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	wd_users_id_lock = wi_lock_init(wi_lock_alloc());
		
	wd_users_timer = wi_timer_init_with_function(wi_timer_alloc(),
												 wd_users_update_idle,
												 WD_USERS_TIMER_INTERVAL,
												 true);
}



void wd_users_schedule(void) {
	wi_timer_schedule(wd_users_timer);
}



static void wd_users_update_idle(wi_timer_t *timer) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wi_time_interval_t	interval;

	wi_dictionary_rdlock(wd_users);

	if(wi_dictionary_count(wd_users) > 0) {
		interval = wi_time_interval();

		enumerator = wi_dictionary_data_enumerator(wd_users);
		
		while((user = wi_enumerator_next_data(enumerator))) {
			wi_recursive_lock_lock(user->user_lock);
			
			if(user->state == WD_USER_LOGGED_IN && !user->idle &&
			   wi_date_time_interval(user->idle_time) + WD_USERS_IDLE_TIME < interval) {
				user->idle = true;
				
				wd_user_broadcast_status(user);
			}

			wi_recursive_lock_unlock(user->user_lock);
		}
	}
		
	wi_dictionary_unlock(wd_users);
}



#pragma mark -

void wd_users_add_user(wd_user_t *user) {
	wi_dictionary_wrlock(wd_users);
	wi_mutable_dictionary_set_data_for_key(wd_users, user, wi_number_with_int32(wd_user_id(user)));
	wi_dictionary_unlock(wd_users);
}



void wd_users_remove_user(wd_user_t *user) {
	wd_chats_remove_user(user);
	wd_transfers_remove_user(user, false);
	
	wd_user_unsubscribe_paths(user);
	
	wi_dictionary_wrlock(wd_users);
	wi_mutable_dictionary_remove_data_for_key(wd_users, wi_number_with_int32(wd_user_id(user)));
	wi_dictionary_unlock(wd_users);
}



void wd_users_remove_all_users(void) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;

	wi_dictionary_wrlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		wd_chats_remove_user(user);
		wd_transfers_remove_user(user, true);

		wd_user_unsubscribe_paths(user);
	}

	wi_mutable_dictionary_remove_all_data(wd_users);
	
	wi_dictionary_unlock(wd_users);
}



wd_user_t * wd_users_user_with_id(wd_uid_t id) {
	wd_user_t     *user;

	wi_dictionary_rdlock(wd_users);
	user = wi_autorelease(wi_retain(wi_dictionary_data_for_key(wd_users, wi_number_with_int32(id))));
	wi_dictionary_unlock(wd_users);
	
	return user;
}



wi_array_t * wd_users_users_with_login(wi_string_t *name) {
	wi_enumerator_t			*enumerator;
	wi_mutable_array_t		*users;
	wd_user_t				*user;
	
	users = wi_mutable_array();

	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wi_is_equal(wd_user_login(user), name))
			wi_mutable_array_add_data(users, user);
	}

	wi_dictionary_unlock(wd_users);
	
	return users;
}



void wd_users_reply_users(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t				*enumerator;
	wi_p7_message_t				*reply;
	wd_user_t					*peer;
	wd_user_protocol_state_t	state;
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		wi_recursive_lock_lock(peer->user_lock);
		
		switch(user->state) {
			default:
			case WD_USER_CONNECTED:			state = WD_USER_PROTOCOL_CONNECTED;		break;
			case WD_USER_GAVE_CLIENT_INFO:	state = WD_USER_PROTOCOL_LOGGING_IN;	break;
			case WD_USER_SAID_HELLO:		state = WD_USER_PROTOCOL_LOGGING_IN;	break;
			case WD_USER_GAVE_USER:			state = WD_USER_PROTOCOL_LOGGING_IN;	break;
			case WD_USER_LOGGED_IN:			state = WD_USER_PROTOCOL_LOGGED_IN;		break;
			case WD_USER_DISCONNECTED:		state = WD_USER_PROTOCOL_DISCONNECTING;	break;
		}
		
		reply = wi_p7_message_with_name(WI_STR("wired.user.user_list"), wd_p7_spec);
		wi_p7_message_set_uint32_for_name(reply, peer->id, WI_STR("wired.user.id"));
		wi_p7_message_set_bool_for_name(reply, peer->idle, WI_STR("wired.user.idle"));
		wi_p7_message_set_string_for_name(reply, peer->nick, WI_STR("wired.user.nick"));
		wi_p7_message_set_string_for_name(reply, peer->status, WI_STR("wired.user.status"));
		wi_p7_message_set_data_for_name(reply, peer->icon, WI_STR("wired.user.icon"));
		wi_p7_message_set_enum_for_name(reply, peer->color, WI_STR("wired.account.color"));
		wi_p7_message_set_date_for_name(reply, peer->idle_time, WI_STR("wired.user.idle_time"));
		
		if(peer->transfer) {
			state = WD_USER_PROTOCOL_TRANSFERRING;
			
			wi_p7_message_set_enum_for_name(reply, peer->transfer->type, WI_STR("wired.transfer.type"));
			wi_p7_message_set_string_for_name(reply, peer->transfer->path, WI_STR("wired.file.path"));
			wi_p7_message_set_uint64_for_name(reply, peer->transfer->datasize, WI_STR("wired.transfer.data_size"));
			wi_p7_message_set_uint64_for_name(reply, peer->transfer->rsrcsize, WI_STR("wired.transfer.rsrc_size"));
			wi_p7_message_set_uint64_for_name(reply, peer->transfer->transferred, WI_STR("wired.transfer.transferred"));
			wi_p7_message_set_uint32_for_name(reply, peer->transfer->speed, WI_STR("wired.transfer.speed"));
			wi_p7_message_set_uint32_for_name(reply, peer->transfer->queue, WI_STR("wired.transfer.queue_position"));
		}

		wi_p7_message_set_enum_for_name(reply, state, WI_STR("wired.user.state"));

		wi_recursive_lock_unlock(peer->user_lock);

		wd_user_reply_message(user, reply, message);
	}
	
	wi_dictionary_unlock(wd_users);
	
	reply = wi_p7_message_with_name(WI_STR("wired.user.user_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



#pragma mark -

wd_user_t * wd_user_with_p7_socket(wi_p7_socket_t *p7_socket) {
	wd_user_t		*user;
	
	user				= wd_user_init_with_socket(wd_user_alloc(), wi_p7_socket_socket(p7_socket));
	user->p7_socket		= wi_retain(p7_socket);

	return wi_autorelease(user);
}



#pragma mark -

static wd_user_t * wd_user_alloc(void) {
	return wi_runtime_create_instance(wd_user_runtime_id, sizeof(wd_user_t));
}



static wd_user_t * wd_user_init_with_socket(wd_user_t *user, wi_socket_t *socket) {
	wi_address_t	*address;

	user->id						= wd_user_next_id();
	user->socket					= wi_retain(socket);
	user->state						= WD_USER_CONNECTED;
	user->login_time				= wi_date_init(wi_date_alloc());
	user->idle_time					= wi_date_init(wi_date_alloc());
	
	address							= wi_socket_address(socket);
	user->ip						= wi_retain(wi_address_string(address));
	user->host						= wi_retain(wi_address_hostname(address));
	
	user->user_lock					= wi_recursive_lock_init(wi_recursive_lock_alloc());
	user->socket_lock				= wi_recursive_lock_init(wi_recursive_lock_alloc());
	
	user->subscribed_paths			= wi_set_init_with_capacity(wi_mutable_set_alloc(), 0, true);
	user->subscribed_virtualpaths	= wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	return user;
}



static void wd_user_dealloc(wi_runtime_instance_t *instance) {
	wd_user_t		*user = instance;
	
	wi_release(user->user_lock);
	wi_release(user->socket_lock);

	wi_release(user->socket);
	wi_release(user->p7_socket);
	
	wi_release(user->account);
	
	wi_release(user->nick);
	wi_release(user->login);
	wi_release(user->ip);
	wi_release(user->host);
	wi_release(user->client_info);
	wi_release(user->status);
	wi_release(user->icon);

	wi_release(user->idle_time);
	wi_release(user->login_time);

	wi_release(user->transfer);
	
	wi_release(user->subscribed_paths);
	wi_release(user->subscribed_virtualpaths);
}



static wi_string_t * wd_user_description(wi_runtime_instance_t *instance) {
	wd_user_t		*user = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{uid = %u, nick = %@, login = %@, ip = %@}"),
		wi_runtime_class_name(user),
		user,
		user->id,
		user->nick,
		user->login,
		user->ip);
}



#pragma mark -

static wd_uid_t wd_user_next_id(void) {
	wd_uid_t	id;
	
	wi_lock_lock(wd_users_id_lock);
	
	id = ++wd_users_current_id;

	wi_lock_unlock(wd_users_id_lock);

	return id;
}



#pragma mark -

void wd_user_reply_user_info(wd_user_t *peer, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_cipher_t			*cipher;
	
	wi_recursive_lock_lock(peer->user_lock);
	
	reply = wi_p7_message_with_name(WI_STR("wired.user.info"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, peer->id, WI_STR("wired.user.id"));
	wi_p7_message_set_bool_for_name(reply, peer->idle, WI_STR("wired.user.idle"));
	wi_p7_message_set_string_for_name(reply, peer->login, WI_STR("wired.user.login"));
	wi_p7_message_set_string_for_name(reply, peer->nick, WI_STR("wired.user.nick"));
	wi_p7_message_set_string_for_name(reply, peer->status, WI_STR("wired.user.status"));
	wi_p7_message_set_data_for_name(reply, peer->icon, WI_STR("wired.user.icon"));
	wi_p7_message_set_enum_for_name(reply, peer->color, WI_STR("wired.account.color"));
	wi_p7_message_set_string_for_name(reply, peer->ip, WI_STR("wired.user.ip"));
	wi_p7_message_set_string_for_name(reply, peer->host, WI_STR("wired.user.host"));
	wi_p7_message_set_date_for_name(reply, peer->login_time, WI_STR("wired.user.login_time"));
	wi_p7_message_set_date_for_name(reply, peer->idle_time, WI_STR("wired.user.idle_time"));
	
	cipher = wi_p7_socket_cipher(peer->p7_socket);
	
	if(cipher) {
		wi_p7_message_set_string_for_name(reply, wi_cipher_name(cipher), WI_STR("wired.user.cipher.name"));
		wi_p7_message_set_uint32_for_name(reply, wi_cipher_bits(cipher), WI_STR("wired.user.cipher.bits"));
	}
	
	wi_p7_message_set_string_for_name(reply, peer->client_info->application_name, WI_STR("wired.info.application.name"));
	wi_p7_message_set_string_for_name(reply, peer->client_info->application_version, WI_STR("wired.info.application.version"));
	
	if(wi_string_length(peer->client_info->application_build) > 0)
		wi_p7_message_set_string_for_name(reply, peer->client_info->application_build, WI_STR("wired.info.application.build"));
	
	wi_p7_message_set_string_for_name(reply, peer->client_info->os_name, WI_STR("wired.info.os.name"));
	wi_p7_message_set_string_for_name(reply, peer->client_info->os_version, WI_STR("wired.info.os.version"));
	wi_p7_message_set_string_for_name(reply, peer->client_info->arch, WI_STR("wired.info.arch"));

	wi_recursive_lock_unlock(peer->user_lock);

	wd_user_reply_message(user, reply, message);
}



void wd_user_broadcast_status(wd_user_t *user) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*message;
	wd_chat_t			*chat;
	
	enumerator = wi_array_data_enumerator(wd_chats_chats_with_user(user));

	wi_recursive_lock_lock(user->user_lock);
	
	while((chat = wi_enumerator_next_data(enumerator))) {
		message = wi_p7_message_with_name(WI_STR("wired.chat.user_status"), wd_p7_spec);
		wi_p7_message_set_uint32_for_name(message, wd_chat_id(chat), WI_STR("wired.chat.id"));
		wi_p7_message_set_uint32_for_name(message, user->id, WI_STR("wired.user.id"));
		wi_p7_message_set_bool_for_name(message, user->idle, WI_STR("wired.user.idle"));
		wi_p7_message_set_string_for_name(message, user->nick, WI_STR("wired.user.nick"));
		wi_p7_message_set_string_for_name(message, user->status, WI_STR("wired.user.status"));
		wi_p7_message_set_enum_for_name(message, user->color, WI_STR("wired.account.color"));
		wi_p7_message_set_date_for_name(message, user->idle_time, WI_STR("wired.user.idle_time"));

		wd_chat_broadcast_message(chat, message);
	}

	wi_recursive_lock_unlock(user->user_lock);
}



void wd_user_broadcast_icon(wd_user_t *user) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*message;
	wd_chat_t			*chat;
	
	enumerator = wi_array_data_enumerator(wd_chats_chats_with_user(user));

	wi_recursive_lock_lock(user->user_lock);
	
	while((chat = wi_enumerator_next_data(enumerator))) {
		message = wi_p7_message_with_name(WI_STR("wired.chat.user_icon"), wd_p7_spec);
		wi_p7_message_set_uint32_for_name(message, user->id, WI_STR("wired.user.id"));
		wi_p7_message_set_uint32_for_name(message, wd_chat_id(chat), WI_STR("wired.chat.id"));
		wi_p7_message_set_data_for_name(message, user->icon, WI_STR("wired.user.icon"));
		
		wd_chat_broadcast_message(chat, message);
	}
	
	wi_recursive_lock_unlock(user->user_lock);
}



#pragma mark -

void wd_user_lock_socket(wd_user_t *user) {
	wi_recursive_lock_lock(user->socket_lock);
}



void wd_user_unlock_socket(wd_user_t *user) {
	wi_recursive_lock_unlock(user->socket_lock);
}



#pragma mark -

void wd_user_set_state(wd_user_t *user, wd_user_state_t state) {
	WD_USER_SET_VALUE(user, user->state, state);
}



wd_user_state_t wd_user_state(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->state);
}



void wd_user_set_idle(wd_user_t *user, wi_boolean_t idle) {
	WD_USER_SET_VALUE(user, user->idle, idle);
}



wi_boolean_t wd_user_is_idle(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->idle);
}



void wd_user_set_account(wd_user_t *user, wd_account_t *account) {
	WD_USER_SET_INSTANCE(user, user->account, account);
}



wd_account_t * wd_user_account(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->account);
}



void wd_user_set_nick(wd_user_t *user, wi_string_t *nick) {
	WD_USER_SET_INSTANCE(user, user->nick, nick);
}



wi_string_t * wd_user_nick(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->nick);
}



void wd_user_set_login(wd_user_t *user, wi_string_t *login) {
	WD_USER_SET_INSTANCE(user, user->login, login);
}



wi_string_t * wd_user_login(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->login);
}



void wd_user_set_client_info(wd_user_t *user, wd_client_info_t *client_info) {
	WD_USER_SET_INSTANCE(user, user->client_info, client_info);
}



wd_client_info_t * wd_user_client_info(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->client_info);
}



void wd_user_set_status(wd_user_t *user, wi_string_t *status) {
	WD_USER_SET_INSTANCE(user, user->status, status);
}



wi_string_t * wd_user_status(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->status);
}



void wd_user_set_icon(wd_user_t *user, wi_data_t *icon) {
	WD_USER_SET_INSTANCE(user, user->icon, icon);
}



wi_data_t * wd_user_icon(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->icon);
}



void wd_user_set_color(wd_user_t *user, wi_p7_enum_t color) {
	WD_USER_SET_VALUE(user, user->color, color);
}



wi_p7_enum_t wd_user_color(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->color);
}



void wd_user_set_idle_time(wd_user_t *user, wi_date_t * idle_time) {
	WD_USER_SET_INSTANCE(user, user->idle_time, idle_time);
}



wi_date_t * wd_user_idle_time(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->idle_time);
}



void wd_user_set_transfer(wd_user_t *user, wd_transfer_t *transfer) {
	WD_USER_SET_INSTANCE(user, user->transfer, transfer);
}



wd_transfer_t * wd_user_transfer(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->transfer);
}



void wd_user_set_joined_public_chat(wd_user_t *user, wi_boolean_t joined_public_chat) {
	WD_USER_SET_VALUE(user, user->joined_public_chat, joined_public_chat);
}



wi_boolean_t wd_user_has_joined_public_chat(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->joined_public_chat);
}



#pragma mark -

wi_boolean_t wd_user_supports_rsrc(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->client_info->supports_rsrc);
}



#pragma mark -

void wd_user_subscribe_boards(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_boards, true);
}



void wd_user_unsubscribe_boards(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_boards, false);
}



wi_boolean_t wd_user_is_subscribed_boards(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->subscribed_boards);
}




#pragma mark -

void wd_user_subscribe_accounts(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_accounts, true);
}



void wd_user_unsubscribe_accounts(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_accounts, false);
}



wi_boolean_t wd_user_is_subscribed_accounts(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->subscribed_accounts);
}




#pragma mark -

void wd_user_subscribe_log(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_log, true);
}



void wd_user_unsubscribe_log(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_log, false);
}



wi_boolean_t wd_user_is_subscribed_log(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->subscribed_log);
}




#pragma mark -

void wd_user_subscribe_events(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_events, true);
}



void wd_user_unsubscribe_events(wd_user_t *user) {
	WD_USER_SET_VALUE(user, user->subscribed_events, false);
}



wi_boolean_t wd_user_is_subscribed_events(wd_user_t *user) {
	WD_USER_RETURN_VALUE(user, user->subscribed_events);
}




#pragma mark -

void wd_user_subscribe_path(wd_user_t *user, wi_string_t *path, wi_string_t *realpath) {
	wi_string_t		*metapath;
	
	wi_recursive_lock_lock(user->user_lock);

	wi_mutable_set_add_data(user->subscribed_paths, realpath);

	if(wd_files_fsevents)
		wi_fsevents_add_path(wd_files_fsevents, realpath);
		
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
		
	wi_mutable_set_add_data(user->subscribed_paths, metapath);

	if(wd_files_fsevents)
		wi_fsevents_add_path(wd_files_fsevents, metapath);
	
	wi_mutable_dictionary_set_data_for_key(user->subscribed_virtualpaths, path, realpath);

	wi_recursive_lock_unlock(user->user_lock);
}



void wd_user_unsubscribe_path(wd_user_t *user, wi_string_t *realpath) {
	wi_string_t		*metapath;
	
	wi_recursive_lock_lock(user->user_lock);
		
	wi_mutable_set_remove_data(user->subscribed_paths, realpath);

	if(wd_files_fsevents)
		wi_fsevents_remove_path(wd_files_fsevents, realpath);
		
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
		
	wi_mutable_set_add_data(user->subscribed_paths, metapath);

	if(wd_files_fsevents)
		wi_fsevents_add_path(wd_files_fsevents, metapath);

	wi_mutable_dictionary_remove_data_for_key(user->subscribed_virtualpaths, realpath);
	
	wi_recursive_lock_unlock(user->user_lock);
}



void wd_user_unsubscribe_paths(wd_user_t *user) {
	wi_enumerator_t		*enumerator;
	wi_string_t			*path;
	
	wi_recursive_lock_lock(user->user_lock);

	enumerator = wi_array_data_enumerator(wi_set_all_data(user->subscribed_paths));
		
	while((path = wi_enumerator_next_data(enumerator))) {
		wi_retain(path);

		while(wi_set_contains_data(user->subscribed_paths, path)) {
			if(wd_files_fsevents)
				wi_fsevents_remove_path(wd_files_fsevents, path);

			wi_mutable_set_remove_data(user->subscribed_paths, path);
		}
			
		wi_release(path);
	}
	
	wi_mutable_dictionary_remove_all_data(user->subscribed_virtualpaths);
		
	wi_recursive_lock_unlock(user->user_lock);
}



wi_set_t * wd_user_subscribed_paths(wd_user_t *user) {
	WD_USER_RETURN_INSTANCE(user, user->subscribed_paths);
}



wi_array_t * wd_user_subscribed_virtual_paths_for_path(wd_user_t *user, wi_string_t *path) {
	wi_mutable_array_t		*array;
	wi_string_t				*virtualpath;
	
	array = wi_mutable_array();
	
	wi_recursive_lock_lock(user->user_lock);
	
	if(wi_is_equal(wi_string_last_path_component(path), WI_STR(WD_FILES_META_PATH))) {
		path			= wi_string_by_deleting_last_path_component(path);
		virtualpath		= wi_dictionary_data_for_key(user->subscribed_virtualpaths, path);
		
		if(virtualpath)
			wi_mutable_array_add_data(array, virtualpath);
		
		if(!wi_is_equal(path, WI_STR("/"))) {
			path			= wi_string_by_deleting_last_path_component(path);
			virtualpath		= wi_dictionary_data_for_key(user->subscribed_virtualpaths, path);
			
			if(virtualpath)
				wi_mutable_array_add_data(array, virtualpath);
		}
	} else {
		virtualpath = wi_dictionary_data_for_key(user->subscribed_virtualpaths, path);
		
		if(virtualpath)
			wi_mutable_array_add_data(array, virtualpath);
	}
	
	wi_recursive_lock_unlock(user->user_lock);
	
	return array;
}



#pragma mark -

wi_socket_t * wd_user_socket(wd_user_t *user) {
	return user->socket;
}



wi_p7_socket_t * wd_user_p7_socket(wd_user_t *user) {
	return user->p7_socket;
}



wd_uid_t wd_user_id(wd_user_t *user) {
	return user->id;
}



wi_string_t * wd_user_identifier(wd_user_t *user) {
	wi_string_t		*identifier;
	
	wi_recursive_lock_lock(user->user_lock);
	identifier = wi_string_with_format(WI_STR("%#@/%#@/%#@"), user->nick, user->login, user->ip);
	wi_recursive_lock_unlock(user->user_lock);
	
	return identifier;
}



wi_date_t * wd_user_login_time(wd_user_t *user) {
	return user->login_time;
}



wi_string_t * wd_user_ip(wd_user_t *user) {
	return user->ip;
}



wi_string_t * wd_user_host(wd_user_t *user) {
	return user->host;
}



#pragma mark -

wd_client_info_t * wd_client_info_with_message(wi_p7_message_t *message) {
	return wi_autorelease(wd_client_info_init_with_message(wd_client_info_alloc(), message));
}



#pragma mark -

static wd_client_info_t * wd_client_info_alloc(void) {
	return wi_runtime_create_instance(wd_client_info_runtime_id, sizeof(wd_client_info_t));
}



static wd_client_info_t * wd_client_info_init_with_message(wd_client_info_t *client_info, wi_p7_message_t *message) {
	client_info->application_name		= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.application.name")));
	client_info->application_version	= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.application.version")));
    client_info->application_build      = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.application.build")));
	client_info->os_name				= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.os.name")));
	client_info->os_version				= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.os.version")));
	client_info->arch					= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.arch")));
	
	wi_p7_message_get_bool_for_name(message, &client_info->supports_rsrc, WI_STR("wired.info.supports_rsrc"));
	
	return client_info;
}



static void wd_client_info_dealloc(wi_runtime_instance_t *instance) {
	wd_client_info_t		*client_info = instance;

	wi_release(client_info->application_name);
	wi_release(client_info->application_version);
    wi_release(client_info->application_build);
	wi_release(client_info->os_name);
	wi_release(client_info->os_version);
	wi_release(client_info->arch);
}



static wi_string_t * wd_client_info_description(wi_runtime_instance_t *instance) {
	wd_client_info_t		*client_info = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{%@}"),
		wi_runtime_class_name(client_info),
		client_info,
		wd_client_info_string(client_info));
}




#pragma mark -

wi_string_t * wd_client_info_string(wd_client_info_t *client_info) {
	return wi_string_with_format(WI_STR("%@ on %@"),
		wd_client_info_application_string(client_info),
		wd_client_info_os_string(client_info));
}



wi_string_t * wd_client_info_application_string(wd_client_info_t *client_info) {
	return wi_string_with_format(WI_STR("%@ %@ (%@)"),
		client_info->application_name,
		client_info->application_version,
		client_info->application_build);
}



wi_string_t * wd_client_info_os_string(wd_client_info_t *client_info) {
	return wi_string_with_format(WI_STR("%@ %@ (%@)"),
		client_info->os_name,
		client_info->os_version,
		client_info->arch);
}
