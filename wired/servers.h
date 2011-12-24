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

#ifndef WD_SERVERS_H
#define WD_SERVERS_H 1

#include <wired/wired.h>

typedef struct _wd_server			wd_server_t;


void								wd_servers_initialize(void);
void								wd_servers_schedule(void);

wd_server_t *						wd_servers_server_for_ip(wi_string_t *);

void								wd_servers_register_server(wd_user_t *, wi_p7_message_t *);
wi_boolean_t						wd_servers_update_server(wi_string_t *, wd_user_t *, wi_p7_message_t *);
void								wd_servers_reply_categories(wd_user_t *, wi_p7_message_t *);
void								wd_servers_reply_server_list(wd_user_t *, wi_p7_message_t *);

wi_boolean_t						wd_server_is_active(wd_server_t *);
wi_cipher_t *						wd_server_cipher(wd_server_t *);
wi_uinteger_t						wd_server_port(wd_server_t *);

#endif /* WD_SERVERS_H */
