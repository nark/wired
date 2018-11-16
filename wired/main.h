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

#ifndef WD_MAIN_H
#define WD_MAIN_H 1

#include <wired/wired.h>

typedef struct _wd_user				wd_user_t;
typedef struct _wd_chat				wd_chat_t;


void								wd_write_status(wi_boolean_t);

void								wd_database_set_version_for_table(wi_uinteger_t, wi_string_t *);
wi_uinteger_t						wd_database_version_for_table(wi_string_t *);


extern wi_boolean_t					wd_running;

extern wi_address_family_t			wd_address_family;
extern wi_boolean_t					wd_startup;

extern wi_string_t					*wd_config_path;

extern wi_sqlite3_database_t		*wd_database;

extern wi_lock_t					*wd_status_lock;
extern wi_date_t					*wd_start_date;
extern wi_uinteger_t				wd_current_users, wd_total_users;
extern wi_uinteger_t				wd_current_downloads, wd_total_downloads;
extern wi_uinteger_t				wd_current_uploads, wd_total_uploads;
extern wi_file_offset_t				wd_downloads_traffic, wd_uploads_traffic;
extern wi_uinteger_t				wd_tracker_current_servers;
extern wi_uinteger_t				wd_tracker_current_users;
extern wi_file_offset_t				wd_tracker_current_files;
extern wi_file_offset_t				wd_tracker_current_size;

#endif /* WD_MAIN_H */
