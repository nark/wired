/* $Id$ */

/*
 *  Copyright (c) 2008-2009 Axel Andersson
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

#ifndef WD_BOARD_H
#define WD_BOARD_H 1

#include "users.h"

void									wd_boards_initialize(void);

void									wd_boards_renamed_user(wi_string_t *, wi_string_t *);
void									wd_boards_renamed_group(wi_string_t *, wi_string_t *);
void									wd_boards_reload_account(wd_user_t *, wd_account_t *, wd_account_t *);

void									wd_boards_reply_boards(wd_user_t *, wi_p7_message_t *);
void									wd_boards_reply_threads(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_reply_thread(wi_uuid_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_boards_has_board_with_name(wi_string_t *);
wi_string_t *							wd_boards_subject_for_thread(wi_uuid_t *);
wi_string_t *							wd_boards_board_for_thread(wi_uuid_t *);
wi_string_t *							wd_boards_subject_for_post(wi_uuid_t *);
wi_string_t *							wd_boards_board_for_post(wi_uuid_t *);

wi_boolean_t							wd_boards_add_board(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_rename_board(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_move_board(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_delete_board(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_get_board_info(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_set_board_info(wi_string_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_boards_add_thread(wi_string_t *, wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_edit_thread(wi_uuid_t *, wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_move_thread(wi_uuid_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_delete_thread(wi_uuid_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_boards_add_post(wi_uuid_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_edit_post(wi_uuid_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_delete_post(wi_uuid_t *, wd_user_t *, wi_p7_message_t *);

#endif /* WD_BOARD_H */
