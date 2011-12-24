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

#ifndef WD_CHATS_H
#define WD_CHATS_H 1

#include <wired/wired.h>

#include "users.h"

typedef uint32_t						wd_cid_t;
typedef struct _wd_topic				wd_topic_t;


void									wd_chats_initialize(void);

void									wd_chats_add_chat(wd_chat_t *);
wd_chat_t *								wd_chats_chat_with_id(wd_cid_t);
wi_array_t *							wd_chats_chats_with_user(wd_user_t *);
void									wd_chats_remove_user(wd_user_t *);

wd_chat_t *								wd_chat_private_chat(void);

wi_boolean_t							wd_chat_contains_user(wd_chat_t *, wd_user_t *);
void									wd_chat_add_user_and_broadcast(wd_chat_t *, wd_user_t *);
void									wd_chat_remove_user(wd_chat_t *, wd_user_t *);

void									wd_chat_reply_user_list(wd_chat_t *, wd_user_t *, wi_p7_message_t *);
void									wd_chat_reply_topic(wd_chat_t *);
void									wd_chat_broadcast_topic(wd_chat_t *);
void									wd_chat_broadcast_user_leave(wd_chat_t *, wd_user_t *);
wi_p7_message_t *						wd_chat_topic_message(wd_chat_t *);

void									wd_chat_set_topic(wd_chat_t *, wd_topic_t *);
wd_topic_t *							wd_chat_topic(wd_chat_t *);

wd_cid_t								wd_chat_id(wd_chat_t *);
wi_array_t *							wd_chat_users(wd_chat_t *);

void									wd_chat_add_invitation_for_user(wd_chat_t *, wd_user_t *);
wi_boolean_t							wd_chat_is_user_invited(wd_chat_t *, wd_user_t *);
void									wd_chat_remove_invitation_for_user(wd_chat_t *, wd_user_t *);


wd_topic_t *							wd_topic_with_string(wi_string_t *, wi_date_t *, wi_string_t *, wi_string_t *, wi_string_t *);


extern wd_chat_t						*wd_public_chat;

#endif /* WD_CHATS_H */
