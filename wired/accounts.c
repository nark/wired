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

#include <stddef.h>
#include <string.h>
#include <wired/wired.h>

#include "accounts.h"
#include "boards.h"
#include "chats.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "users.h"

#define WD_ACCOUNT_FIELD_TABLE_NAME					"tablename"
#define WD_ACCOUNT_FIELD_PROTOCOL_NAME				"protocolname"
#define WD_ACCOUNT_FIELD_TYPE						"type"
#define WD_ACCOUNT_FIELD_ACCOUNT					"account"
#define WD_ACCOUNT_FIELD_REQUIRED					"required"


struct _wd_account {
	wi_runtime_base_t								base;
	
	wi_mutable_dictionary_t							*values;
};

enum _wd_account_type {
	WD_ACCOUNT_USER,
	WD_ACCOUNT_GROUP
};
typedef enum _wd_account_type						wd_account_type_t;

enum _wd_account_field_type {
	WD_ACCOUNT_FIELD_STRING							= 0,
	WD_ACCOUNT_FIELD_DATE,
	WD_ACCOUNT_FIELD_NUMBER,
	WD_ACCOUNT_FIELD_BOOLEAN,
	WD_ACCOUNT_FIELD_ENUM,
	WD_ACCOUNT_FIELD_LIST
};
typedef enum _wd_account_field_type					wd_account_field_type_t;

enum _wd_account_field_account {
	WD_ACCOUNT_FIELD_USER_LIST						= (1 << 0),
	WD_ACCOUNT_FIELD_GROUP_LIST						= (1 << 1),
	WD_ACCOUNT_FIELD_USER							= (1 << 2),
	WD_ACCOUNT_FIELD_GROUP							= (1 << 3),
	WD_ACCOUNT_FIELD_PRIVILEGE						= (1 << 4),
	WD_ACCOUNT_FIELD_USER_AND_GROUP					= (WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_GROUP),
	WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST		= (WD_ACCOUNT_FIELD_USER_LIST | WD_ACCOUNT_FIELD_GROUP_LIST),
	WD_ACCOUNT_FIELD_USER_AND_PRIVILEGE				= (WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_PRIVILEGE),
	WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE	= (WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_GROUP | WD_ACCOUNT_FIELD_PRIVILEGE)
};
typedef enum _wd_account_field_account				wd_account_field_account_t;


static void											wd_accounts_create_tables(void);
static void											wd_accounts_create_default_accounts(void);
static void											wd_accounts_convert_accounts(void);
static wi_boolean_t									wd_accounts_convert_accounts_from_1_3(wi_string_t *, wi_array_t *);
static wi_boolean_t									wd_accounts_convert_accounts_from_2_0b1(wi_string_t *, wi_array_t *);
static wi_boolean_t									wd_accounts_convert_accounts_from_2_0b2(wi_string_t *, wi_string_t *, wi_uinteger_t);

static void											wd_accounts_reload_user_account(wd_account_t *);
static void											wd_accounts_reload_group_account(wd_account_t *);
static void											wd_accounts_reload_account(wd_user_t *, wi_string_t *);
static wi_boolean_t									wd_accounts_rename_group(wi_string_t *oldname, wi_string_t *newname);

static void											wd_accounts_notify_subscribers(void);

static wd_account_t *								wd_account_init(wd_account_t *);
static wd_account_t *								wd_account_init_with_sqlite3_results(wd_account_t *, wi_dictionary_t *);
static wd_account_t *								wd_account_init_with_name_and_values(wd_account_t *, wi_string_t *, wi_dictionary_t *);
static wi_runtime_instance_t *						wd_account_copy(wi_runtime_instance_t *);
static wi_string_t *								wd_account_description(wi_runtime_instance_t *);
static void											wd_account_dealloc(wi_runtime_instance_t *);

static wi_string_t *								wd_account_sqlite3_insert_string(wd_account_t *, wi_uinteger_t);
static wi_string_t *								wd_account_sqlite3_update_string(wd_account_t *, wi_uinteger_t);

static void											wd_account_read_from_message(wd_account_t *, wi_p7_message_t *);
static void											wd_account_write_to_message(wd_account_t *, wi_uinteger_t, wi_p7_message_t *);
static void											wd_account_override_privileges(wd_account_t *, wd_account_t *);


static wi_mutable_dictionary_t						*wd_account_fields_by_table_name;
static wi_mutable_dictionary_t						*wd_account_fields_by_protocol_name;

static wi_runtime_id_t								wd_account_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t							wd_account_runtime_class = {
	"wd_account_t",
	wd_account_dealloc,
	wd_account_copy,
	NULL,
	wd_account_description,
	NULL
};



#define WD_ACCOUNT_FIELD_DICTIONARY(tablename, protocolname, type, account, required)	\
	wi_dictionary_with_data_and_keys(													\
		(tablename),						WI_STR(WD_ACCOUNT_FIELD_TABLE_NAME),		\
		(protocolname),						WI_STR(WD_ACCOUNT_FIELD_PROTOCOL_NAME),		\
		WI_INT32((type)),					WI_STR(WD_ACCOUNT_FIELD_TYPE),				\
		WI_INT32((account)),				WI_STR(WD_ACCOUNT_FIELD_ACCOUNT),			\
		wi_number_with_bool((required)),	WI_STR(WD_ACCOUNT_FIELD_REQUIRED),			\
		NULL)

void wd_accounts_initialize(void) {
	wi_enumerator_t		*enumerator;
	wi_dictionary_t		*dictionary, *results;
	
	wd_accounts_create_tables();
	
	wd_account_runtime_id = wi_runtime_register_class(&wd_account_runtime_class);

	enumerator = wi_array_data_enumerator(wi_array_with_data(
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("name"),
									WI_STR("wired.account.name"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(wi_null(),
									WI_STR("wired.account.new_name"),
									WD_ACCOUNT_FIELD_STRING,
									0,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("full_name"),
									WI_STR("wired.account.full_name"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("comment"),
									WI_STR("wired.account.comment"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("creation_time"),
									WI_STR("wired.account.creation_time"),
									WD_ACCOUNT_FIELD_DATE,
									WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("modification_time"),
									WI_STR("wired.account.modification_time"),
									WD_ACCOUNT_FIELD_DATE,
									WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("login_time"),
									WI_STR("wired.account.login_time"),
									WD_ACCOUNT_FIELD_DATE,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("edited_by"),
									WI_STR("wired.account.edited_by"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("downloads"),
									WI_STR("wired.account.downloads"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("download_transferred"),
									WI_STR("wired.account.download_transferred"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("uploads"),
									WI_STR("wired.account.uploads"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("upload_transferred"),
									WI_STR("wired.account.upload_transferred"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("group"),
									WI_STR("wired.account.group"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER_AND_PRIVILEGE | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("groups"),
									WI_STR("wired.account.groups"),
									WD_ACCOUNT_FIELD_LIST,
									WD_ACCOUNT_FIELD_USER_AND_PRIVILEGE | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("password"),
									WI_STR("wired.account.password"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST,
									true),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("color"),
									WI_STR("wired.account.color"),
									WD_ACCOUNT_FIELD_ENUM,
									WD_ACCOUNT_FIELD_USER_AND_GROUP | WD_ACCOUNT_FIELD_USER_LIST_AND_GROUP_LIST,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("files"),
									WI_STR("wired.account.files"),
									WD_ACCOUNT_FIELD_STRING,
									WD_ACCOUNT_FIELD_USER_AND_GROUP,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("user_get_info"),
									WI_STR("wired.account.user.get_info"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("user_disconnect_users"),
									WI_STR("wired.account.user.disconnect_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("user_ban_users"),
									WI_STR("wired.account.user.ban_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("user_cannot_be_disconnected"),
									WI_STR("wired.account.user.cannot_be_disconnected"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("user_cannot_set_nick"),
									WI_STR("wired.account.user.cannot_set_nick"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("user_get_users"),
									WI_STR("wired.account.user.get_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("chat_kick_users"),
									WI_STR("wired.account.chat.kick_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("chat_set_topic"),
									WI_STR("wired.account.chat.set_topic"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("chat_create_chats"),
									WI_STR("wired.account.chat.create_chats"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("message_send_messages"),
									WI_STR("wired.account.message.send_messages"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("message_broadcast"),
									WI_STR("wired.account.message.broadcast"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_list_files"),
									WI_STR("wired.account.file.list_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_search_files"),
									WI_STR("wired.account.file.search_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_get_info"),
									WI_STR("wired.account.file.get_info"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_create_links"),
									WI_STR("wired.account.file.create_links"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_rename_files"),
									WI_STR("wired.account.file.rename_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_set_type"),
									WI_STR("wired.account.file.set_type"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_set_comment"),
									WI_STR("wired.account.file.set_comment"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_set_permissions"),
									WI_STR("wired.account.file.set_permissions"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_set_executable"),
									WI_STR("wired.account.file.set_executable"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_set_label"),
									WI_STR("wired.account.file.set_label"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_recursive_list_depth_limit"),
									WI_STR("wired.account.file.recursive_list_depth_limit"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_create_directories"),
									WI_STR("wired.account.file.create_directories"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_move_files"),
									WI_STR("wired.account.file.move_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_delete_files"),
									WI_STR("wired.account.file.delete_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("file_access_all_dropboxes"),
									WI_STR("wired.account.file.access_all_dropboxes"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_change_password"),
									WI_STR("wired.account.account.change_password"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_list_accounts"),
									WI_STR("wired.account.account.list_accounts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_read_accounts"),
									WI_STR("wired.account.account.read_accounts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_create_users"),
									WI_STR("wired.account.account.create_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_edit_users"),
									WI_STR("wired.account.account.edit_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_delete_users"),
									WI_STR("wired.account.account.delete_users"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_create_groups"),
									WI_STR("wired.account.account.create_groups"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_edit_groups"),
									WI_STR("wired.account.account.edit_groups"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_delete_groups"),
									WI_STR("wired.account.account.delete_groups"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("account_raise_account_privileges"),
									WI_STR("wired.account.account.raise_account_privileges"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_download_files"),
									WI_STR("wired.account.transfer.download_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_upload_files"),
									WI_STR("wired.account.transfer.upload_files"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_upload_anywhere"),
									WI_STR("wired.account.transfer.upload_anywhere"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_upload_directories"),
									WI_STR("wired.account.transfer.upload_directories"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_download_speed_limit"),
									WI_STR("wired.account.transfer.download_speed_limit"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_upload_speed_limit"),
									WI_STR("wired.account.transfer.upload_speed_limit"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_download_limit"),
									WI_STR("wired.account.transfer.download_limit"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("transfer_upload_limit"),
									WI_STR("wired.account.transfer.upload_limit"),
									WD_ACCOUNT_FIELD_NUMBER,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("log_view_log"),
									WI_STR("wired.account.log.view_log"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("events_view_events"),
									WI_STR("wired.account.events.view_events"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("settings_get_settings"),
									WI_STR("wired.account.settings.get_settings"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("settings_set_settings"),
									WI_STR("wired.account.settings.set_settings"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("banlist_get_bans"),
									WI_STR("wired.account.banlist.get_bans"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("banlist_add_bans"),
									WI_STR("wired.account.banlist.add_bans"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("banlist_delete_bans"),
									WI_STR("wired.account.banlist.delete_bans"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_read_boards"),
									WI_STR("wired.account.board.read_boards"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_add_boards"),
									WI_STR("wired.account.board.add_boards"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_move_boards"),
									WI_STR("wired.account.board.move_boards"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_rename_boards"),
									WI_STR("wired.account.board.rename_boards"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_delete_boards"),
									WI_STR("wired.account.board.delete_boards"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_get_board_info"),
									WI_STR("wired.account.board.get_board_info"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_set_board_info"),
									WI_STR("wired.account.board.set_board_info"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_add_threads"),
									WI_STR("wired.account.board.add_threads"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_move_threads"),
									WI_STR("wired.account.board.move_threads"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_add_posts"),
									WI_STR("wired.account.board.add_posts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_edit_own_threads_and_posts"),
									WI_STR("wired.account.board.edit_own_threads_and_posts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_edit_all_threads_and_posts"),
									WI_STR("wired.account.board.edit_all_threads_and_posts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_delete_own_threads_and_posts"),
									WI_STR("wired.account.board.delete_own_threads_and_posts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("board_delete_all_threads_and_posts"),
									WI_STR("wired.account.board.delete_all_threads_and_posts"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("tracker_list_servers"),
									WI_STR("wired.account.tracker.list_servers"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		WD_ACCOUNT_FIELD_DICTIONARY(WI_STR("tracker_register_servers"),
									WI_STR("wired.account.tracker.register_servers"),
									WD_ACCOUNT_FIELD_BOOLEAN,
									WD_ACCOUNT_FIELD_USER_AND_GROUP_AND_PRIVILEGE,
									false),
		NULL));
	
	wd_account_fields_by_table_name		= wi_dictionary_init(wi_mutable_dictionary_alloc());
	wd_account_fields_by_protocol_name	= wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		wi_mutable_dictionary_set_data_for_key(wd_account_fields_by_table_name,
											   dictionary,
											   wi_dictionary_data_for_key(dictionary, WI_STR(WD_ACCOUNT_FIELD_TABLE_NAME)));

		wi_mutable_dictionary_set_data_for_key(wd_account_fields_by_protocol_name,
											   dictionary,
											   wi_dictionary_data_for_key(dictionary, WI_STR(WD_ACCOUNT_FIELD_PROTOCOL_NAME)));
	}
	
	wd_accounts_convert_accounts();
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT COUNT(*) AS count FROM users"), NULL);
	
	if(!results)
		wi_log_fatal(WI_STR("Could not execute database statement: %m"));
	
	if(wi_number_integer(wi_dictionary_data_for_key(results, WI_STR("count"))) == 0)
		wd_accounts_create_default_accounts();
}



#pragma mark -

wd_account_t * wd_accounts_read_user_and_group(wi_string_t *name) {
	wi_string_t			*groupname;
	wd_account_t		*useraccount, *groupaccount;
	
	useraccount = wd_accounts_read_user(name);
	
	if(!useraccount)
		return NULL;
	
	groupname = wd_account_group(useraccount);
	
	if(groupname && wi_string_length(groupname) > 0) {
		groupaccount = wd_accounts_read_group(groupname);
		
		if(groupaccount)
			wd_account_override_privileges(useraccount, groupaccount);
	}

    //wi_log_info(WI_STR("useraccount: %@"), wi_dictionary_data_for_key(useraccount->values, WI_STR("wired.account.name")));
	
	return useraccount;
}



wd_account_t * wd_accounts_read_user(wi_string_t *name) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT * FROM users WHERE name = ?"),
										   name,
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return NULL;
	}
	
	if(wi_dictionary_count(results) == 0)
		return NULL;
	
	return wi_autorelease(wd_account_init_with_sqlite3_results(wd_account_alloc(), results));
}



wd_account_t * wd_accounts_read_group(wi_string_t *name) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT * FROM groups WHERE name = ?"),
										   name,
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return NULL;
	}
	
	if(wi_dictionary_count(results) == 0)
		return NULL;
	
	return wi_autorelease(wd_account_init_with_sqlite3_results(wd_account_alloc(), results));
}



wi_string_t * wd_accounts_password_for_user(wi_string_t *name) {
	wd_account_t	*account;
	
	if(wi_string_length(name) == 0)
		name = WI_STR("guest");
	
	account = wd_accounts_read_user(name);
	
	if(account)
		return wd_account_password(account);
	
	return NULL;
}



wi_boolean_t wd_accounts_change_password(wd_account_t *account, wi_string_t *password, wd_user_t *user, wi_p7_message_t *message) {
	if(!wi_sqlite3_execute_statement(wd_database,
									 WI_STR("UPDATE users SET password = ? WHERE name = ?"),
									 password,
									 wd_account_name(account),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	return true;
}



wi_boolean_t wd_accounts_create_user(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*string;
	
	wi_mutable_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.creation_time"));
	wi_mutable_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	string = wd_account_sqlite3_insert_string(account, WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST);
	
	if(!wi_sqlite3_execute_statement(wd_database, wi_string_with_format(WI_STR("INSERT INTO users %@"), string), NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}

	wd_accounts_notify_subscribers();
	
	return true;
}



wi_boolean_t wd_accounts_create_group(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*string;
	
	wi_mutable_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.creation_time"));
	wi_mutable_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	string = wd_account_sqlite3_insert_string(account, WD_ACCOUNT_FIELD_GROUP);
	
	if(!wi_sqlite3_execute_statement(wd_database, wi_string_with_format(WI_STR("INSERT INTO groups %@"), string), NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}

	wd_accounts_notify_subscribers();
	
	return true;
}



wi_boolean_t wd_accounts_edit_user(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t		*results;
	wi_string_t			*name, *newname, *string;
	
	name = wi_autorelease(wi_retain(wd_account_name(account)));
	
	if(wd_account_new_name(account))
		newname = wd_account_new_name(account);
	else
		newname = NULL;
	
	if(newname && wi_string_length(newname) > 0 && !wi_is_equal(name, newname))
		wi_mutable_dictionary_set_data_for_key(account->values, newname, WI_STR("wired.account.name"));
	
	wi_mutable_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.modification_time"));
	wi_mutable_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
	
	wi_sqlite3_begin_immediate_transaction(wd_database);
	
	string		= wd_account_sqlite3_update_string(account, WD_ACCOUNT_FIELD_USER);
	results		= wi_sqlite3_execute_statement(wd_database, wi_string_with_format(WI_STR("UPDATE users SET %@ WHERE name = ?"), string),
											   name,
											   NULL);
	
	if(results) {
		wi_sqlite3_commit_transaction(wd_database);
		
		if(newname) {
			wd_boards_renamed_user(name, newname);
			wd_accounts_notify_subscribers();
		}
		
		wd_accounts_reload_user_account(account);
		
		return true;
	} else {
		wi_sqlite3_rollback_transaction(wd_database);
		
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
}



wi_boolean_t wd_accounts_edit_group(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*name, *newname, *string;
	wi_boolean_t		has_new_name, commit;
	wi_dictionary_t     *results;
    
	name = wi_autorelease(wi_retain(wd_account_name(account)));
	
	if(wd_account_new_name(account))
		newname = wd_account_new_name(account);
	else
		newname = NULL;
	
    has_new_name = (newname && wi_string_length(newname) > 0 && !wi_is_equal(name, newname));
    
	if(has_new_name)
		wi_mutable_dictionary_set_data_for_key(account->values, newname, WI_STR("wired.account.name"));
	
	wi_mutable_dictionary_set_data_for_key(account->values, wi_date(), WI_STR("wired.account.modification_time"));
	wi_mutable_dictionary_set_data_for_key(account->values, wd_user_nick(user), WI_STR("wired.account.edited_by"));
		
	string = wd_account_sqlite3_update_string(account, WD_ACCOUNT_FIELD_GROUP);
    
    
	if(wi_sqlite3_execute_statement(wd_database, wi_string_with_format(WI_STR("UPDATE groups SET %@ WHERE name = ?"), string), name, NULL)) {
        
        if(has_new_name) {
            
            wd_accounts_rename_group(name, newname);
            
			wd_boards_renamed_group(name, newname);
			wd_accounts_notify_subscribers();
		}
		
		wd_accounts_reload_group_account(account);
		
		return true;
    } else {
		
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;   
    }
}



wi_boolean_t wd_accounts_delete_user(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t		*results;
	
	wi_sqlite3_begin_immediate_transaction(wd_database);
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM users WHERE name = ?"),
										   wd_account_name(account),
										   NULL);
	
	if(results) {
		wi_sqlite3_commit_transaction(wd_database);
		
		wd_boards_renamed_user(wd_account_name(account), NULL);
		
		wd_accounts_notify_subscribers();
		
		return true;
	} else {
		wi_sqlite3_rollback_transaction(wd_database);
		
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
    
}



wi_boolean_t wd_accounts_delete_group(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t		*results;
	
	wi_sqlite3_begin_immediate_transaction(wd_database);
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM groups WHERE name = ?"),
										   wd_account_name(account),
										   NULL);
	
	if(results) {
		wi_sqlite3_commit_transaction(wd_database);
		
		wd_boards_renamed_user(wd_account_name(account), NULL);
		
		wd_accounts_notify_subscribers();
		
		return true;
	} else {
		wi_sqlite3_rollback_transaction(wd_database);
		
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
}



void wd_accounts_update_login_time(wd_account_t *account) {
	wi_string_t 	*name;

	name = wd_account_name(account);

	if(!name)
		return;

	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE users SET login_time = DATETIME() WHERE name = ?"),
									 wd_account_name(account),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
}



void wd_accounts_add_download_statistics(wd_account_t *account, wi_boolean_t finished, wi_file_offset_t transferred) {
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE users SET "
														 "downloads = downloads + ?, "
														 "download_transferred = download_transferred + ? "
														 "WHERE name = ?"),
									 WI_INT32(finished ? 1 : 0),
									 WI_INT32(transferred),
									 wd_account_name(account),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
}



void wd_accounts_add_upload_statistics(wd_account_t *account, wi_boolean_t finished, wi_file_offset_t transferred) {
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE users SET "
														 "uploads = uploads + ?, "
														 "upload_transferred = upload_transferred + ? "
														 "WHERE name = ?"),
									 WI_INT32(finished ? 1 : 0),
									 WI_INT32(transferred),
									 wd_account_name(account),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
}



#pragma mark -

static void wd_accounts_create_tables(void) {
	wi_uinteger_t		version;
	
	version = wd_database_version_for_table(WI_STR("users"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE users ("
																 "name TEXT NOT NULL, "
																 "full_name TEXT, "
																 "comment TEXT, "
																 "creation_time TEXT, "
																 "modification_time TEXT, "
																 "login_time TEXT, "
																 "edited_by TEXT, "
																 "downloads INTEGER NOT NULL DEFAULT 0, "
																 "download_transferred INTEGER NOT NULL DEFAULT 0, "
																 "uploads INTEGER NOT NULL DEFAULT 0, "
																 "upload_transferred INTEGER NOT NULL DEFAULT 0, "
																 "`group` TEXT, "
																 "groups TEXT, "
																 "password TEXT, "
																 "color INTEGER, "
																 "files TEXT, "
																 "user_get_info INTEGER, "
																 "user_disconnect_users INTEGER, "
																 "user_ban_users INTEGER, "
																 "user_cannot_be_disconnected INTEGER, "
																 "user_cannot_set_nick INTEGER, "
																 "user_get_users INTEGER, "
																 "chat_kick_users INTEGER, "
																 "chat_set_topic INTEGER, "
																 "chat_create_chats INTEGER, "
																 "message_send_messages INTEGER, "
																 "message_broadcast INTEGER, "
																 "file_list_files INTEGER, "
																 "file_search_files INTEGER, "
																 "file_get_info INTEGER, "
																 "file_create_links INTEGER, "
																 "file_rename_files INTEGER, "
																 "file_set_type INTEGER, "
																 "file_set_comment INTEGER, "
																 "file_set_permissions INTEGER, "
																 "file_set_executable INTEGER, "
																 "file_set_label INTEGER, "
																 "file_recursive_list_depth_limit INTEGER, "
																 "file_create_directories INTEGER, "
																 "file_move_files INTEGER, "
																 "file_delete_files INTEGER, "
																 "file_access_all_dropboxes INTEGER, "
																 "account_change_password INTEGER, "
																 "account_list_accounts INTEGER, "
																 "account_read_accounts INTEGER, "
																 "account_create_users INTEGER, "
																 "account_edit_users INTEGER, "
																 "account_delete_users INTEGER, "
																 "account_create_groups INTEGER, "
																 "account_edit_groups INTEGER, "
																 "account_delete_groups INTEGER, "
																 "account_raise_account_privileges INTEGER, "
																 "transfer_download_files INTEGER, "
																 "transfer_upload_files INTEGER, "
																 "transfer_upload_anywhere INTEGER, "
																 "transfer_upload_directories INTEGER, "
																 "transfer_download_speed_limit INTEGER, "
																 "transfer_upload_speed_limit INTEGER, "
																 "transfer_download_limit INTEGER, "
																 "transfer_upload_limit INTEGER, "
																 "board_read_boards INTEGER, "
																 "board_add_boards INTEGER, "
																 "board_move_boards INTEGER, "
																 "board_rename_boards INTEGER, "
																 "board_delete_boards INTEGER, "
																 "board_get_board_info INTEGER, "
																 "board_set_board_info INTEGER, "
																 "board_add_threads INTEGER, "
																 "board_move_threads INTEGER, "
																 "board_add_posts INTEGER, "
																 "board_edit_own_threads_and_posts INTEGER, "
																 "board_edit_all_threads_and_posts INTEGER, "
																 "board_delete_own_threads_and_posts INTEGER, "
																 "board_delete_all_threads_and_posts INTEGER, "
																 "log_view_log INTEGER, "
																 "events_view_events INTEGER, "
																 "settings_get_settings INTEGER, "
																 "settings_set_settings INTEGER, "
																 "banlist_get_bans INTEGER, "
																 "banlist_add_bans INTEGER, "
																 "banlist_delete_bans INTEGER, "
																 "tracker_list_servers INTEGER, "
																 "tracker_register_servers INTEGER, "
																 "PRIMARY KEY (name) "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("users"));
	
	version = wd_database_version_for_table(WI_STR("groups"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE groups ("
																 "name TEXT NOT NULL, "
																 "full_name TEXT, "
																 "comment TEXT, "
																 "creation_time TEXT, "
																 "modification_time TEXT, "
																 "edited_by TEXT, "
																 "color INTEGER, "
																 "files TEXT, "
																 "user_get_info INTEGER, "
																 "user_disconnect_users INTEGER, "
																 "user_ban_users INTEGER, "
																 "user_cannot_be_disconnected INTEGER, "
																 "user_cannot_set_nick INTEGER, "
																 "user_get_users INTEGER, "
																 "chat_kick_users INTEGER, "
																 "chat_set_topic INTEGER, "
																 "chat_create_chats INTEGER, "
																 "message_send_messages INTEGER, "
																 "message_broadcast INTEGER, "
																 "file_list_files INTEGER, "
																 "file_search_files INTEGER, "
																 "file_get_info INTEGER, "
																 "file_create_links INTEGER, "
																 "file_rename_files INTEGER, "
																 "file_set_type INTEGER, "
																 "file_set_comment INTEGER, "
																 "file_set_permissions INTEGER, "
																 "file_set_executable INTEGER, "
																 "file_set_label INTEGER, "
																 "file_recursive_list_depth_limit INTEGER, "
																 "file_create_directories INTEGER, "
																 "file_move_files INTEGER, "
																 "file_delete_files INTEGER, "
																 "file_access_all_dropboxes INTEGER, "
																 "account_change_password INTEGER, "
																 "account_list_accounts INTEGER, "
																 "account_read_accounts INTEGER, "
																 "account_create_users INTEGER, "
																 "account_edit_users INTEGER, "
																 "account_delete_users INTEGER, "
																 "account_create_groups INTEGER, "
																 "account_edit_groups INTEGER, "
																 "account_delete_groups INTEGER, "
																 "account_raise_account_privileges INTEGER, "
																 "transfer_download_files INTEGER, "
																 "transfer_upload_files INTEGER, "
																 "transfer_upload_anywhere INTEGER, "
																 "transfer_upload_directories INTEGER, "
																 "transfer_download_speed_limit INTEGER, "
																 "transfer_upload_speed_limit INTEGER, "
																 "transfer_download_limit INTEGER, "
																 "transfer_upload_limit INTEGER, "
																 "board_read_boards INTEGER, "
																 "board_add_boards INTEGER, "
																 "board_move_boards INTEGER, "
																 "board_rename_boards INTEGER, "
																 "board_delete_boards INTEGER, "
																 "board_get_board_info INTEGER, "
																 "board_set_board_info INTEGER, "
																 "board_add_threads INTEGER, "
																 "board_move_threads INTEGER, "
																 "board_add_posts INTEGER, "
																 "board_edit_own_threads_and_posts INTEGER, "
																 "board_edit_all_threads_and_posts INTEGER, "
																 "board_delete_own_threads_and_posts INTEGER, "
																 "board_delete_all_threads_and_posts INTEGER, "
																 "log_view_log INTEGER, "
																 "events_view_events INTEGER, "
																 "settings_get_settings INTEGER, "
																 "settings_set_settings INTEGER, "
																 "banlist_get_bans INTEGER, "
																 "banlist_add_bans INTEGER, "
																 "banlist_delete_bans INTEGER, "
																 "tracker_list_servers INTEGER, "
																 "tracker_register_servers INTEGER, "
																 "PRIMARY KEY (name) "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
            
            // delete group trigger: updates owned users when a group is deleted
            if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TRIGGER groups_delete_trigger "
                                                                 "BEFORE DELETE ON groups "
                                                                 "FOR EACH ROW BEGIN "
                                                                 "UPDATE users SET `group`=NULL WHERE `group` = `OLD`.`name`; "
                                                                 "END"),
                                             NULL)) {
                wi_log_fatal(WI_STR("Could not execute database statement: %m"));
            }
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("groups"));
}



static void wd_accounts_create_default_accounts(void) {
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO users ("
														 "name, "
														 "full_name, "
														 "creation_time, "
														 "downloads, "
														 "download_transferred, "
														 "uploads, "
														 "upload_transferred, "
														 "password, "
														 "color, "
														 "user_get_info, "
														 "user_disconnect_users, "
														 "user_ban_users, "
														 "user_get_users, "
														 "chat_kick_users, "
														 "chat_set_topic, "
														 "chat_create_chats, "
														 "message_send_messages, "
														 "message_broadcast, "
														 "file_list_files, "
														 "file_search_files, "
														 "file_get_info, "
														 "file_create_links, "
														 "file_rename_files, "
														 "file_set_type, "
														 "file_set_comment, "
														 "file_set_permissions, "
														 "file_set_executable, "
														 "file_set_label, "
														 "file_create_directories, "
														 "file_move_files, "
														 "file_delete_files, "
														 "file_access_all_dropboxes, "
														 "account_change_password, "
														 "account_list_accounts, "
														 "account_read_accounts, "
														 "account_create_users, "
														 "account_edit_users, "
														 "account_delete_users, "
														 "account_create_groups, "
														 "account_edit_groups, "
														 "account_delete_groups, "
														 "account_raise_account_privileges, "
														 "transfer_download_files, "
														 "transfer_upload_files, "
														 "transfer_upload_anywhere, "
														 "transfer_upload_directories, "
														 "board_read_boards, "
														 "board_add_boards, "
														 "board_move_boards, "
														 "board_rename_boards, "
														 "board_delete_boards, "
														 "board_get_board_info, "
														 "board_set_board_info, "
														 "board_add_threads, "
														 "board_move_threads, "
														 "board_add_posts, "
														 "board_edit_own_threads_and_posts, "
														 "board_edit_all_threads_and_posts, "
														 "board_delete_own_threads_and_posts, "
														 "board_delete_all_threads_and_posts, "
														 "log_view_log, "
														 "events_view_events, "
														 "settings_get_settings, "
														 "settings_set_settings, "
														 "banlist_get_bans, "
														 "banlist_add_bans, "
														 "banlist_delete_bans, "
														 "tracker_list_servers, "
														 "tracker_register_servers) "
														 "VALUES ( "
														 "'admin', "
														 "'Administrator', "
														 "DATETIME(), "
														 "0, 0, 0, 0, "
														 "'da39a3ee5e6b4b0d3255bfef95601890afd80709', "
														 "'wired.account.color.red', "
														 "1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, "
														 "1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, "
														 "1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, "
														 "1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1)"),
									 NULL)) {
		wi_log_fatal(WI_STR("Could not execute database statement: %m"));
	}
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO users ("
														 "name, "
														 "full_name, "
														 "creation_time, "
														 "downloads, "
														 "download_transferred, "
														 "uploads, "
														 "upload_transferred, "
														 "password, "
														 "chat_create_chats, "
														 "message_send_messages, "
														 "file_list_files, "
														 "file_search_files, "
														 "file_get_info, "
														 "transfer_download_files, "
														 "transfer_upload_files, "
														 "transfer_upload_directories, "
														 "board_read_boards, "
														 "board_add_threads, "
														 "board_add_posts) "
														 "VALUES ( "
														 "'guest', "
														 "'Guest', "
														 "DATETIME(), "
														 "0, 0, 0, 0, "
														 "'da39a3ee5e6b4b0d3255bfef95601890afd80709', "
														 "1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1)"),
									 NULL)) {
		wi_log_fatal(WI_STR("Could not execute database statement: %m"));
	}
}



static void wd_accounts_convert_accounts(void) {
	wi_mutable_array_t		*users_fields;
	wi_mutable_array_t		*groups_fields;
	wi_runtime_instance_t	*instance;
	
	users_fields = wi_array_with_data(
		/* "Name" */
		wi_array_with_data(WI_STR("wired.account.name"),
						   WI_STR("wired.account.chat.create_chats"),
						   WI_STR("wired.account.message.send_messages"),
						   WI_STR("wired.account.board.read_boards"),
						   WI_STR("wired.account.file.list_files"),
						   WI_STR("wired.account.file.search_files"),
						   WI_STR("wired.account.file.get_info"),
						   NULL),
		/* "Password" */
		wi_array_with_data(WI_STR("wired.account.password"), NULL),
		/* "Group" */
		wi_array_with_data(WI_STR("wired.account.group"), NULL),
		/* "Can get user info" */
		wi_array_with_data(WI_STR("wired.account.user.get_info"), NULL),
		/* "Can broadcast" */
		wi_array_with_data(WI_STR("wired.account.message.broadcast"), NULL),
		/* "Can post news" */
		wi_array_with_data(WI_STR("wired.account.board.add_threads"),
						   WI_STR("wired.account.board.add_posts"),
						   WI_STR("wired.account.board.edit_own_posts"),
						   WI_STR("wired.account.board.delete_own_posts"),
						   NULL),
		/* "Can clear news" */
		wi_array_with_data(WI_STR("wired.account.board.add_boards"),
						   WI_STR("wired.account.board.move_boards"),
						   WI_STR("wired.account.board.rename_boards"),
						   WI_STR("wired.account.board.delete_boards"),
						   WI_STR("wired.account.board.get_board_info"),
						   WI_STR("wired.account.board.set_board_info"),
						   WI_STR("wired.account.board.move_threads"),
						   WI_STR("wired.account.board.edit_all_posts"),
						   WI_STR("wired.account.board.delete_all_posts"),
						   WI_STR("wired.account.board.delete_threads"),
						   NULL),
		/* "Can download" */
		wi_array_with_data(WI_STR("wired.account.transfer.download_files"), NULL),
		/* "Can upload" */
		wi_array_with_data(WI_STR("wired.account.transfer.upload_files"),
						   WI_STR("wired.account.transfer.upload_directories"),
						   NULL),
		/* "Can upload anywhere" */
		wi_array_with_data(WI_STR("wired.account.transfer.upload_anywhere"), NULL),
		/* "Can create folders" */
		wi_array_with_data(WI_STR("wired.account.file.create_directories"), NULL),
		/* "Can move files" */
		wi_array_with_data(WI_STR("wired.account.file.move_files"),
						   WI_STR("wired.account.file.rename_files"),
						   WI_STR("wired.account.file.create_links"),
						   WI_STR("wired.account.file.rename_files"),
						   WI_STR("wired.account.file.set_label"),
						   WI_STR("wired.account.file.set_type"),
						   WI_STR("wired.account.file.set_comment"),
						   WI_STR("wired.account.file.set_permissions"),
						   WI_STR("wired.account.file.set_executable"),
						   NULL),
		/* "Can delete files" */
		wi_array_with_data(WI_STR("wired.account.file.delete_files"), NULL),
		/* "Can view drop boxes" */
		wi_array_with_data(WI_STR("wired.account.file.access_all_dropboxes"), NULL),
		/* "Can create accounts" */
		wi_array_with_data(WI_STR("wired.account.account.create_users"),
						   WI_STR("wired.account.account.create_groups"),
						   NULL),
		/* "Can edit accounts" */
		wi_array_with_data(WI_STR("wired.account.account.list_accounts"),
						   WI_STR("wired.account.account.read_accounts"),
						   WI_STR("wired.account.account.edit_users"),
						   WI_STR("wired.account.account.edit_groups"),
						   NULL),
		/* "Can delete accounts" */
		wi_array_with_data(WI_STR("wired.account.account.delete_users"),
						   WI_STR("wired.account.account.delete_groups"),
						   NULL),
		/* "Can elevate privileges" */
		wi_array_with_data(WI_STR("wired.account.account.raise_account_privileges"),
						   WI_STR("wired.account.account.change_password"),
						   WI_STR("wired.account.user.get_users"),
						   WI_STR("wired.account.log.view_log"),
						   WI_STR("wired.account.events.view_events"),
						   WI_STR("wired.account.settings.get_settings"),
						   WI_STR("wired.account.settings.set_settings"),
						   WI_STR("wired.account.banlist.get_bans"),
						   WI_STR("wired.account.banlist.add_bans"),
						   WI_STR("wired.account.banlist.delete_bans"),
						   NULL),
		/* "Can kick users" */
		wi_array_with_data(WI_STR("wired.account.chat.kick_users"),
						   WI_STR("wired.account.user.disconnect_users"),
						   NULL),
		/* "Can ban users" */
		wi_array_with_data(WI_STR("wired.account.user.ban_users"), NULL),
		/* "Cannot be kicked" */
		wi_array_with_data(WI_STR("wired.account.user.cannot_be_disconnected"), NULL),
		/* "Download speed limit" */
		wi_array_with_data(WI_STR("wired.account.transfer.download_speed_limit"), NULL),
		/* "Upload speed limit" */
		wi_array_with_data(WI_STR("wired.account.transfer.upload_speed_limit"), NULL),
		/* "Download limit" */
		wi_array_with_data(WI_STR("wired.account.transfer.download_limit"), NULL),
		/* "Upload limit" */
		wi_array_with_data(WI_STR("wired.account.transfer.upload_limit"), NULL),
		/* "Can set topic" */
		wi_array_with_data(WI_STR("wired.account.chat.set_topic"), NULL),
		NULL);
	
	groups_fields = wi_autorelease(wi_mutable_copy(users_fields));
	
	wi_mutable_array_remove_data_in_range(groups_fields, wi_make_range(1, 2));
	
	if(wi_fs_path_exists(WI_STR("users"), NULL)) {
		instance = wi_plist_read_instance_from_file(WI_STR("users"));
		
		if(!instance) {
			if(wd_accounts_convert_accounts_from_1_3(WI_STR("users"), users_fields))
				wi_log_info(WI_STR("Migrated users to \"users\""));
		} else {
			if(wi_runtime_id(instance) == wi_array_runtime_id()) {
				if(wd_accounts_convert_accounts_from_2_0b1(WI_STR("users"), instance))
					wi_log_info(WI_STR("Migrated users to \"users\""));
			}
		}
		
		wi_sqlite3_begin_immediate_transaction(wd_database);
		
		if(wd_accounts_convert_accounts_from_2_0b2(WI_STR("users"), WI_STR("users"), WD_ACCOUNT_FIELD_USER | WD_ACCOUNT_FIELD_USER_LIST)) {
			wi_log_info(WI_STR("Migrated users to database"));
			wi_fs_delete_path(WI_STR("users"));
			
			wi_sqlite3_commit_transaction(wd_database);
		} else {
			wi_sqlite3_rollback_transaction(wd_database);
		}
	}
	
	if(wi_fs_path_exists(WI_STR("groups"), NULL)) {
		instance = wi_plist_read_instance_from_file(WI_STR("groups"));
		
		if(!instance) {
			if(wd_accounts_convert_accounts_from_1_3(WI_STR("groups"), groups_fields))
				wi_log_info(WI_STR("Migrated groups to \"groups\""));
		} else {
			if(wi_runtime_id(instance) == wi_array_runtime_id()) {
				if(wd_accounts_convert_accounts_from_2_0b1(WI_STR("groups"), instance))
					wi_log_info(WI_STR("Migrated groups to \"groups\""));
			}
		}
		
		wi_sqlite3_begin_immediate_transaction(wd_database);
		
		if(wd_accounts_convert_accounts_from_2_0b2(WI_STR("groups"), WI_STR("groups"), WD_ACCOUNT_FIELD_GROUP | WD_ACCOUNT_FIELD_GROUP_LIST)) {
			wi_log_info(WI_STR("Migrated groups to database"));
			wi_fs_delete_path(WI_STR("groups"));
			
			wi_sqlite3_commit_transaction(wd_database);
		} else {
			wi_sqlite3_rollback_transaction(wd_database);
		}
	}
}



static wi_boolean_t wd_accounts_convert_accounts_from_1_3(wi_string_t *path, wi_array_t *fields) {
	wi_enumerator_t				*enumerator;
	wi_file_t					*file;
	wi_string_t					*string, *name, *value, *color;
	wi_array_t					*array;
	wi_mutable_dictionary_t		*values, *dictionary;
	wi_dictionary_t				*field;
	wi_number_t					*number;
	wi_runtime_instance_t		*instance;
	wi_uinteger_t				i, count;
	wd_account_field_type_t		type;
	
	dictionary		= wi_mutable_dictionary();
	count			= wi_array_count(fields);
	file			= wi_file_for_reading(path);
	
	if(!file) {
		wi_log_error(WI_STR("Could not read accounts from \"%@\": %m"), path);
		
		return false;
	}
	
	while((string = wi_file_read_config_line(file))) {
		array = wi_string_components_separated_by_string(string, WI_STR(":"));
		
		if(wi_array_count(array) > 0) {
			values = wi_mutable_dictionary();
			
			for(i = 0; i < count; i++) {
				if(i < wi_array_count(array)) {
					enumerator = wi_array_data_enumerator(WI_ARRAY(fields, i));
					
					while((name = wi_enumerator_next_data(enumerator))) {
						field		= wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, name);
						type		= wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)));
						value		= WI_ARRAY(array, i);
						instance	= NULL;
						
						switch(type) {
							case WD_ACCOUNT_FIELD_STRING:
								instance = value;
								
								if(wi_is_equal(name, WI_STR("wired.account.password")) && wi_string_length(instance) == 0)
									instance = wi_string_sha1(WI_STR(""));
								break;

							case WD_ACCOUNT_FIELD_DATE:
								instance = wi_date_with_rfc3339_string(wi_string_by_replacing_string_with_string(value, WI_STR(";"), WI_STR(":"), 0));
								break;

							case WD_ACCOUNT_FIELD_NUMBER:
								if(wi_string_integer(value) != 0)
									instance = wi_number_with_integer(wi_string_integer(value));
								break;

							case WD_ACCOUNT_FIELD_BOOLEAN:
								if(i == 0)
									instance = wi_number_with_bool(true);
								else if(wi_string_integer(value) != 0)
									instance = wi_number_with_bool(wi_string_integer(value));
								break;

							case WD_ACCOUNT_FIELD_ENUM:
								instance = value;
								break;

							case WD_ACCOUNT_FIELD_LIST:
								instance = wi_string_components_separated_by_string(value, WI_STR(","));
								
								if(wi_array_count(instance) == 1 && wi_string_length(WI_ARRAY(instance, 0)) == 0)
									instance = NULL;
								break;
						}
						
						if(instance)
							wi_mutable_dictionary_set_data_for_key(values, instance, name);
					}
				}
			}
			
			if(wi_dictionary_count(values) > 0) {
				if((number = wi_dictionary_data_for_key(values, WI_STR("wired.account.user.disconnect_users"))) && wi_number_bool(number))
					color = WI_STR("wired.account.color.red");
				else if((number = wi_dictionary_data_for_key(values, WI_STR("wired.account.user.ban_users"))) && wi_number_bool(number))
					color = WI_STR("wired.account.color.red");
				else
					color = NULL;

				if(color)
					wi_mutable_dictionary_set_data_for_key(values, color, WI_STR("wired.account.color"));
				
				name = wi_autorelease(wi_retain(wi_dictionary_data_for_key(values, WI_STR("wired.account.name"))));
				
				wi_mutable_dictionary_remove_data_for_key(values, name);
				wi_mutable_dictionary_set_data_for_key(dictionary, values, name);
			}
		}
	}
	
	if(!wi_plist_write_instance_to_file(dictionary, path)) {
		wi_log_error(WI_STR("Could not write accounts to \"%@\": %m"), path);
	
		return false;
	}
		
	return true;
}



static wi_boolean_t wd_accounts_convert_accounts_from_2_0b1(wi_string_t *path, wi_array_t *accounts) {
	wi_mutable_dictionary_t		*values, *dictionary;
	wi_string_t					*name;
	wi_uinteger_t				i, count;
	
	dictionary		= wi_mutable_dictionary();
	count			= wi_array_count(accounts);
	
	for(i = 0; i < count; i++) {
		values		= wi_autorelease(wi_mutable_copy(WI_ARRAY(accounts, i)));
		name		= wi_autorelease(wi_retain(wi_dictionary_data_for_key(values, WI_STR("wired.account.name"))));
		
		wi_mutable_dictionary_remove_data_for_key(values, WI_STR("wired.account.name"));
		wi_mutable_dictionary_set_data_for_key(dictionary, values, name);
	}
	
	if(!wi_plist_write_instance_to_file(dictionary, path)) {
		wi_log_error(WI_STR("Could not write accounts to \"%@\": %m"), path);
		
		return false;
	}
	
	return true;
}



static wi_boolean_t wd_accounts_convert_accounts_from_2_0b2(wi_string_t *path, wi_string_t *table, wi_uinteger_t accounttype) {
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*results;
	wi_string_t					*name, *string;
	wi_runtime_instance_t		*instance;
	wd_account_t				*account;
	
	instance = wi_plist_read_instance_from_file(path);
	
	if(!instance) {
		wi_log_error(WI_STR("Could not read accounts from \"%@\": %m"), path);
		
		return false;
	}
	
	if(wi_runtime_id(instance) != wi_dictionary_runtime_id()) {
		wi_log_error(WI_STR("Could not read accounts from \"%@\": Wrong format"), path);
		
		return false;
	}
	
	enumerator = wi_dictionary_key_enumerator(instance);
	
	while((name = wi_enumerator_next_data(enumerator))) {
		results = wi_sqlite3_execute_statement(wd_database, wi_string_with_format(WI_STR("SELECT name FROM %@ WHERE name = ?"), table), name, NULL);
		
		if(!results) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
			
			return false;
		}
		
		if(wi_dictionary_count(results) == 0) {
			account		= wi_autorelease(wd_account_init_with_name_and_values(wd_account_alloc(), name, wi_dictionary_data_for_key(instance, name)));
			string		= wd_account_sqlite3_insert_string(account, accounttype);
			
			if(!wi_sqlite3_execute_statement(wd_database, wi_string_with_format(WI_STR("INSERT INTO %@ %@"), table, string), NULL)) {
				wi_log_error(WI_STR("Could not execute database statement: %m"));
				
				return false;
			}
		}
	}
	
	return true;
}



#pragma mark -

static void wd_accounts_reload_user_account(wd_account_t *account) {
	wi_enumerator_t		*enumerator;
	wi_string_t			*name;
	wd_user_t			*user;
	wd_account_t		*useraccount;
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		useraccount = wd_user_account(user);
		
		if(useraccount && wi_is_equal(wd_account_name(useraccount), wd_account_name(account))) {
			name = wd_account_new_name(account);
			
			if(wi_string_length(name) == 0)
				name = wd_account_name(account);

			wd_accounts_reload_account(user, name);
		}
	}
	
	wi_dictionary_unlock(wd_users);
}



static void wd_accounts_reload_group_account(wd_account_t *account) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	wd_account_t		*useraccount;
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		useraccount = wd_user_account(user);
		
		if(useraccount && wi_is_equal(wd_account_group(useraccount), wd_account_name(account)))
			wd_accounts_reload_account(user, wd_account_name(useraccount));
	}
	
	wi_dictionary_unlock(wd_users);
}



static void wd_accounts_reload_account(wd_user_t *user, wi_string_t *name) {
	wi_string_t			*newnick;
	wd_account_t		*oldaccount, *newaccount;
	wi_p7_enum_t		newcolor;
	wi_boolean_t		changed = false;
	
	oldaccount = wd_user_account(user);
	newaccount = wd_accounts_read_user_and_group(name);
	
	if(!newaccount)
		return;
	
	wd_user_set_account(user, newaccount);
	
	wd_user_set_login(user, wd_account_name(newaccount));
	
	newcolor = wd_account_color(newaccount);
	
	if(wd_user_color(user) != newcolor) {
		wd_user_set_color(user, newcolor);
		
		changed = true;
	}
	
	if(wd_account_user_cannot_set_nick(newaccount)) {
		newnick = wd_account_nick(newaccount);
		
		if(!wi_is_equal(wd_user_nick(user), newnick)) {
			wd_user_set_nick(user, newnick);
			
			changed = true;
		}
	}
	
	if(changed)
		wd_user_broadcast_status(user);
	
	if(!wd_account_account_list_accounts(newaccount) && wd_user_is_subscribed_accounts(user))
		wd_user_unsubscribe_accounts(user);
	
	if(!wd_account_board_read_boards(newaccount) && wd_user_is_subscribed_boards(user))
		wd_user_unsubscribe_boards(user);
	
	if(!wd_account_log_view_log(newaccount) && wd_user_is_subscribed_log(user))
		wd_user_unsubscribe_log(user);
	
	if(!wd_account_file_list_files(newaccount) && wi_set_count(wd_user_subscribed_paths(user)) > 0)
		wd_user_unsubscribe_paths(user);
	
	wd_user_send_message(user, wd_account_privileges_message(newaccount));
	
	wd_boards_reload_account(user, oldaccount, newaccount);
}



static wi_boolean_t wd_accounts_rename_group(wi_string_t *name, wi_string_t *newname) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_mutable_array_t			*newgroups;
	wi_array_t					*groups;
	wi_uinteger_t				index;
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE users SET `group` = ? WHERE `group` = ?"),
									 (newname && wi_string_length(newname) > 0) ? newname: wi_null(),
									 name,
									 NULL)) {
		return false;
	}
        
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT `name`, `groups` FROM `users`"), NULL);
	    
	if(!statement)
		return false;
	    
    
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
        wi_string_t *result;
        result = wi_dictionary_data_for_key(results, WI_STR("groups"));
        
        if(result != wi_null()) {
            groups		= wi_string_components_separated_by_string(result, WI_STR("\34"));
            index		= wi_array_index_of_data(groups, name);
            
            if(index != WI_NOT_FOUND) {
                
                newgroups = wi_autorelease(wi_mutable_copy(groups));
                
                if(newname)
                    wi_mutable_array_replace_data_at_index(newgroups, newname, index);
                else
                    wi_mutable_array_remove_data_at_index(newgroups, index);
                
                if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE users SET groups = ? WHERE name = ?"),
                                                 wi_array_components_joined_by_string(newgroups, WI_STR("\34")),
                                                 wi_dictionary_data_for_key(results, WI_STR("name")),
                                                 NULL)) {
                    return false;
                }
            }
        }
	}
	return true;
}



static void wd_accounts_notify_subscribers(void) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*message;
	wd_user_t			*user;
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN && wd_user_is_subscribed_accounts(user)) {
			message = wi_p7_message_with_name(WI_STR("wired.account.accounts_changed"), wd_p7_spec);
			wd_user_send_message(user, message);
		}
	}
	
	wi_dictionary_unlock(wd_users);
}



#pragma mark -

wi_boolean_t wd_accounts_reply_user_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_p7_message_t				*reply;
	wi_string_t					*group;
	wd_account_t				*useraccount, *groupaccount;
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT * FROM users"), NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		useraccount		= wi_autorelease(wd_account_init_with_sqlite3_results(wd_account_alloc(), results));
		group			= wd_account_group(useraccount);
		
		if(group && wi_string_length(group) > 0) {
			groupaccount = wd_accounts_read_group(group);
			
			if(groupaccount)
				wd_account_override_privileges(useraccount, groupaccount);
		}
		
		reply = wi_p7_message_with_name(WI_STR("wired.account.user_list"), wd_p7_spec);
		wd_account_write_to_message(useraccount, WD_ACCOUNT_FIELD_USER_LIST, reply);
		wd_user_reply_message(user, reply, message);
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.user_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	return true;
}



wi_boolean_t wd_accounts_reply_group_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_p7_message_t				*reply;
	wd_account_t				*account;
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT * FROM groups"), NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		account = wi_autorelease(wd_account_init_with_sqlite3_results(wd_account_alloc(), results));
		
		reply = wi_p7_message_with_name(WI_STR("wired.account.group_list"), wd_p7_spec);
		wd_account_write_to_message(account, WD_ACCOUNT_FIELD_GROUP_LIST, reply);
		wd_user_reply_message(user, reply, message);
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.group_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	return true;
}



#pragma mark -

wd_account_t * wd_account_alloc(void) {
	return wi_runtime_create_instance(wd_account_runtime_id, sizeof(wd_account_t));
}



static wd_account_t * wd_account_init(wd_account_t *account) {
	return account;
}



static wd_account_t * wd_account_init_with_sqlite3_results(wd_account_t *account, wi_dictionary_t *results) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*name, *protocolname;
	wi_runtime_instance_t	*value;
	
	account->values		= wi_dictionary_init(wi_mutable_dictionary_alloc());
	enumerator			= wi_dictionary_key_enumerator(results);
	
	while((name = wi_enumerator_next_data(enumerator))) {
		value = wi_dictionary_data_for_key(results, name);
		
		if(value != wi_null()) {
			field			= wi_dictionary_data_for_key(wd_account_fields_by_table_name, name);
			protocolname	= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_PROTOCOL_NAME));
			
			switch((wd_account_field_type_t) wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
				case WD_ACCOUNT_FIELD_STRING:
				case WD_ACCOUNT_FIELD_NUMBER:
				case WD_ACCOUNT_FIELD_BOOLEAN:
				case WD_ACCOUNT_FIELD_ENUM:
					wi_mutable_dictionary_set_data_for_key(account->values, value, protocolname);
					break;
					
				case WD_ACCOUNT_FIELD_DATE:
					wi_mutable_dictionary_set_data_for_key(account->values, wi_date_with_sqlite3_string(value), protocolname);
					break;
					
				case WD_ACCOUNT_FIELD_LIST:
					wi_mutable_dictionary_set_data_for_key(account->values, wi_string_components_separated_by_string(value, WI_STR("\34")), protocolname);
					break;
			}
		}
	}
	
	return account;
}



static wd_account_t * wd_account_init_with_name_and_values(wd_account_t *account, wi_string_t *name, wi_dictionary_t *values) {
	account->values = wi_mutable_copy(values);
	
	wi_mutable_dictionary_set_data_for_key(account->values, name, WI_STR("wired.account.name"));
	
	return account;
}



static wi_runtime_instance_t * wd_account_copy(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	wd_account_t		*account_copy;
	
	account_copy = wd_account_init(wd_account_alloc());
	
	account_copy->values = wi_mutable_copy(account->values);
	
	return account_copy;
}



wd_account_t * wd_account_init_with_message(wd_account_t *account, wi_p7_message_t *message) {
	account->values = wi_dictionary_init(wi_mutable_dictionary_alloc());

	wd_account_read_from_message(account, message);
	
	return account;
}



static wi_string_t * wd_account_description(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	
	return wi_description(account->values);
}



static void wd_account_dealloc(wi_runtime_instance_t *instance) {
	wd_account_t		*account = instance;
	
	wi_release(account->values);
}



#pragma mark -

static wi_string_t * wd_account_sqlite3_insert_string(wd_account_t *account, wi_uinteger_t accounttype) {
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*field;
	wi_mutable_string_t			*sqlcolumns, *sqlvalues;
	wi_string_t					*name, *tablename;
	wi_runtime_instance_t		*value;
	wd_account_field_type_t		type;
	
	sqlcolumns		= wi_mutable_string();
	sqlvalues		= wi_mutable_string();
	enumerator		= wi_dictionary_key_enumerator(account->values);
	
	while((name = wi_enumerator_next_data(enumerator))) {
		field = wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, name);
		
		if(field) {
			if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & accounttype) {
				type		= wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)));
				tablename	= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TABLE_NAME));
				value		= wi_dictionary_data_for_key(account->values, name);
				
				if(wi_string_length(sqlcolumns) > 0)
					wi_mutable_string_append_string(sqlcolumns, WI_STR(", "));
				
				wi_mutable_string_append_format(sqlcolumns, WI_STR("`%@`"), tablename);
				
				if(wi_string_length(sqlvalues) > 0)
					wi_mutable_string_append_string(sqlvalues, WI_STR(", "));
				
				switch(type) {
					case WD_ACCOUNT_FIELD_STRING:
						wi_mutable_string_append_format(sqlvalues, WI_STR("'%q'"), value);
						break;
						
					case WD_ACCOUNT_FIELD_DATE:
						wi_mutable_string_append_format(sqlvalues, WI_STR("'%q'"), wi_date_sqlite3_string(value));
						break;
						
					case WD_ACCOUNT_FIELD_NUMBER:
					case WD_ACCOUNT_FIELD_BOOLEAN:
					case WD_ACCOUNT_FIELD_ENUM:
						wi_mutable_string_append_format(sqlvalues, WI_STR("%u"), wi_number_integer(value));
						break;
						
					case WD_ACCOUNT_FIELD_LIST:
						wi_mutable_string_append_format(sqlvalues, WI_STR("'%q'"), wi_array_components_joined_by_string(value, WI_STR("\34")));
						break;
				}
			}
		}
	}
	
	return wi_string_with_format(WI_STR("(%@) VALUES (%@)"), sqlcolumns, sqlvalues);
}



static wi_string_t * wd_account_sqlite3_update_string(wd_account_t *account, wi_uinteger_t accounttype) {
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*field;
	wi_mutable_string_t			*string;
	wi_string_t					*name, *tablename;
	wi_runtime_instance_t		*value;
	
	string			= wi_mutable_string();
	enumerator		= wi_dictionary_key_enumerator(wd_account_fields_by_protocol_name);
	
	while((name = wi_enumerator_next_data(enumerator))) {
		field = wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, name);

		if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & accounttype) {
			tablename	= wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TABLE_NAME));
			value		= wi_dictionary_data_for_key(account->values, name);
			
			if(wi_string_length(string) > 0)
				wi_mutable_string_append_string(string, WI_STR(", "));
			
			wi_mutable_string_append_format(string, WI_STR("`%@` = "), tablename);
			
			if(value) {
				switch((wd_account_field_type_t) wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
					case WD_ACCOUNT_FIELD_STRING:
					case WD_ACCOUNT_FIELD_ENUM:
						wi_mutable_string_append_format(string, WI_STR("'%q'"), value);
						break;
						
					case WD_ACCOUNT_FIELD_DATE:
						wi_mutable_string_append_format(string, WI_STR("'%q'"), wi_date_sqlite3_string(value));
						break;
						
					case WD_ACCOUNT_FIELD_NUMBER:
					case WD_ACCOUNT_FIELD_BOOLEAN: 
                        wi_mutable_string_append_format(string, WI_STR("%u"), wi_number_integer(value));
                        break;
						
					case WD_ACCOUNT_FIELD_LIST:
						wi_mutable_string_append_format(string, WI_STR("'%q'"), wi_array_components_joined_by_string(value, WI_STR("\34")));
						break;
				}
			} else {
				wi_mutable_string_append_format(string, WI_STR("NULL"));
			}
		}
	}

	return string;
}



#pragma mark -

static void wd_account_read_from_message(wd_account_t *account, wi_p7_message_t *message) {
        
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*name;
	wi_runtime_instance_t	*instance;
	wi_boolean_t			required;
	
	enumerator = wi_dictionary_key_enumerator(wd_account_fields_by_protocol_name);
			
    // enumerate each account setting field
	while((name = wi_enumerator_next_data(enumerator))) {
		field		= wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, name);
		required	= wi_number_bool(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_REQUIRED)));

        // store value depending of field type (string, bool, date, number, etc)
		switch((wd_account_field_type_t) wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
			case WD_ACCOUNT_FIELD_STRING:
				instance = wi_p7_message_string_for_name(message, name);
                
				if(instance)
					wi_mutable_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_mutable_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_DATE:
				instance = wi_p7_message_date_for_name(message, name);
				
				if(instance)
					wi_mutable_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_mutable_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_NUMBER:
			case WD_ACCOUNT_FIELD_BOOLEAN:
				instance = wi_p7_message_number_for_name(message, name);
				
				if(instance)
					wi_mutable_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_mutable_dictionary_remove_data_for_key(account->values, name);
				break;
			
			case WD_ACCOUNT_FIELD_ENUM:
				instance = wi_p7_message_enum_name_for_name(message, name);
				
				if(instance)
					wi_mutable_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_mutable_dictionary_remove_data_for_key(account->values, name);
				break;

			case WD_ACCOUNT_FIELD_LIST:
				instance = wi_p7_message_list_for_name(message, name);
				
				if(instance)
					wi_mutable_dictionary_set_data_for_key(account->values, instance, name);
				else if(!required)
					wi_mutable_dictionary_remove_data_for_key(account->values, name);
				break;
		}
	}
}



static void wd_account_write_to_message(wd_account_t *account, wi_uinteger_t accounttype, wi_p7_message_t *message) {
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*field;
	wi_string_t					*name;
	wi_runtime_instance_t		*instance;
	wi_boolean_t				required;
	
	enumerator = wi_dictionary_key_enumerator(wd_account_fields_by_protocol_name);
	
	while((name = wi_enumerator_next_data(enumerator))) {
		field = wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, name);

		if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & accounttype) {
			instance = wi_dictionary_data_for_key(account->values, name);
			
			if(accounttype == WD_ACCOUNT_FIELD_PRIVILEGE)
				required = true;
			else
				required = wi_number_bool(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_REQUIRED)));
			
			switch((wd_account_field_type_t) wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)))) {
				case WD_ACCOUNT_FIELD_STRING:
					if(!instance && required)
						instance = WI_STR("");
					
					if(instance)
						wi_p7_message_set_string_for_name(message, instance, name);
					break;

				case WD_ACCOUNT_FIELD_DATE:
					if(!instance && required)
						instance = wi_date_with_time(0);
					
					if(instance)
						wi_p7_message_set_date_for_name(message, instance, name);
					break;

				case WD_ACCOUNT_FIELD_NUMBER:
				case WD_ACCOUNT_FIELD_BOOLEAN:
					if(!instance && required)
						instance = wi_number_with_integer(0);
					
					if(instance)
						wi_p7_message_set_number_for_name(message, instance, name);
					break;
				
				case WD_ACCOUNT_FIELD_ENUM:
					
					if(!instance && required)
						wi_p7_message_set_enum_for_name(message, 0, name);
					else if(instance)
						wi_p7_message_set_enum_name_for_name(message, instance, name);
					break;

				case WD_ACCOUNT_FIELD_LIST:
					if(!instance && required)
						instance = wi_array();
					
					if(instance)
						wi_p7_message_set_list_for_name(message, instance, name);
					break;
			}
		}
	}
}



static void wd_account_override_privileges(wd_account_t *account1, wd_account_t *account2) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*field;
	wi_string_t				*field_name;
	wi_runtime_instance_t	*value;

	enumerator = wi_dictionary_key_enumerator(wd_account_fields_by_protocol_name);
    
    while((field_name = wi_enumerator_next_data(enumerator))) {

		field = wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, field_name);
		
		if(!wi_is_equal(WI_STR("wired.account.name"), field_name) &&
           wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & WD_ACCOUNT_FIELD_PRIVILEGE ||
		   wi_is_equal(field_name, WI_STR("wired.account.files")) ||
		   wi_is_equal(field_name, WI_STR("wired.account.color"))) {
            
			value = wi_dictionary_data_for_key(account1->values, field_name);
                     
			if(!value) {
				value = wi_dictionary_data_for_key(account2->values, field_name);
				
				if(value)
                    wi_mutable_dictionary_set_data_for_key(account1->values, value, field_name);
                
			} else if(value && wi_number_bool(value) == false) {                
                value = wi_dictionary_data_for_key(account2->values, field_name);
                        
				if(value)
					wi_mutable_dictionary_set_data_for_key(account1->values, value, field_name);
                
            }
		}
	}
}



#pragma mark -

void wd_account_update_from_message(wd_account_t *account, wi_p7_message_t *message) {
	wd_account_read_from_message(account, message);
}



#pragma mark -

void wd_account_reply_user_account(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.user"), wd_p7_spec);
	wd_account_write_to_message(account, WD_ACCOUNT_FIELD_USER, reply);
	wd_user_reply_message(user, reply, message);
}



void wd_account_reply_group_account(wd_account_t *account, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.account.group"), wd_p7_spec);
	wd_account_write_to_message(account, WD_ACCOUNT_FIELD_GROUP, reply);
	wd_user_reply_message(user, reply, message);
}



wi_p7_message_t * wd_account_privileges_message(wd_account_t *account) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.account.privileges"), wd_p7_spec);
	wd_account_write_to_message(account, WD_ACCOUNT_FIELD_PRIVILEGE, message);
	
	return message;
}



#pragma mark -

wi_string_t * wd_account_nick(wd_account_t *account) {
	wi_string_t		*fullname;
	
	fullname = wd_account_full_name(account);
	
	if(fullname && wi_string_length(fullname) > 0)
		return fullname;
	
	return wd_account_name(account);
}



#pragma mark -

wi_boolean_t wd_account_verify_privileges_for_user(wd_account_t *account, wd_user_t *user, wi_string_t **error) {
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*field;
	wi_string_t					*name, *group;
	wi_runtime_instance_t		*instance1, *instance2;
	wd_account_t				*newaccount, *useraccount, *groupaccount;
	wd_account_field_type_t		type;
	wi_integer_t				integer1, integer2;
	wi_boolean_t				boolean1, boolean2;
	
	newaccount = wi_autorelease(wi_copy(account));
	group = wd_account_group(newaccount);
	
	if(group && wi_string_length(group) > 0) {
		groupaccount = wd_accounts_read_group(group);
		
		if(groupaccount)
			wd_account_override_privileges(newaccount, groupaccount);
	}
	
	useraccount = wd_user_account(user);
	
	if(!wd_account_account_raise_account_privileges(useraccount)) {
		enumerator = wi_dictionary_key_enumerator(wd_account_fields_by_protocol_name);
			
		while((name = wi_enumerator_next_data(enumerator))) {
			field = wi_dictionary_data_for_key(wd_account_fields_by_protocol_name, name);
			
			if(wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_ACCOUNT))) & WD_ACCOUNT_FIELD_PRIVILEGE) {
				type		= wi_number_int32(wi_dictionary_data_for_key(field, WI_STR(WD_ACCOUNT_FIELD_TYPE)));
				instance1	= wi_dictionary_data_for_key(useraccount->values, name);
				instance2	= wi_dictionary_data_for_key(newaccount->values, name);
				
				switch(type) {
					case WD_ACCOUNT_FIELD_STRING:
					case WD_ACCOUNT_FIELD_DATE:
					case WD_ACCOUNT_FIELD_LIST:
					case WD_ACCOUNT_FIELD_ENUM:
						break;
					
					case WD_ACCOUNT_FIELD_NUMBER:
						integer1 = instance1 ? wi_number_integer(instance1) : 0;
						integer2 = instance2 ? wi_number_integer(instance2) : 0;
						
						if(integer1 > 0 && (integer2 > integer1 || integer2 == 0)) {
							*error = wi_string_with_format(WI_STR("Tried to increase \"%@\" to %u"),
								name, integer2);

							return false;
						}
						break;
					
					case WD_ACCOUNT_FIELD_BOOLEAN:
						boolean1 = instance1 ? wi_number_bool(instance1) : false;
						boolean2 = instance2 ? wi_number_bool(instance2) : false;
						
						if(!boolean1 && boolean2) {
							*error = wi_string_with_format(WI_STR("Tried to enable \"%@\""), name);
							
							return false;
						}
						break;
				}
			}
		}
			
		instance1 = wi_dictionary_data_for_key(useraccount->values, WI_STR("wired.account.files"));
		instance2 = wi_dictionary_data_for_key(newaccount->values, WI_STR("wired.account.files"));
		
		if(!instance1)
			instance1 = WI_STR("");
		
		if(!instance2)
			instance2 = WI_STR("");
		
		if(!wi_string_has_prefix(instance2, instance1))
			return false;
	}
	
	return true;
}



#pragma mark -

#define WD_ACCOUNT_STRING_ACCESSOR(name, fieldname)										\
	wi_string_t * (name)(wd_account_t *account) {										\
		wi_string_t		*string;														\
																						\
		if((string = wi_dictionary_data_for_key((account)->values, (fieldname))))		\
			return string;																\
																						\
		return WI_STR("");																\
	}

#define WD_ACCOUNT_DATE_ACCESSOR(name, fieldname)										\
	wi_date_t * (name)(wd_account_t *account) {											\
		return wi_dictionary_data_for_key((account)->values, (fieldname));				\
	}

#define WD_ACCOUNT_BOOLEAN_ACCESSOR(name, fieldname)									\
	wi_boolean_t (name)(wd_account_t *account) {										\
		wi_number_t		*number;														\
																						\
		if((number = wi_dictionary_data_for_key((account)->values, (fieldname))))		\
			return wi_number_bool(number);												\
																						\
		return false;																	\
	}

#define WD_ACCOUNT_NUMBER_ACCESSOR(name, fieldname)										\
	wi_uinteger_t (name)(wd_account_t *account) {										\
		wi_number_t		*number;														\
																						\
		if((number = wi_dictionary_data_for_key((account)->values, (fieldname))))		\
			return wi_number_integer(number);											\
																						\
		return 0;																		\
	}

#define WD_ACCOUNT_ENUM_ACCESSOR(name, fieldname)										\
	wi_p7_enum_t (name)(wd_account_t *account) {										\
		wi_p7_spec_field_t		*field;													\
		wi_string_t				*enumname;												\
																						\
		if((enumname = wi_dictionary_data_for_key((account)->values, (fieldname)))) {	\
			field = wi_p7_spec_field_with_name(wd_p7_spec, (fieldname));				\
																						\
			if(field) {																	\
				return (wi_p7_enum_t) (intptr_t) wi_dictionary_data_for_key(			\
					wi_p7_spec_field_enums_by_name(field), (enumname));					\
			}																			\
		}																				\
																						\
		return 0;																		\
	}

#define WD_ACCOUNT_LIST_ACCESSOR(name, fieldname)										\
	wi_array_t * (name)(wd_account_t *account) {										\
		wi_array_t		*array;															\
																						\
		if((array = wi_dictionary_data_for_key((account)->values, (fieldname))))		\
			return array;																\
																						\
		return wi_array();																\
	}

WD_ACCOUNT_STRING_ACCESSOR(wd_account_name, WI_STR("wired.account.name"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_new_name, WI_STR("wired.account.new_name"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_full_name, WI_STR("wired.account.full_name"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_comment, WI_STR("wired.account.comment"))
WD_ACCOUNT_ENUM_ACCESSOR(wd_account_color, WI_STR("wired.account.color"))
WD_ACCOUNT_DATE_ACCESSOR(wd_account_creation_time, WI_STR("wired.account.creation_time"))
WD_ACCOUNT_DATE_ACCESSOR(wd_account_modification_time, WI_STR("wired.account.modification_time"))
WD_ACCOUNT_DATE_ACCESSOR(wd_account_login_time, WI_STR("wired.account.login_time"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_edited_by, WI_STR("wired.account.edited_by"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_password, WI_STR("wired.account.password"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_group, WI_STR("wired.account.group"))
WD_ACCOUNT_LIST_ACCESSOR(wd_account_groups, WI_STR("wired.account.groups"))
WD_ACCOUNT_STRING_ACCESSOR(wd_account_files, WI_STR("wired.account.files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_cannot_set_nick, WI_STR("wired.account.user.cannot_set_nick"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_get_info, WI_STR("wired.account.user.get_info"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_disconnect_users, WI_STR("wired.account.user.disconnect_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_ban_users, WI_STR("wired.account.user.ban_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_cannot_be_disconnected, WI_STR("wired.account.user.cannot_be_disconnected"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_user_get_users, WI_STR("wired.account.user.get_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_chat_kick_users, WI_STR("wired.account.chat.kick_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_chat_set_topic, WI_STR("wired.account.chat.set_topic"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_chat_create_chats, WI_STR("wired.account.chat.create_chats"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_message_send_messages, WI_STR("wired.account.message.send_messages"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_message_broadcast, WI_STR("wired.account.message.broadcast"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_read_boards, WI_STR("wired.account.board.read_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_add_boards, WI_STR("wired.account.board.add_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_move_boards, WI_STR("wired.account.board.move_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_rename_boards, WI_STR("wired.account.board.rename_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_delete_boards, WI_STR("wired.account.board.delete_boards"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_get_board_info, WI_STR("wired.account.board.get_board_info"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_set_board_info, WI_STR("wired.account.board.set_board_info"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_add_threads, WI_STR("wired.account.board.add_threads"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_move_threads, WI_STR("wired.account.board.move_threads"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_add_posts, WI_STR("wired.account.board.add_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_edit_own_threads_and_posts, WI_STR("wired.account.board.edit_own_threads_and_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_edit_all_threads_and_posts, WI_STR("wired.account.board.edit_all_threads_and_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_delete_own_threads_and_posts, WI_STR("wired.account.board.delete_own_threads_and_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_board_delete_all_threads_and_posts, WI_STR("wired.account.board.delete_all_threads_and_posts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_list_files, WI_STR("wired.account.file.list_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_search_files, WI_STR("wired.account.file.search_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_get_info, WI_STR("wired.account.file.get_info"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_create_directories, WI_STR("wired.account.file.create_directories"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_create_links, WI_STR("wired.account.file.create_links"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_move_files, WI_STR("wired.account.file.move_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_rename_files, WI_STR("wired.account.file.rename_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_type, WI_STR("wired.account.file.set_type"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_comment, WI_STR("wired.account.file.set_comment"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_permissions, WI_STR("wired.account.file.set_permissions"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_executable, WI_STR("wired.account.file.set_executable"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_set_label, WI_STR("wired.account.file.set_label"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_delete_files, WI_STR("wired.account.file.delete_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_file_access_all_dropboxes, WI_STR("wired.account.file.access_all_dropboxes"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_file_recursive_list_depth_limit, WI_STR("wired.account.file.recursive_list_depth_limit"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_download_files, WI_STR("wired.account.transfer.download_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_upload_files, WI_STR("wired.account.transfer.upload_files"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_upload_directories, WI_STR("wired.account.transfer.upload_directories"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_transfer_upload_anywhere, WI_STR("wired.account.transfer.upload_anywhere"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_download_limit, WI_STR("wired.account.transfer.download_limit"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_upload_limit, WI_STR("wired.account.transfer.upload_limit"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_download_speed_limit, WI_STR("wired.account.transfer.download_speed_limit"))
WD_ACCOUNT_NUMBER_ACCESSOR(wd_account_transfer_upload_speed_limit, WI_STR("wired.account.transfer.upload_speed_limit"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_change_password, WI_STR("wired.account.account.change_password"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_list_accounts, WI_STR("wired.account.account.list_accounts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_read_accounts, WI_STR("wired.account.account.read_accounts"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_create_users, WI_STR("wired.account.account.create_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_edit_users, WI_STR("wired.account.account.edit_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_delete_users, WI_STR("wired.account.account.delete_users"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_create_groups, WI_STR("wired.account.account.create_groups"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_edit_groups, WI_STR("wired.account.account.edit_groups"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_delete_groups, WI_STR("wired.account.account.delete_groups"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_account_raise_account_privileges, WI_STR("wired.account.account.raise_account_privileges"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_log_view_log, WI_STR("wired.account.log.view_log"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_events_view_events, WI_STR("wired.account.events.view_events"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_settings_get_settings, WI_STR("wired.account.settings.get_settings"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_settings_set_settings, WI_STR("wired.account.settings.set_settings"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_banlist_get_bans, WI_STR("wired.account.banlist.get_bans"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_banlist_add_bans, WI_STR("wired.account.banlist.add_bans"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_banlist_delete_bans, WI_STR("wired.account.banlist.delete_bans"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_tracker_list_servers, WI_STR("wired.account.tracker.list_servers"))
WD_ACCOUNT_BOOLEAN_ACCESSOR(wd_account_tracker_register_servers, WI_STR("wired.account.tracker.register_servers"))
