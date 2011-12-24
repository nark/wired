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

#ifndef WD_SERVER_H
#define WD_SERVER_H 1

#include <wired/wired.h>

#include "chats.h"

#define WD_DNSSD_NAME				"_wired._tcp"
#define WD_SERVER_PORT				4871


void								wd_server_initialize(void);
void								wd_server_schedule(void);
void								wd_server_listen(void);
void								wd_server_apply_settings(wi_set_t *);
void								wd_server_cleanup(void);

wi_p7_message_t *					wd_client_info_message(void);
wi_p7_message_t *					wd_server_info_message(void);

void								wd_server_log_reply_log(wd_user_t *, wi_p7_message_t *);

wi_p7_message_t *					wd_user_read_message(wd_user_t *, wi_time_interval_t);
wi_boolean_t						wd_user_write_message(wd_user_t *, wi_time_interval_t, wi_p7_message_t *);

void								wd_user_send_message(wd_user_t *, wi_p7_message_t *);
void								wd_user_reply_message(wd_user_t *, wi_p7_message_t *, wi_p7_message_t *);
void								wd_user_reply_okay(wd_user_t *, wi_p7_message_t *);
void								wd_user_reply_error(wd_user_t *, wi_string_t *, wi_p7_message_t *);
void								wd_user_reply_file_errno(wd_user_t *, wi_p7_message_t *);
void								wd_user_reply_internal_error(wd_user_t *, wi_string_t *, wi_p7_message_t *);
void								wd_broadcast_message(wi_p7_message_t *);
void								wd_chat_broadcast_message(wd_chat_t *, wi_p7_message_t *);

extern wi_uinteger_t				wd_port;
extern wi_data_t					*wd_banner;
extern wi_p7_spec_t					*wd_p7_spec;

#endif /* WD_SERVER_H */
