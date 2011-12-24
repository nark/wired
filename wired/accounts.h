/* $Id$ */

/*
 *  Copyright (c) 2003-2009 Axel Andersson
 *  All rights reserved_
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1_ Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer_
 *  2_ Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution_
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED_  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE_
 */

#ifndef WD_ACCOUNTS_H
#define WD_ACCOUNTS_H 1

#include <wired/wired.h>
#include "main.h"

typedef struct _wd_account			wd_account_t;


void								wd_accounts_initialize(void);

wd_account_t *						wd_accounts_read_user_and_group(wi_string_t *);
wd_account_t *						wd_accounts_read_user(wi_string_t *);
wd_account_t *						wd_accounts_read_group(wi_string_t *);
wi_string_t *						wd_accounts_password_for_user(wi_string_t *);
wi_boolean_t						wd_accounts_change_password(wd_account_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_create_user(wd_account_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_create_group(wd_account_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_edit_user(wd_account_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_edit_group(wd_account_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_delete_user(wd_account_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_delete_group(wd_account_t *, wd_user_t *, wi_p7_message_t *);
void								wd_accounts_update_login_time(wd_account_t *);
void								wd_accounts_add_download_statistics(wd_account_t *, wi_boolean_t, wi_file_offset_t);
void								wd_accounts_add_upload_statistics(wd_account_t *, wi_boolean_t, wi_file_offset_t);

wi_boolean_t						wd_accounts_reply_user_list(wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_accounts_reply_group_list(wd_user_t *, wi_p7_message_t *);

wd_account_t *						wd_account_alloc(void);
wd_account_t *						wd_account_init_with_message(wd_account_t *, wi_p7_message_t *);

void								wd_account_update_from_message(wd_account_t *, wi_p7_message_t *);

wi_p7_message_t *					wd_account_privileges_message(wd_account_t *);
void								wd_account_reply_user_account(wd_account_t *, wd_user_t *, wi_p7_message_t *);
void								wd_account_reply_group_account(wd_account_t *, wd_user_t *, wi_p7_message_t *);

wi_string_t *						wd_account_nick(wd_account_t *);

wi_boolean_t						wd_account_verify_privileges_for_user(wd_account_t *, wd_user_t *, wi_string_t **);

wi_string_t *						wd_account_name(wd_account_t *);
wi_string_t *						wd_account_new_name(wd_account_t *);
wi_string_t *						wd_account_full_name(wd_account_t *);
wi_string_t *						wd_account_comment(wd_account_t *);
wi_p7_enum_t						wd_account_color(wd_account_t *);
wi_date_t *							wd_account_creation_time(wd_account_t *);
wi_date_t *							wd_account_modification_time(wd_account_t *);
wi_date_t *							wd_account_login_time(wd_account_t *);
wi_string_t *						wd_account_edited_by(wd_account_t *);
wi_string_t *						wd_account_password(wd_account_t *);
wi_string_t *						wd_account_group(wd_account_t *);
wi_array_t *						wd_account_groups(wd_account_t *);
wi_string_t *						wd_account_files(wd_account_t *);
wi_boolean_t 						wd_account_user_cannot_set_nick(wd_account_t *);
wi_boolean_t 						wd_account_user_get_info(wd_account_t *);
wi_boolean_t 						wd_account_user_disconnect_users(wd_account_t *);
wi_boolean_t 						wd_account_user_ban_users(wd_account_t *);
wi_boolean_t 						wd_account_user_cannot_be_disconnected(wd_account_t *);
wi_boolean_t 						wd_account_user_get_users(wd_account_t *);
wi_boolean_t 						wd_account_chat_kick_users(wd_account_t *);
wi_boolean_t 						wd_account_chat_set_topic(wd_account_t *);
wi_boolean_t 						wd_account_chat_create_chats(wd_account_t *);
wi_boolean_t 						wd_account_message_send_messages(wd_account_t *);
wi_boolean_t 						wd_account_message_broadcast(wd_account_t *);
wi_boolean_t 						wd_account_board_read_boards(wd_account_t *);
wi_boolean_t 						wd_account_board_add_boards(wd_account_t *);
wi_boolean_t 						wd_account_board_move_boards(wd_account_t *);
wi_boolean_t 						wd_account_board_rename_boards(wd_account_t *);
wi_boolean_t 						wd_account_board_delete_boards(wd_account_t *);
wi_boolean_t 						wd_account_board_get_board_info(wd_account_t *);
wi_boolean_t 						wd_account_board_set_board_info(wd_account_t *);
wi_boolean_t 						wd_account_board_add_threads(wd_account_t *);
wi_boolean_t 						wd_account_board_move_threads(wd_account_t *);
wi_boolean_t 						wd_account_board_add_posts(wd_account_t *);
wi_boolean_t 						wd_account_board_edit_own_threads_and_posts(wd_account_t *);
wi_boolean_t 						wd_account_board_edit_all_threads_and_posts(wd_account_t *);
wi_boolean_t 						wd_account_board_delete_own_threads_and_posts(wd_account_t *);
wi_boolean_t 						wd_account_board_delete_all_threads_and_posts(wd_account_t *);
wi_boolean_t 						wd_account_file_list_files(wd_account_t *);
wi_boolean_t 						wd_account_file_search_files(wd_account_t *);
wi_boolean_t 						wd_account_file_get_info(wd_account_t *);
wi_boolean_t 						wd_account_file_create_directories(wd_account_t *);
wi_boolean_t 						wd_account_file_create_links(wd_account_t *);
wi_boolean_t 						wd_account_file_move_files(wd_account_t *);
wi_boolean_t 						wd_account_file_rename_files(wd_account_t *);
wi_boolean_t 						wd_account_file_set_type(wd_account_t *);
wi_boolean_t 						wd_account_file_set_comment(wd_account_t *);
wi_boolean_t 						wd_account_file_set_permissions(wd_account_t *);
wi_boolean_t 						wd_account_file_set_executable(wd_account_t *);
wi_boolean_t 						wd_account_file_set_label(wd_account_t *);
wi_boolean_t 						wd_account_file_delete_files(wd_account_t *);
wi_boolean_t 						wd_account_file_access_all_dropboxes(wd_account_t *);
wi_uinteger_t 						wd_account_file_recursive_list_depth_limit(wd_account_t *);
wi_boolean_t 						wd_account_transfer_download_files(wd_account_t *);
wi_boolean_t 						wd_account_transfer_upload_files(wd_account_t *);
wi_boolean_t 						wd_account_transfer_upload_directories(wd_account_t *);
wi_boolean_t 						wd_account_transfer_upload_anywhere(wd_account_t *);
wi_uinteger_t 						wd_account_transfer_download_limit(wd_account_t *);
wi_uinteger_t 						wd_account_transfer_upload_limit(wd_account_t *);
wi_uinteger_t 						wd_account_transfer_download_speed_limit(wd_account_t *);
wi_uinteger_t 						wd_account_transfer_upload_speed_limit(wd_account_t *);
wi_boolean_t 						wd_account_account_change_password(wd_account_t *);
wi_boolean_t 						wd_account_account_list_accounts(wd_account_t *);
wi_boolean_t 						wd_account_account_read_accounts(wd_account_t *);
wi_boolean_t 						wd_account_account_create_users(wd_account_t *);
wi_boolean_t 						wd_account_account_edit_users(wd_account_t *);
wi_boolean_t 						wd_account_account_delete_users(wd_account_t *);
wi_boolean_t 						wd_account_account_create_groups(wd_account_t *);
wi_boolean_t 						wd_account_account_edit_groups(wd_account_t *);
wi_boolean_t 						wd_account_account_delete_groups(wd_account_t *);
wi_boolean_t 						wd_account_account_raise_account_privileges(wd_account_t *);
wi_boolean_t 						wd_account_log_view_log(wd_account_t *);
wi_boolean_t 						wd_account_events_view_events(wd_account_t *);
wi_boolean_t 						wd_account_settings_get_settings(wd_account_t *);
wi_boolean_t 						wd_account_settings_set_settings(wd_account_t *);
wi_boolean_t 						wd_account_banlist_get_bans(wd_account_t *);
wi_boolean_t 						wd_account_banlist_add_bans(wd_account_t *);
wi_boolean_t 						wd_account_banlist_delete_bans(wd_account_t *);
wi_boolean_t 						wd_account_tracker_list_servers(wd_account_t *);
wi_boolean_t 						wd_account_tracker_register_servers(wd_account_t *);

#endif /* WD_ACCOUNTS_H */
