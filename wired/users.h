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

#ifndef WD_USERS_H
#define WD_USERS_H 1

#include <wired/wired.h>

#include "accounts.h"
#include "main.h"
#include "transfers.h"

#define WD_USER_BUFFER_INITIAL_SIZE		BUFSIZ
#define WD_USER_BUFFER_MAX_SIZE			131072


enum _wd_user_state {
	WD_USER_CONNECTED					= 0,
	WD_USER_GAVE_CLIENT_INFO,
	WD_USER_SAID_HELLO,
	WD_USER_GAVE_USER,
	WD_USER_LOGGED_IN,
	WD_USER_DISCONNECTED
};
typedef enum _wd_user_state				wd_user_state_t;

enum _wd_user_protocol_state {
	WD_USER_PROTOCOL_CONNECTED			= 0,
	WD_USER_PROTOCOL_LOGGING_IN,
	WD_USER_PROTOCOL_LOGGED_IN,
	WD_USER_PROTOCOL_TRANSFERRING,
	WD_USER_PROTOCOL_DISCONNECTING
};
typedef enum _wd_user_protocol_state	wd_user_protocol_state_t;


typedef uint32_t						wd_uid_t;
typedef struct _wd_client_info			wd_client_info_t;


void									wd_users_initialize(void);
void									wd_users_schedule(void);

void									wd_users_add_user(wd_user_t *);
void									wd_users_remove_user(wd_user_t *);
void									wd_users_remove_all_users(void);
wd_user_t *								wd_users_user_with_id(wd_uid_t);
wi_array_t *							wd_users_users_with_login(wi_string_t *);
void									wd_users_reply_users(wd_user_t *, wi_p7_message_t *);

wd_user_t *								wd_user_with_p7_socket(wi_p7_socket_t *);

void									wd_user_reply_user_info(wd_user_t *, wd_user_t *, wi_p7_message_t *);
void									wd_user_broadcast_status(wd_user_t *);
void									wd_user_broadcast_icon(wd_user_t *);

void									wd_user_lock_socket(wd_user_t *);
void									wd_user_unlock_socket(wd_user_t *);

void									wd_user_set_state(wd_user_t *, wd_user_state_t);
wd_user_state_t							wd_user_state(wd_user_t *);

void									wd_user_set_idle(wd_user_t *, wi_boolean_t);
wi_boolean_t							wd_user_is_idle(wd_user_t *);
void									wd_user_set_account(wd_user_t *, wd_account_t *);
wd_account_t *							wd_user_account(wd_user_t *);
void									wd_user_set_nick(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_nick(wd_user_t *);
void									wd_user_set_login(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_login(wd_user_t *);
void									wd_user_set_client_info(wd_user_t *, wd_client_info_t *);
wd_client_info_t *						wd_user_client_info(wd_user_t *);
void									wd_user_set_status(wd_user_t *, wi_string_t *);
wi_string_t	*							wd_user_status(wd_user_t *);
void									wd_user_set_icon(wd_user_t *, wi_data_t *);
wi_data_t *								wd_user_icon(wd_user_t *);
void									wd_user_set_color(wd_user_t *, wi_p7_enum_t);
wi_p7_enum_t							wd_user_color(wd_user_t *);
void									wd_user_set_idle_time(wd_user_t *, wi_date_t *);
wi_date_t *								wd_user_idle_time(wd_user_t *);
void									wd_user_set_transfer(wd_user_t *, wd_transfer_t *);
wd_transfer_t *							wd_user_transfer(wd_user_t *);
void									wd_user_set_joined_public_chat(wd_user_t *, wi_boolean_t);
wi_boolean_t							wd_user_has_joined_public_chat(wd_user_t *);

wi_boolean_t							wd_user_supports_rsrc(wd_user_t *);

void									wd_user_subscribe_boards(wd_user_t *);
void									wd_user_unsubscribe_boards(wd_user_t *);
wi_boolean_t							wd_user_is_subscribed_boards(wd_user_t *);

void									wd_user_subscribe_accounts(wd_user_t *);
void									wd_user_unsubscribe_accounts(wd_user_t *);
wi_boolean_t							wd_user_is_subscribed_accounts(wd_user_t *);

void									wd_user_subscribe_log(wd_user_t *);
void									wd_user_unsubscribe_log(wd_user_t *);
wi_boolean_t							wd_user_is_subscribed_log(wd_user_t *);

void									wd_user_subscribe_events(wd_user_t *);
void									wd_user_unsubscribe_events(wd_user_t *);
wi_boolean_t							wd_user_is_subscribed_events(wd_user_t *);

void									wd_user_subscribe_path(wd_user_t *, wi_string_t *, wi_string_t *);
void									wd_user_unsubscribe_path(wd_user_t *, wi_string_t *);
void									wd_user_unsubscribe_paths(wd_user_t *);
wi_set_t *								wd_user_subscribed_paths(wd_user_t *);
wi_array_t *							wd_user_subscribed_virtual_paths_for_path(wd_user_t *, wi_string_t *);

wi_socket_t *							wd_user_socket(wd_user_t *);
wi_p7_socket_t *						wd_user_p7_socket(wd_user_t *);
wd_uid_t								wd_user_id(wd_user_t *);
wi_string_t *							wd_user_identifier(wd_user_t *);
wi_date_t *								wd_user_login_time(wd_user_t *);
wi_string_t	*							wd_user_ip(wd_user_t *);
wi_string_t	*							wd_user_host(wd_user_t *);


extern wi_dictionary_t					*wd_users;


wd_client_info_t *						wd_client_info_with_message(wi_p7_message_t *);

wi_string_t *							wd_client_info_string(wd_client_info_t *);
wi_string_t *							wd_client_info_application_string(wd_client_info_t *);
wi_string_t *							wd_client_info_os_string(wd_client_info_t *);

#endif /* WD_USERS_H */
