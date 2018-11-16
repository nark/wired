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

#include "config.h"

#include <wired/wired.h>

#include "accounts.h"
#include "boards.h"
#include "server.h"
#include "settings.h"

struct _wd_board_privileges {
	wi_runtime_base_t							base;
	
	wi_string_t									*owner;
	wi_string_t									*group;
	wi_uinteger_t								mode;
};

enum _wd_board_permissions {
	WD_BOARD_OWNER_WRITE						= (2 << 6),
	WD_BOARD_OWNER_READ							= (4 << 6),
	WD_BOARD_GROUP_WRITE						= (2 << 3),
	WD_BOARD_GROUP_READ							= (4 << 3),
	WD_BOARD_EVERYONE_WRITE						= (2 << 0),
	WD_BOARD_EVERYONE_READ						= (4 << 0)
};
typedef enum _wd_board_permissions				wd_board_permissions_t;

typedef struct _wd_board_privileges				wd_board_privileges_t;


static void										wd_boards_create_tables(void);
static void										wd_boards_convert_boards(void);
static wi_boolean_t								wd_boards_convert_boards_to_database(wi_string_t *);
static wi_boolean_t								wd_boards_convert_thread_to_database(wi_string_t *, wi_string_t *);
static wi_boolean_t								wd_boards_convert_news_to_database(wi_string_t *, wi_string_t *);
static wd_board_privileges_t *					wd_boards_privileges_for_path(wi_string_t *);

static void										wd_boards_changed_thread(wi_uuid_t *, wd_board_privileges_t *);
static wd_board_privileges_t *					wd_boards_privileges_for_board(wi_string_t *);
static wd_board_privileges_t *					wd_boards_privileges_for_thread(wi_uuid_t *);
static wd_board_privileges_t *					wd_boards_privileges_for_post(wi_uuid_t *);

static wd_board_privileges_t *					wd_board_privileges_alloc(void);
static void										wd_board_privileges_dealloc(wi_runtime_instance_t *);

static wd_board_privileges_t *					wd_board_privileges_with_message(wi_p7_message_t *);
static wd_board_privileges_t *					wd_board_privileges_with_owner(wi_string_t *, wi_string_t *, wi_uinteger_t);
static wd_board_privileges_t *					wd_board_privileges_with_sqlite3_results(wi_dictionary_t *);

static wi_boolean_t								wd_board_privileges_is_readable_by_account(wd_board_privileges_t *, wd_account_t *);
static wi_boolean_t								wd_board_privileges_is_writable_by_account(wd_board_privileges_t *, wd_account_t *);


static wi_runtime_id_t							wd_board_privileges_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t						wd_board_privileges_runtime_class = {
	"wd_board_privileges_t",
	wd_board_privileges_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};



void wd_boards_initialize(void) {
	wi_dictionary_t		*results;
	
	wd_board_privileges_runtime_id = wi_runtime_register_class(&wd_board_privileges_runtime_class);
	
	wd_boards_create_tables();
	wd_boards_convert_boards();
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT COUNT(*) AS count FROM boards"), NULL);
	
	if(!results)
		wi_log_fatal(WI_STR("Could not execute database statement: %m"));
	
	if(wi_number_integer(wi_dictionary_data_for_key(results, WI_STR("count"))) == 0) {
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO boards (board, owner, `group`, mode) VALUES (?, ?, ?, ?)"),
										 WI_STR("General"),
										 WI_STR("admin"),
										 WI_STR(""),
										 WI_INT32(WD_BOARD_OWNER_READ    | WD_BOARD_OWNER_WRITE |
												  WD_BOARD_GROUP_READ    | WD_BOARD_GROUP_WRITE |
												  WD_BOARD_EVERYONE_READ | WD_BOARD_EVERYONE_WRITE),
										 NULL)) {
			wi_log_fatal(WI_STR("Could not execute database statement: %m"));
		}
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO threads "
															 "(thread, board, subject, `text`, post_date, nick, login, ip, icon) "
															 "VALUES "
															 "(?, ?, ?, ?, ?, ?, ?, ?, ?)"),
										 wi_uuid_string(wi_uuid()),
										 WI_STR("General"),
										 WI_STR("Welcome to Wired"),
										 WI_STR("Welcome to your Wired server. To learn more about administrating your server, please have a look to the [url=http://www.read-write.fr/wired/wiki/]wiki[/url]."),
										 wi_date_sqlite3_string(wi_date()),
										 WI_STR("nark"),
										 WI_STR("admin"),
										 WI_STR("127.0.0.1"),
										 WI_STR(""),
										 NULL)) {
			wi_log_fatal(WI_STR("Could not execute database statement: %m"));
		}
	}
}



#pragma mark -

void wd_boards_renamed_user(wi_string_t *olduser, wi_string_t *newuser) {
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET owner = ? WHERE owner = ?"),
									 newuser ? newuser : wi_null(),
									 olduser,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
	
	if(newuser) {
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET login = ? WHERE login = ?"),
										 newuser,
										 olduser,
										 NULL)) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		}
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE posts SET login = ? WHERE login = ?"),
										 newuser,
										 olduser,
										 NULL)) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		}
	}
}



void wd_boards_renamed_group(wi_string_t *oldgroup, wi_string_t *newgroup) {
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET `group` = ? WHERE `group` = ?"),
									 newgroup ? newgroup : wi_null(),
									 oldgroup,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
}



void wd_boards_reload_account(wd_user_t *user, wd_account_t *oldaccount, wd_account_t *newaccount) {
	wi_sqlite3_statement_t		*statement;
	wi_p7_message_t				*broadcast;
	wi_dictionary_t				*results;
	wi_runtime_instance_t		*board;
	wd_board_privileges_t		*privileges;
	wi_boolean_t				oldreadable, newreadable;
	wi_boolean_t				oldwritable, newwritable;
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT board, owner, `group`, mode FROM boards"), NULL);

	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		board			= wi_dictionary_data_for_key(results, WI_STR("board"));
		privileges		= wd_board_privileges_with_sqlite3_results(results);
		oldreadable		= wd_board_privileges_is_readable_by_account(privileges, oldaccount);
		newreadable		= wd_board_privileges_is_readable_by_account(privileges, newaccount);
		oldwritable		= wd_board_privileges_is_writable_by_account(privileges, oldaccount);
		newwritable		= wd_board_privileges_is_writable_by_account(privileges, newaccount);
		
		if(!(oldreadable || oldwritable) && (newreadable || newwritable)) {
			broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_added"), wd_p7_spec);
			wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
			wi_p7_message_set_bool_for_name(broadcast, newreadable, WI_STR("wired.board.readable"));
			wi_p7_message_set_bool_for_name(broadcast, newwritable, WI_STR("wired.board.writable"));
			wd_user_send_message(user, broadcast);
		}
		else if((oldreadable || oldwritable) && !(newreadable || newwritable)) {
			broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_deleted"), wd_p7_spec);
			wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
			wd_user_send_message(user, broadcast);
		}
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return;
	}
}



#pragma mark -

void wd_boards_reply_boards(wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_p7_message_t				*reply;
	wd_board_privileges_t		*privileges;
	wi_boolean_t				readable, writable;

	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT board, owner, `group`, mode FROM boards ORDER BY board"), NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		privileges		= wd_board_privileges_with_sqlite3_results(results);
		readable		= wd_board_privileges_is_readable_by_account(privileges, wd_user_account(user));
		writable		= wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user));
		
		if(readable || writable) {
			reply = wi_p7_message_with_name(WI_STR("wired.board.board_list"), wd_p7_spec);
			wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("board")), WI_STR("wired.board.board"));
			wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.board.readable"));
			wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.board.writable"));
			wd_user_reply_message(user, reply, message);
		}
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}

	reply = wi_p7_message_with_name(WI_STR("wired.board.board_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



void wd_boards_reply_threads(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_p7_message_t				*reply;
	wi_string_t					*query;
	wi_runtime_instance_t		*thread, *login, *postdate, *editdate, *latestreply, *latestreplydate;
	wd_board_privileges_t		*privileges;
	
	if(board) {
		query = WI_STR("SELECT thread, threads.board, subject, post_date, edit_date, nick, login, "
					   "owner, `group`, mode, "
					   "(SELECT COUNT(*) FROM posts WHERE posts.thread = threads.thread) AS replies, "
					   "(SELECT post FROM posts WHERE posts.thread = threads.thread ORDER BY post_date DESC LIMIT 1) AS latest_reply, "
					   "(SELECT post_date FROM posts WHERE posts.thread = threads.thread ORDER BY post_date DESC LIMIT 1) AS latest_reply_date "
					   "FROM threads "
					   "LEFT JOIN boards ON threads.board = boards.board "
					   "WHERE boards.board = ?");
	} else {
		query = WI_STR("SELECT thread, threads.board, subject, post_date, edit_date, nick, login, "
					   "owner, `group`, mode, "
					   "(SELECT COUNT(*) FROM posts WHERE posts.thread = threads.thread) AS replies, "
					   "(SELECT post FROM posts WHERE posts.thread = threads.thread ORDER BY post_date DESC LIMIT 1) AS latest_reply, "
					   "(SELECT post_date FROM posts WHERE posts.thread = threads.thread ORDER BY post_date DESC LIMIT 1) AS latest_reply_date "
					   "FROM threads "
					   "LEFT JOIN boards ON threads.board = boards.board");
	}
	
	statement = wi_sqlite3_prepare_statement(wd_database, query, board, NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		privileges = wd_board_privileges_with_sqlite3_results(results);
		
		if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(user))) {
			thread				= wi_dictionary_data_for_key(results, WI_STR("thread"));
			postdate			= wi_dictionary_data_for_key(results, WI_STR("post_date"));
			editdate			= wi_dictionary_data_for_key(results, WI_STR("edit_date"));
			login				= wi_dictionary_data_for_key(results, WI_STR("login"));
			latestreply			= wi_dictionary_data_for_key(results, WI_STR("latest_reply"));
			latestreplydate		= wi_dictionary_data_for_key(results, WI_STR("latest_reply_date"));
		
			reply = wi_p7_message_with_name(WI_STR("wired.board.thread_list"), wd_p7_spec);
			wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("board")), WI_STR("wired.board.board"));
			wi_p7_message_set_uuid_for_name(reply, wi_uuid_with_string(thread), WI_STR("wired.board.thread"));
			wi_p7_message_set_date_for_name(reply, wi_date_with_sqlite3_string(postdate), WI_STR("wired.board.post_date"));
			
			if(editdate != wi_null())
				wi_p7_message_set_date_for_name(reply, wi_date_with_sqlite3_string(editdate), WI_STR("wired.board.edit_date"));

			wi_p7_message_set_bool_for_name(reply, wi_is_equal(login, wd_user_login(user)), WI_STR("wired.board.own_thread"));
			wi_p7_message_set_number_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("replies")), WI_STR("wired.board.replies"));
			
			if(latestreply != wi_null())
				wi_p7_message_set_uuid_for_name(reply, wi_uuid_with_string(latestreply), WI_STR("wired.board.latest_reply"));
			
			if(latestreplydate != wi_null())
				wi_p7_message_set_date_for_name(reply, wi_date_with_sqlite3_string(latestreplydate), WI_STR("wired.board.latest_reply_date"));
			
			wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("subject")), WI_STR("wired.board.subject"));
			wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("nick")), WI_STR("wired.user.nick"));

			wd_user_reply_message(user, reply, message);
		}
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}

	reply = wi_p7_message_with_name(WI_STR("wired.board.thread_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



wi_boolean_t wd_boards_reply_thread(wi_uuid_t *thread, wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_p7_message_t				*reply;
	wi_runtime_instance_t		*post, *postdate, *editdate, *login;
	wd_board_privileges_t		*privileges;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT thread, threads.board, `text`, icon, "
															   "owner, `group`, mode "
															   "FROM threads "
															   "LEFT JOIN boards ON threads.board = boards.board "
															   "WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.thread_not_found"), message);
		
		return false;
	}
	
	privileges = wd_board_privileges_with_sqlite3_results(results);
	
	if(!wd_board_privileges_is_readable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.board.thread"), wd_p7_spec);
	wi_p7_message_set_uuid_for_name(reply, thread, WI_STR("wired.board.thread"));
	wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("text")), WI_STR("wired.board.text"));
	wi_p7_message_set_data_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("icon")), WI_STR("wired.user.icon"));
	wd_user_reply_message(user, reply, message);
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT post, thread, `text`, post_date, edit_date, text, nick, login, icon "
																 "FROM posts "
																 "WHERE thread = ?"),
											 wi_uuid_string(thread),
											 NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		post			= wi_dictionary_data_for_key(results, WI_STR("post"));
		postdate		= wi_dictionary_data_for_key(results, WI_STR("post_date"));
		editdate		= wi_dictionary_data_for_key(results, WI_STR("edit_date"));
		login			= wi_dictionary_data_for_key(results, WI_STR("login"));
		
		reply = wi_p7_message_with_name(WI_STR("wired.board.post_list"), wd_p7_spec);
		wi_p7_message_set_uuid_for_name(reply, thread, WI_STR("wired.board.thread"));
		wi_p7_message_set_uuid_for_name(reply, wi_uuid_with_string(post), WI_STR("wired.board.post"));
		wi_p7_message_set_date_for_name(reply, wi_date_with_sqlite3_string(postdate), WI_STR("wired.board.post_date"));
		
		if(editdate != wi_null())
			wi_p7_message_set_date_for_name(reply, wi_date_with_sqlite3_string(editdate), WI_STR("wired.board.edit_date"));
		
		wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("text")), WI_STR("wired.board.text"));
		wi_p7_message_set_bool_for_name(reply, wi_is_equal(login, wd_user_login(user)), WI_STR("wired.board.own_post"));
		wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("nick")), WI_STR("wired.user.nick"));
		wi_p7_message_set_data_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("icon")), WI_STR("wired.user.icon"));
		
		wd_user_reply_message(user, reply, message);
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.board.post_list.done"), wd_p7_spec);
	wi_p7_message_set_uuid_for_name(reply, thread, WI_STR("wired.board.thread"));
	wd_user_reply_message(user, reply, message);
	
	return true;
}



#pragma mark -

wi_boolean_t wd_boards_add_board(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_p7_message_t			*broadcast;
	wd_user_t				*peer;
	wd_board_privileges_t	*privileges;
	wi_boolean_t			readable, writable;
	
	privileges = wd_board_privileges_with_message(message);
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO boards (board, owner, `group`, mode) VALUES (?, ?, ?, ?)"),
									 board,
									 privileges->owner,
									 privileges->group,
									 WI_INT32(privileges->mode),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
    // broadcast board added to clients
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			readable = wd_board_privileges_is_readable_by_account(privileges, wd_user_account(peer));
			writable = wd_board_privileges_is_writable_by_account(privileges, wd_user_account(peer));

			if(readable || writable) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_added"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_bool_for_name(broadcast, readable, WI_STR("wired.board.readable"));
				wi_p7_message_set_bool_for_name(broadcast, writable, WI_STR("wired.board.writable"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
    
    // broadcast board added to tracker if it's a shared board
	
	return true;
}



wi_boolean_t wd_boards_rename_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t             *enumerator;
    wi_sqlite3_statement_t      *statement;
    wi_dictionary_t             *results;
	wi_p7_message_t             *broadcast;
	wd_user_t                   *peer;
	wd_board_privileges_t       *privileges;
    wi_string_t                 *old_subboard, *new_subboard;
    wi_array_t                  *old_subboards, *new_subboards;
    unsigned int                 i;
    
    old_subboards = wi_mutable_array();
	new_subboards = wi_mutable_array();
    
    // update the parent board
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET board = ? WHERE board = ?"),
									 newboard,
									 oldboard,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
    
    // retrieves matching board and subboards
    statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT board FROM boards WHERE board LIKE ? ORDER BY board"), 
                                             wi_string_with_format(WI_STR("%@%%"), oldboard), 
                                             NULL);
    
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
        
		return false;
	}
    	
    // temporary store subboards to avoid modifying the database during the satement
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && results && wi_dictionary_count(results) > 0) {

        old_subboard = wi_dictionary_data_for_key(results, WI_STR("board"));
        
        // exclude parent board
        if(!wi_is_equal(old_subboard, newboard)) {
            wi_range_t range = wi_string_range_of_string(old_subboard, oldboard, 0);
            
            // prepare renaming
            if(range.location != WI_NOT_FOUND) {
                new_subboard = wi_string_by_replacing_characters_in_range_with_string(old_subboard, range, newboard);
            
                wi_mutable_array_add_data(old_subboards, old_subboard);
                wi_mutable_array_add_data(new_subboards, new_subboard);
            }
        }
    }
    
    // update subboards
    for(i = 0; i < wi_array_count(old_subboards); i++) {
        
        old_subboard = wi_array_data_at_index(old_subboards, i);
        new_subboard = wi_array_data_at_index(new_subboards, i);
        
        if(!wi_is_equal(old_subboard, new_subboard)) {
            // update boards path
            if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET board = ? WHERE board = ?"),
                                             new_subboard,
                                             old_subboard,
                                             NULL)) {
                
                wi_log_error(WI_STR("Could not execute database statement: %m"));
                wd_user_reply_internal_error(user, wi_error_string(), message);
                
                return false;
            }
            
            // update related threads in sub-board
            if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET board = ? WHERE board = ?"),
                                             new_subboard,
                                             old_subboard,
                                             NULL)) {
                wi_log_error(WI_STR("Could not execute database statement: %m"));
                wd_user_reply_internal_error(user, wi_error_string(), message);
                
                return false;
            }
        }
    }
        
    // update related threads of the parent board
    if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET board = ? WHERE board = ?"),
									 newboard,
									 oldboard,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	privileges = wd_boards_privileges_for_board(newboard);
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(peer)) ||
			   wd_board_privileges_is_writable_by_account(privileges, wd_user_account(peer))) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_renamed"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
				wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);

	return true;
}



wi_boolean_t wd_boards_move_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t             *enumerator;
    wi_sqlite3_statement_t      *statement;
    wi_dictionary_t             *results;
	wi_p7_message_t             *broadcast;
	wd_user_t                   *peer;
	wd_board_privileges_t       *privileges;
    wi_string_t                 *old_subboard, *new_subboard;
    
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET board = ? WHERE board = ?"),
									 newboard,
									 oldboard,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
    
    // retrieves subboards
    statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT board FROM boards WHERE board LIKE ? ORDER BY board"), 
                                             wi_string_with_format(WI_STR("%@%%"), oldboard), 
                                             NULL);
    
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
        
		return false;
	}
	
    // update suboards
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
        
        old_subboard = wi_dictionary_data_for_key(results, WI_STR("board"));
        wi_range_t range = wi_string_range_of_string(old_subboard, oldboard, 0);
        
        if(range.location != WI_NOT_FOUND) {
            new_subboard = wi_string_by_replacing_characters_in_range_with_string(old_subboard, range, newboard);
            
            if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET board = ? WHERE board = ?"),
                                             new_subboard,
                                             old_subboard,
                                             NULL)) {
                wi_log_error(WI_STR("Could not execute database statement: %m"));
                wd_user_reply_internal_error(user, wi_error_string(), message);
                
                return false;
            }
            
            if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET board = ? WHERE board = ?"),
                                             new_subboard,
                                             old_subboard,
                                             NULL)) {
                wi_log_error(WI_STR("Could not execute database statement: %m"));
                wd_user_reply_internal_error(user, wi_error_string(), message);
                
                return false;
            }
        }
    }
    
    if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET board = ? WHERE board = ?"),
									 newboard,
									 oldboard,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
    // braodcast board moved to users
	privileges = wd_boards_privileges_for_board(newboard);
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(peer)) ||
			   wd_board_privileges_is_writable_by_account(privileges, wd_user_account(peer))) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_moved"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
				wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
	
	return true;
}



wi_boolean_t wd_boards_delete_board(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_p7_message_t			*broadcast;
	wd_user_t				*peer;
	wd_board_privileges_t	*privileges;
	
	privileges = wd_boards_privileges_for_board(board);
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM boards WHERE board = ?"),
									 board,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(peer)) ||
			   wd_board_privileges_is_writable_by_account(privileges, wd_user_account(peer))) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_deleted"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
	
	return true;
}



wi_boolean_t wd_boards_get_board_info(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wd_board_privileges_t	*privileges;
	    
	privileges = wd_boards_privileges_for_board(board);
	
	reply = wi_p7_message_with_name(WI_STR("wired.board.board_info"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, board, WI_STR("wired.board.board"));
	wi_p7_message_set_string_for_name(reply, privileges->owner, WI_STR("wired.board.owner"));
	wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
	wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
	wi_p7_message_set_string_for_name(reply, privileges->group, WI_STR("wired.board.group"));
	wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
	wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
	wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
	wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
	wd_user_reply_message(user, reply, message);
	
	return true;
}



wi_boolean_t wd_boards_set_board_info(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_p7_message_t			*broadcast;
	wd_user_t				*peer;
	wd_board_privileges_t	*oldprivileges, *newprivileges;
	wi_boolean_t			oldreadable, newreadable;
	wi_boolean_t			oldwritable, newwritable;
	
	oldprivileges = wd_boards_privileges_for_board(board);
	newprivileges = wd_board_privileges_with_message(message);
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE boards SET owner = ?, `group` = ?, mode = ? WHERE board = ?"),
									 newprivileges->owner,
									 newprivileges->group,
									 WI_INT32(newprivileges->mode),
									 board,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			oldreadable = wd_board_privileges_is_readable_by_account(oldprivileges, wd_user_account(peer));
			newreadable = wd_board_privileges_is_readable_by_account(newprivileges, wd_user_account(peer));
			oldwritable = wd_board_privileges_is_writable_by_account(oldprivileges, wd_user_account(peer));
			newwritable = wd_board_privileges_is_writable_by_account(newprivileges, wd_user_account(peer));
			
			if((oldreadable || oldwritable) && (!newreadable && !newwritable)) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_deleted"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wd_user_send_message(peer, broadcast);
			}
			else if((!oldreadable && !oldwritable) && (newreadable || newwritable)) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_added"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_bool_for_name(broadcast, newreadable, WI_STR("wired.board.readable"));
				wi_p7_message_set_bool_for_name(broadcast, newwritable, WI_STR("wired.board.writable"));
				wd_user_send_message(peer, broadcast);
			}
			else if((oldreadable || oldwritable) && (newreadable || newwritable)) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_info_changed"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_bool_for_name(broadcast, newreadable, WI_STR("wired.board.readable"));
				wi_p7_message_set_bool_for_name(broadcast, newwritable, WI_STR("wired.board.writable"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
	
	return true;
}



#pragma mark -

wi_boolean_t wd_boards_has_board_with_name(wi_string_t *board) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT board FROM boards WHERE board = ?"),
										   board,
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}
	
	return (wi_dictionary_count(results) > 0);
}



wi_string_t * wd_boards_board_for_thread(wi_uuid_t *thread) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT board FROM threads WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return NULL;
	}
	
	return wi_dictionary_data_for_key(results, WI_STR("board"));
}



wi_string_t * wd_boards_subject_for_thread(wi_uuid_t *thread) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT subject FROM threads WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return NULL;
	}
	
	return wi_dictionary_data_for_key(results, WI_STR("subject"));
}



wi_string_t * wd_boards_board_for_post(wi_uuid_t *post) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT boards.board FROM posts "
															   "LEFT JOIN threads ON posts.thread = threads.thread "
															   "LEFT JOIN boards ON threads.board = boards.board "
															   "WHERE post = ?"),
										   wi_uuid_string(post),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return NULL;
	}
	
	return wi_dictionary_data_for_key(results, WI_STR("board"));
}



wi_string_t * wd_boards_subject_for_post(wi_uuid_t *post) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT subject FROM posts "
															   "LEFT JOIN threads ON posts.thread = threads.thread "
															   "WHERE post = ?"),
										   wi_uuid_string(post),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return NULL;
	}
	
	return wi_dictionary_data_for_key(results, WI_STR("subject"));
}



#pragma mark -

wi_boolean_t wd_boards_add_thread(wi_string_t *board, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_p7_message_t			*broadcast;
	wi_uuid_t				*thread;
	wi_date_t				*postdate;
	wd_user_t				*peer;
	wd_board_privileges_t	*privileges;
	
	privileges = wd_boards_privileges_for_board(board);
	
	if(!wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	thread			= wi_uuid();
	postdate		= wi_date();
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO threads "
														 "(thread, board, subject, `text`, post_date, nick, login, ip, icon) "
														 "VALUES "
														 "(?, ?, ?, ?, ?, ?, ?, ?, ?)"),
									 wi_uuid_string(thread),
									 board,
									 subject,
									 text,
									 wi_date_sqlite3_string(postdate),
									 wd_user_nick(user),
									 wd_user_login(user),
									 wd_user_ip(user),
									 wd_user_icon(user),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(peer))) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_added"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wi_p7_message_set_date_for_name(broadcast, postdate, WI_STR("wired.board.post_date"));
				wi_p7_message_set_string_for_name(broadcast, subject, WI_STR("wired.board.subject"));
				wi_p7_message_set_uint32_for_name(broadcast, 0, WI_STR("wired.board.replies"));
				wi_p7_message_set_bool_for_name(broadcast, wi_is_equal(wd_user_login(peer), wd_user_login(user)), WI_STR("wired.board.own_thread"));
				wi_p7_message_set_string_for_name(broadcast, wd_user_nick(user), WI_STR("wired.user.nick"));
				wi_p7_message_set_data_for_name(broadcast, wd_user_icon(user), WI_STR("wired.user.icon"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
	
	return true;
}



wi_boolean_t wd_boards_edit_thread(wi_uuid_t *thread, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t			*results;
	wd_board_privileges_t	*privileges;
	
	privileges = wd_boards_privileges_for_thread(thread);
	
	if(!wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	if(!wd_account_board_edit_all_threads_and_posts(wd_user_account(user))) {
		results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT login FROM threads WHERE thread = ?"),
											   wi_uuid_string(thread),
											   NULL);
		
		if(!results) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);
			
			return false;
		}
		
		if(!wi_is_equal(wd_user_login(user), wi_dictionary_data_for_key(results, WI_STR("login")))) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
			return false;
		}
	}
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET subject =  ?, `text` = ?, edit_date = ? "
														 "WHERE thread = ?"),
									 subject,
									 text,
									 wi_date_sqlite3_string(wi_date()),
									 wi_uuid_string(thread),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
									 
	
	wd_boards_changed_thread(thread, privileges);
	
	return true;
}



wi_boolean_t wd_boards_move_thread(wi_uuid_t *thread, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_p7_message_t			*broadcast;
	wi_dictionary_t			*results;
	wi_runtime_instance_t	*postdate, *editdate, *login;
	wd_user_t				*peer;
	wd_board_privileges_t	*oldprivileges, *newprivileges;
	wi_boolean_t			oldreadable, newreadable;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT board, thread, post_date, edit_date, subject, nick, login, icon "
															   "FROM threads "
															   "WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.thread_not_found"), message);
		
		return false;
	}
	
	oldprivileges = wd_boards_privileges_for_board(wi_dictionary_data_for_key(results, WI_STR("board")));
	newprivileges = wd_boards_privileges_for_board(newboard);
	
	if(!wd_board_privileges_is_writable_by_account(oldprivileges, wd_user_account(user)) &&
	   !wd_board_privileges_is_writable_by_account(newprivileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE threads SET board = ? WHERE thread = ?"),
									 newboard,
									 wi_uuid_string(thread),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			oldreadable = wd_board_privileges_is_readable_by_account(oldprivileges, wd_user_account(peer));
			newreadable = wd_board_privileges_is_readable_by_account(newprivileges, wd_user_account(peer));
			
			if(oldreadable && newreadable) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_moved"), wd_p7_spec);
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
				wd_user_send_message(peer, broadcast);
			}
			else if(oldreadable) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_deleted"), wd_p7_spec);
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wd_user_send_message(peer, broadcast);
			}
			else if(newreadable) {
				postdate	= wi_dictionary_data_for_key(results, WI_STR("post_date"));
				editdate	= wi_dictionary_data_for_key(results, WI_STR("edit_date"));
				login		= wi_dictionary_data_for_key(results, WI_STR("login"));

				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_added"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.board"));
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wi_p7_message_set_date_for_name(broadcast, wi_date_with_sqlite3_string(postdate), WI_STR("wired.board.post_date"));
				
				if(editdate != wi_null())
					wi_p7_message_set_date_for_name(broadcast, wi_date_with_sqlite3_string(editdate), WI_STR("wired.board.edit_date"));
				
				wi_p7_message_set_bool_for_name(broadcast, wi_is_equal(login, wd_user_login(user)), WI_STR("wired.board.post_date"));
				wi_p7_message_set_string_for_name(broadcast, wi_dictionary_data_for_key(results, WI_STR("subject")), WI_STR("wired.board.subject"));
				wi_p7_message_set_string_for_name(broadcast, wi_dictionary_data_for_key(results, WI_STR("nick")), WI_STR("wired.user.nick"));
				wi_p7_message_set_data_for_name(broadcast, wi_dictionary_data_for_key(results, WI_STR("icon")), WI_STR("wired.user.icon"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);

	return true;
}



wi_boolean_t wd_boards_delete_thread(wi_uuid_t *thread, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*results;
	wi_p7_message_t			*broadcast;
	wd_user_t				*peer;
	wd_board_privileges_t	*privileges;
	
    printf("wd_boards_delete_thread : %s\n", wi_string_cstring(wi_uuid_string(thread)));
    
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT board, login FROM threads WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.thread_not_found"), message);
		
		return false;
	}
	
	privileges = wd_boards_privileges_for_thread(thread);
	
	if(!wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	if(!wd_account_board_delete_all_threads_and_posts(wd_user_account(user))) {
		if(!wi_is_equal(wd_user_login(user), wi_dictionary_data_for_key(results, WI_STR("login")))) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
			return false;
		}
	}
	
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM threads WHERE thread = ?"),
									 wi_uuid_string(thread),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
			if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(peer))) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_deleted"), wd_p7_spec);
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wd_user_send_message(peer, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
	
	return true;
}



#pragma mark -

wi_boolean_t wd_boards_add_post(wi_uuid_t *thread, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_uuid_t				*post;
	wi_date_t				*postdate;
	wi_string_t				*board;
	wd_board_privileges_t	*privileges;
	
	board = wd_boards_board_for_thread(thread);
	
	if(!board) {
		wd_user_reply_error(user, WI_STR("wired.error.thread_not_found"), message);
		
		return false;
	}
	
	privileges = wd_boards_privileges_for_board(board);
	
	if(!wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	post			= wi_uuid();
	postdate		= wi_date();
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO posts "
														 "(post, thread, `text`, post_date, nick, login, ip, icon) "
														 "VALUES "
														 "(?, ?, ?, ?, ?, ?, ?, ?)"),
									 wi_uuid_string(post),
									 wi_uuid_string(thread),
									 text,
									 wi_date_sqlite3_string(postdate),
									 wd_user_nick(user),
									 wd_user_login(user),
									 wd_user_ip(user),
									 wd_user_icon(user),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wd_boards_changed_thread(thread, privileges);
	
	return true;
}



wi_boolean_t wd_boards_edit_post(wi_uuid_t *post, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_uuid_t				*thread;
	wi_dictionary_t			*results;
	wd_board_privileges_t	*privileges;
	
	privileges = wd_boards_privileges_for_post(post);
	
	if(!wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT thread, login FROM posts WHERE post = ?"),
										   wi_uuid_string(post),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.post_not_found"), message);
		
		return false;
	}
	
	thread = wi_uuid_with_string(wi_dictionary_data_for_key(results, WI_STR("thread")));
	
	if(!wd_account_board_edit_all_threads_and_posts(wd_user_account(user))) {
		if(!wi_is_equal(wd_user_login(user), wi_dictionary_data_for_key(results, WI_STR("login")))) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
			return false;
		}
	}
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("UPDATE posts SET `text` = ?, edit_date = ? "
														 "WHERE post = ?"),
									 text,
									 wi_date_sqlite3_string(wi_date()),
									 wi_uuid_string(post),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wd_boards_changed_thread(thread, privileges);
	
	return true;
}



wi_boolean_t wd_boards_delete_post(wi_uuid_t *post, wd_user_t *user, wi_p7_message_t *message) {
	wi_uuid_t				*thread;
	wi_dictionary_t			*results;
	wd_board_privileges_t	*privileges;
	
	privileges = wd_boards_privileges_for_post(post);
	
	if(!wd_board_privileges_is_writable_by_account(privileges, wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return false;
	}
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT thread, login FROM posts WHERE post = ?"),
										   wi_uuid_string(post),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.post_not_found"), message);
		
		return false;
	}
	
	thread = wi_uuid_with_string(wi_dictionary_data_for_key(results, WI_STR("thread")));
	
	if(!wd_account_board_delete_all_threads_and_posts(wd_user_account(user))) {
		if(!wi_is_equal(wd_user_login(user), wi_dictionary_data_for_key(results, WI_STR("login")))) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
			return false;
		}
	}
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM posts WHERE post = ?"),
									 wi_uuid_string(post),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	wd_boards_changed_thread(thread, privileges);
	
	return true;
}



#pragma mark -

static void wd_boards_create_tables(void) {
	wi_uinteger_t		version;
	
	version = wd_database_version_for_table(WI_STR("boards"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE boards ("
																 "board TEXT NOT NULL, "
																 "owner TEXT NOT NULL, "
																 "`group` TEXT NOT NULL, "
																 "mode INTEGER NOT NULL, "
																 "PRIMARY KEY (board) "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}

			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TRIGGER boards_delete_trigger "
																 "BEFORE DELETE ON boards "
																 "FOR EACH ROW BEGIN "
																 "DELETE FROM threads WHERE threads.board = OLD.board; "
																 "END"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("boards"));
	
	version = wd_database_version_for_table(WI_STR("threads"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE threads ("
																 "thread TEXT NOT NULL, "
																 "board TEXT NOT NULL, "
																 "subject TEXT NOT NULL, "
																 "`text` TEXT NOT NULL, "
																 "post_date TEXT NOT NULL, "
																 "edit_date TEXT, "
																 "nick TEXT NOT NULL, "
																 "login TEXT NOT NULL, "
																 "ip TEXT NOT NULL, "
																 "icon BLOB, "
																 "PRIMARY KEY (thread) "
																 /* "FOREIGN KEY (board) REFERENCES boards (board) ON DELETE CASCADE " */
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TRIGGER threads_delete_trigger "
																 "BEFORE DELETE ON threads "
																 "FOR EACH ROW BEGIN "
																 "DELETE FROM posts WHERE posts.thread = OLD.thread; "
																 "END"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("threads"));
	
	version = wd_database_version_for_table(WI_STR("posts"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE posts ("
																 "post TEXT NOT NULL, "
																 "thread TEXT NOT NULL, "
																 "`text` TEXT NOT NULL, "
																 "post_date TEXT NOT NULL, "
																 "edit_date TEXT, "
																 "nick TEXT NOT NULL, "
																 "login TEXT NOT NULL, "
																 "ip TEXT NOT NULL, "
																 "icon BLOB, "
																 "PRIMARY KEY (post) "
																 /* "FOREIGN KEY (thread) REFERENCES threads (thread) ON DELETE CASCADE " */
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("posts"));
}



static void wd_boards_convert_boards(void) {
	if(wi_fs_path_exists(WI_STR("board"), NULL)) {
		if(!wi_fs_rename_path(WI_STR("board"), WI_STR("boards")))
			wi_log_error(WI_STR("Could not move \"%@\" to \"%@\": %m"), WI_STR("board"), WI_STR("boards"));
	}
	
	if(wi_fs_path_exists(WI_STR("boards"), NULL)) {
		wi_sqlite3_begin_immediate_transaction(wd_database);
		
		if(wd_boards_convert_boards_to_database(WI_STR("boards"))) {
			wi_log_info(WI_STR("Migrated boards to database"));
			wi_fs_delete_path(WI_STR("boards"));

			wi_sqlite3_commit_transaction(wd_database);
		} else {
			wi_sqlite3_rollback_transaction(wd_database);
		}
	}

	if(wi_fs_path_exists(WI_STR("news"), NULL)) {
		if(wd_boards_convert_news_to_database(WI_STR("news"), WI_STR("News"))) {
			wi_log_info(WI_STR("Migrated news to board \"News\""));
			wi_fs_delete_path(WI_STR("news"));
		}
	}
}



static wi_boolean_t wd_boards_convert_boards_to_database(wi_string_t *path) {
	wi_fsenumerator_t			*fsenumerator;
	wi_array_t					*components;
	wi_string_t					*childpath, *board;
	wd_board_privileges_t		*privileges, *defaultprivileges;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				count;

	defaultprivileges = wd_board_privileges_with_owner(WI_STR("admin"), WI_STR(""),
		WD_BOARD_OWNER_WRITE | WD_BOARD_OWNER_READ | WD_BOARD_GROUP_WRITE | WD_BOARD_GROUP_READ | WD_BOARD_EVERYONE_WRITE | WD_BOARD_EVERYONE_READ);
	
	fsenumerator = wi_fs_enumerator_at_path(path);
	
	if(!fsenumerator) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), path);
		
		return false;
	}
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &childpath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_error(WI_STR("Could not read board \"%@\": %m"), path);
			
			continue;
		}
		
		if(wi_is_equal(wi_string_path_extension(childpath), WI_STR("WiredThread"))) {
			components		= wi_string_path_components(wi_string_substring_from_index(childpath, wi_string_length(path) + 1));
			count			= wi_array_count(components);
			board			= wi_array_components_joined_by_string(wi_array_subarray_with_range(components, wi_make_range(0, count - 1)), WI_STR("/"));
			
			if(!wd_boards_convert_thread_to_database(board, childpath))
				return false;

			wi_fsenumerator_skip_descendents(fsenumerator);
		} else {
			board			= wi_string_substring_from_index(childpath, wi_string_length(path) + 1);
			privileges		= wd_boards_privileges_for_path(childpath);
			
			if(!privileges)
				privileges = defaultprivileges;
			
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO boards "
																 "(board, owner, `group`, mode) "
																 "VALUES "
																 "(?, ?, ?, ?)"),
											 board,
											 privileges->owner,
											 privileges->group,
											 WI_INT32(privileges->mode),
											 NULL)) {
				wi_log_error(WI_STR("Could not execute database statement: %m"));
				
				return false;
			}
		}
	}
	
	return true;
}



static wi_boolean_t wd_boards_convert_thread_to_database(wi_string_t *board, wi_string_t *path) {
	wi_fsenumerator_t			*fsenumerator;
	wi_enumerator_t				*enumerator;
	wi_mutable_dictionary_t		*posts;
	wi_dictionary_t				*dictionary;
	wi_array_t					*dates;
	wi_string_t					*childpath;
	wi_uuid_t					*post, *thread;
	wi_date_t					*date;
	wi_runtime_instance_t		*instance;
	wi_fsenumerator_status_t	status;

	fsenumerator = wi_fs_enumerator_at_path(path);
	
	if(!fsenumerator) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), path);
		
		return false;
	}
	
	posts = wi_mutable_dictionary();
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &childpath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_error(WI_STR("Could not read thread \"%@\": %m"), path);
			
			continue;
		}
		
		if(wi_is_equal(wi_string_path_extension(childpath), WI_STR("WiredPost"))) {
			instance = wi_plist_read_instance_from_file(childpath);
				
			if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
				post = wi_uuid_with_string(wi_string_by_deleting_path_extension(wi_string_last_path_component(childpath)));
				date = wi_dictionary_data_for_key(instance, WI_STR("wired.board.post_date"));
				
				wi_mutable_dictionary_set_data_for_key(instance, post, WI_STR("wired.board.post"));
				wi_mutable_dictionary_set_data_for_key(posts, instance, date);
			}
		}
	}
	
	dates = wi_array_by_sorting(wi_dictionary_all_keys(posts), wi_date_compare);
	
	if(wi_array_count(dates) > 0) {
		dictionary		= wi_dictionary_data_for_key(posts, WI_ARRAY(dates, 0));
		thread			= wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post"));
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO threads "
															 "(thread, board, subject, `text`, post_date, edit_date, nick, login, ip, icon) "
															 "VALUES "
															 "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
										 thread,
										 board,
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.subject")),
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.text")),
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post_date")),
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"))
											? wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"))
											: wi_null(),
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")),
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.login")),
										 WI_STR(""),
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.icon")),
										 NULL)) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
			
			return false;
		}
		
		if(wi_array_count(dates) > 1) {
			enumerator = wi_array_data_enumerator(wi_array_subarray_with_range(dates, wi_make_range(1, wi_array_count(dates) - 1)));
			
			while((date = wi_enumerator_next_data(enumerator))) {
				dictionary = wi_dictionary_data_for_key(posts, date);
				
				if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO posts "
																	 "(post, thread, `text`, post_date, edit_date, nick, login, ip, icon) "
																	 "VALUES "
																	 "(?, ?, ?, ?, ?, ?, ?, ?, ?)"),
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post")),
												 thread,
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.text")),
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post_date")),
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"))
													? wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"))
													: wi_null(),
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")),
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.login")),
												 WI_STR(""),
												 wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.icon")),
												 NULL)) {
					wi_log_error(WI_STR("Could not execute database statement: %m"));
					
					return false;
				}
			}
		}
	}
	
	return true;
}



static wi_boolean_t wd_boards_convert_news_to_database(wi_string_t *path, wi_string_t *board) {
	wi_array_t			*array;
	wi_file_t			*file;
	wi_string_t			*string, *subject;
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO boards "
														 "(board, owner, `group`, mode) "
														 "VALUES "
														 "(?, ?, ?, ?)"),
									 board,
									 WI_STR("admin"),
									 WI_STR(""),
									 WI_INT32(WD_BOARD_OWNER_WRITE | WD_BOARD_OWNER_READ |
											  WD_BOARD_GROUP_WRITE | WD_BOARD_GROUP_READ |
											  WD_BOARD_EVERYONE_WRITE | WD_BOARD_EVERYONE_READ),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}
	
	file = wi_file_for_reading(path);
	
	if(!file) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), path);
		
		return false;
	}
	
	while((string = wi_file_read_to_string(file, WI_STR("\35")))) {
		array = wi_string_components_separated_by_string(string, WI_STR("\34"));
		
		if(wi_array_count(array) == 3) {
			subject = WI_ARRAY(array, 2);
			
			if(wi_string_length(subject) > 32)
				subject = wi_string_with_format(WI_STR("%@..."), wi_string_substring_to_index(subject, 31));
			
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO threads "
																 "(thread, board, subject, `text`, post_date, edit_date, nick, login, ip, icon) "
																 "VALUES "
																 "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
											 wi_uuid(),
											 board,
											 subject,
											 WI_ARRAY(array, 2),
											 wi_date_with_rfc3339_string(WI_ARRAY(array, 1)),
											 wi_null(),
											 WI_ARRAY(array, 0),
											 WI_STR(""),
											 WI_STR(""),
											 wi_null(),
											 NULL)) {
				wi_log_error(WI_STR("Could not execute database statement: %m"));
				
				return false;
			}
		}
	}
	
	return true;
}



static wd_board_privileges_t * wd_boards_privileges_for_path(wi_string_t *path) {
	wi_string_t				*permissionspath, *string;
	wi_array_t				*array;
	wi_fs_stat_t			sb;
	
	permissionspath = wi_string_by_appending_path_component(path, WI_STR(".wired/permissions"));
	
	if(!wi_fs_stat_path(permissionspath, &sb)) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), permissionspath);
		
		return NULL;
	}
	
	if(sb.size > 128) {
		wi_log_error(WI_STR("Could not read %@: Size is too large (%u"), permissionspath, sb.size);
		
		return NULL;
	}
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), permissionspath));
	
	if(!string) {
		wi_log_error(WI_STR("Could not read \"%@\": %m"), permissionspath);
		
		return NULL;
	}
	
	string		= wi_string_by_deleting_surrounding_whitespace(string);
	array		= wi_string_components_separated_by_string(string, WI_STR("\34"));
	
	if(wi_array_count(array) != 3) {
		wi_log_error(WI_STR("Could not read \"%@\": Contents is malformed (\"%@\")"), permissionspath, string);
		
		return NULL;
	}
	
	return wd_board_privileges_with_owner(WI_ARRAY(array, 0), WI_ARRAY(array, 1), wi_string_uint32(WI_ARRAY(array, 2)));
}



#pragma mark -

static void wd_boards_changed_thread(wi_uuid_t *thread, wd_board_privileges_t *privileges) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*results;
	wi_p7_message_t			*broadcast;
	wi_runtime_instance_t	*subject, *editdate, *replies, *latestreply, *latestreplydate;
	wd_user_t				*user;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT subject, edit_date, "
															   "(SELECT COUNT(*) FROM posts WHERE posts.thread = threads.thread) AS replies, "
															   "(SELECT post FROM posts WHERE posts.thread = threads.thread ORDER BY post_date DESC LIMIT 1) AS latest_reply, "
															   "(SELECT post_date FROM posts WHERE posts.thread = threads.thread ORDER BY post_date DESC LIMIT 1) AS latest_reply_date "
															   "FROM threads "
															   "WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return;
	}
	
	subject				= wi_dictionary_data_for_key(results, WI_STR("subject"));
	editdate			= wi_dictionary_data_for_key(results, WI_STR("edit_date"));
	replies				= wi_dictionary_data_for_key(results, WI_STR("replies"));
	latestreply			= wi_dictionary_data_for_key(results, WI_STR("latest_reply"));
	latestreplydate		= wi_dictionary_data_for_key(results, WI_STR("latest_reply_date"));
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(user)) {
			if(wd_board_privileges_is_readable_by_account(privileges, wd_user_account(user))) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_changed"), wd_p7_spec);
				wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
				wi_p7_message_set_string_for_name(broadcast, subject, WI_STR("wired.board.subject"));

				if(editdate != wi_null())
					wi_p7_message_set_date_for_name(broadcast, wi_date_with_sqlite3_string(editdate), WI_STR("wired.board.edit_date"));
				
				wi_p7_message_set_number_for_name(broadcast, replies, WI_STR("wired.board.replies"));
				
				if(latestreply != wi_null())
					wi_p7_message_set_uuid_for_name(broadcast, wi_uuid_with_string(latestreply), WI_STR("wired.board.latest_reply"));
				
				if(latestreplydate != wi_null())
					wi_p7_message_set_date_for_name(broadcast, wi_date_with_sqlite3_string(latestreplydate), WI_STR("wired.board.latest_reply_date"));
				
				wd_user_send_message(user, broadcast);
			}
		}
	}
	
	wi_dictionary_unlock(wd_users);
}



static wd_board_privileges_t * wd_boards_privileges_for_board(wi_string_t *board) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT owner, `group`, mode FROM boards WHERE board = ?"),
										   board,
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}
	
	return wd_board_privileges_with_sqlite3_results(results);
}



static wd_board_privileges_t * wd_boards_privileges_for_thread(wi_uuid_t *thread) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT owner, `group`, mode "
															   "FROM threads "
															   "LEFT JOIN boards ON threads.board = boards.board "
															   "WHERE thread = ?"),
										   wi_uuid_string(thread),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}
	
	return wd_board_privileges_with_sqlite3_results(results);
}



static wd_board_privileges_t * wd_boards_privileges_for_post(wi_uuid_t *post) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT owner, `group`, mode "
															   "FROM posts "
															   "LEFT JOIN threads ON posts.thread = threads.thread "
															   "LEFT JOIN boards ON threads.board = boards.board "
															   "WHERE post = ?"),
										   wi_uuid_string(post),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}
	
	return wd_board_privileges_with_sqlite3_results(results);
}



#pragma mark -

static wd_board_privileges_t * wd_board_privileges_alloc(void) {
	return wi_runtime_create_instance(wd_board_privileges_runtime_id, sizeof(wd_board_privileges_t));
}



static void wd_board_privileges_dealloc(wi_runtime_instance_t *instance) {
	wd_board_privileges_t		*privileges = instance;
	
	wi_release(privileges->owner);
	wi_release(privileges->group);
}



#pragma mark -

static wd_board_privileges_t * wd_board_privileges_with_message(wi_p7_message_t *message) {
	wd_board_privileges_t		*privileges;
	wi_p7_boolean_t				value;
	
	privileges = wd_board_privileges_alloc();
	privileges->owner = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.board.owner")));

	if(!privileges->owner)
		privileges->owner = wi_retain(WI_STR(""));
	
	privileges->group = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.board.group")));
	
	if(!privileges->group)
		privileges->group = wi_retain(WI_STR(""));
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.owner.read")) && value)
		privileges->mode |= WD_BOARD_OWNER_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.owner.write")) && value)
		privileges->mode |= WD_BOARD_OWNER_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.group.read")) && value)
		privileges->mode |= WD_BOARD_GROUP_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.group.write")) && value)
		privileges->mode |= WD_BOARD_GROUP_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.everyone.read")) && value)
		privileges->mode |= WD_BOARD_EVERYONE_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.everyone.write")) && value)
		privileges->mode |= WD_BOARD_EVERYONE_WRITE;

	return wi_autorelease(privileges);
}



static wd_board_privileges_t * wd_board_privileges_with_owner(wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode) {
	wd_board_privileges_t		*privileges;
	
	privileges				= wd_board_privileges_alloc();
	privileges->owner		= wi_retain(owner);
	privileges->group		= wi_retain(group);
	privileges->mode		= mode;
	
	return wi_autorelease(privileges);
}



static wd_board_privileges_t * wd_board_privileges_with_sqlite3_results(wi_dictionary_t *results) {
	return wd_board_privileges_with_owner(wi_dictionary_data_for_key(results, WI_STR("owner")),
										  wi_dictionary_data_for_key(results, WI_STR("group")),
										  wi_number_integer(wi_dictionary_data_for_key(results, WI_STR("mode"))));
}
	


#pragma mark -

static wi_boolean_t wd_board_privileges_is_readable_by_account(wd_board_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_BOARD_EVERYONE_READ)
		return true;
	
	if(privileges->mode & WD_BOARD_GROUP_READ && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_BOARD_OWNER_READ && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



static wi_boolean_t wd_board_privileges_is_writable_by_account(wd_board_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_BOARD_EVERYONE_WRITE)
		return true;
	
	if(privileges->mode & WD_BOARD_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_BOARD_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}
