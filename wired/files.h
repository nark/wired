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

#ifndef WD_FILES_H
#define WD_FILES_H 1

#include <wired/wired.h>

#include "users.h"

#define WD_FILES_META_PATH				".wired"


enum _wd_file_type {
	WD_FILE_TYPE_FILE					= 0,
	WD_FILE_TYPE_DIR,
	WD_FILE_TYPE_UPLOADS,
	WD_FILE_TYPE_DROPBOX
};
typedef enum _wd_file_type				wd_file_type_t;

enum _wd_file_label {
	WD_FILE_LABEL_NONE					= WI_FS_FINDER_LABEL_NONE,
	WD_FILE_LABEL_RED					= WI_FS_FINDER_LABEL_RED,
	WD_FILE_LABEL_ORANGE				= WI_FS_FINDER_LABEL_ORANGE,
	WD_FILE_LABEL_YELLOW				= WI_FS_FINDER_LABEL_YELLOW,
	WD_FILE_LABEL_GREEN					= WI_FS_FINDER_LABEL_GREEN,
	WD_FILE_LABEL_BLUE					= WI_FS_FINDER_LABEL_BLUE,
	WD_FILE_LABEL_PURPLE				= WI_FS_FINDER_LABEL_PURPLE,
	WD_FILE_LABEL_GRAY					= WI_FS_FINDER_LABEL_GRAY,
};
typedef enum _wd_file_label				wd_file_label_t;

enum _wd_file_permissions {
	WD_FILE_OWNER_WRITE					= (2 << 6),
	WD_FILE_OWNER_READ					= (4 << 6),
	WD_FILE_GROUP_WRITE					= (2 << 3),
	WD_FILE_GROUP_READ					= (4 << 3),
	WD_FILE_EVERYONE_WRITE				= (2 << 0),
	WD_FILE_EVERYONE_READ				= (4 << 0)
};
typedef enum _wd_file_permissions		wd_file_permissions_t;

typedef struct _wd_files_privileges		wd_files_privileges_t;


void									wd_files_initialize(void);
void									wd_files_apply_settings(wi_set_t *);
void									wd_files_schedule(void);

wi_boolean_t							wd_files_reply_list(wi_string_t *, wi_boolean_t, wd_user_t *, wi_p7_message_t *);
wi_file_offset_t						wd_files_count_path(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_reply_info(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_reply_preview(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_create_path(wi_string_t *, wd_file_type_t, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_delete_path(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_move_path(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_link_path(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_files_set_type(wi_string_t *, wd_file_type_t, wd_user_t *, wi_p7_message_t *);
wd_file_type_t							wd_files_type(wi_string_t *);
wd_file_type_t							wd_files_type_with_stat(wi_string_t *, wi_fs_stat_t *);

wi_boolean_t							wd_files_set_executable(wi_string_t *, wi_boolean_t, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_files_set_comment(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
void									wd_files_move_comment(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_remove_comment(wi_string_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_files_set_label(wi_string_t *, wd_file_label_t, wd_user_t *, wi_p7_message_t *);
wd_file_label_t							wd_files_label(wi_string_t *path);
void									wd_files_move_label(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_remove_label(wi_string_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_files_set_privileges(wi_string_t *, wd_files_privileges_t *, wd_user_t *, wi_p7_message_t *);
wd_files_privileges_t *					wd_files_privileges(wi_string_t *, wd_user_t *);
wd_files_privileges_t *					wd_files_drop_box_privileges(wi_string_t *);

wi_boolean_t							wd_files_path_is_valid(wi_string_t *);
wi_string_t *							wd_files_virtual_path(wi_string_t *, wd_user_t *);
wi_string_t *							wd_files_real_path(wi_string_t *, wd_user_t *);
wi_boolean_t							wd_files_has_uploads_or_drop_box_in_path(wi_string_t *, wd_user_t *, wd_files_privileges_t **);

wi_string_t *							wd_files_string_for_bytes(wi_file_offset_t);

wd_files_privileges_t *					wd_files_privileges_with_message(wi_p7_message_t *);

wi_boolean_t							wd_files_privileges_is_readable_by_account(wd_files_privileges_t *, wd_account_t *);
wi_boolean_t							wd_files_privileges_is_writable_by_account(wd_files_privileges_t *, wd_account_t *);
wi_boolean_t							wd_files_privileges_is_readable_and_writable_by_account(wd_files_privileges_t *, wd_account_t *);

extern wi_string_t						*wd_files;
extern wi_uinteger_t					wd_files_root_volume;
extern wi_fsevents_t					*wd_files_fsevents;

#endif /* WD_FILES_H */
