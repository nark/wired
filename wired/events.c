/* $Id$ */

/*
 *  Copyright (c) 2009 Axel Andersson
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

#include "events.h"
#include "main.h"
#include "server.h"
#include "users.h"

static void								wd_events_create_tables(void);
static wi_boolean_t						wd_events_convert_events(wi_string_t *);



void wd_events_initialize(void) {
	wd_events_create_tables();
	
	if(wi_fs_path_exists(WI_STR("events"), NULL)) {
		if(wd_events_convert_events(WI_STR("events"))) {
			wi_log_info(WI_STR("Migrated events to database"));
			wi_fs_delete_path(WI_STR("events"));
		}
	}
}



#pragma mark -

wi_boolean_t wd_events_reply_first_time(wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t				*results;
	wi_p7_message_t				*reply;
	wi_date_t					*date;
	wi_runtime_instance_t		*instance;
	
	results = wi_sqlite3_execute_statement(wd_database,
		WI_STR("SELECT time FROM events ORDER BY time ASC LIMIT 1"), NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	instance	= wi_dictionary_data_for_key(results, WI_STR("time"));
	date		= instance ? instance : wi_date();
	
	reply = wi_p7_message_with_name(WI_STR("wired.event.first_time"), wd_p7_spec);
	wi_p7_message_set_date_for_name(reply, date, WI_STR("wired.event.first_time"));
	wd_user_reply_message(user, reply, message);
	
	return true;
}



wi_boolean_t wd_events_reply_events(wi_date_t *fromtime, wi_uinteger_t numberofdays, wi_uinteger_t lasteventcount, wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_p7_message_t				*reply;
	wi_dictionary_t				*results;
	wi_runtime_instance_t		*event, *parameters, *time, *nick, *login, *ip;
	
	if(fromtime) {
		if(numberofdays > 0) {
			statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT event, parameters, time, nick, login, ip "
																		 "FROM events "
																		 "WHERE time >= DATETIME(?) AND time <= DATETIME(?, '+? day')"),
													 fromtime,
													 fromtime,
													 WI_INT32(numberofdays),
													 NULL);
		} else {
			statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT event, parameters, time, nick, login, ip "
																		 "FROM events "
																		 "WHERE time >= DATETIME(?)"),
													 fromtime,
													 NULL);
		}
	} else {
		statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT event, parameters, time, nick, login, ip "
																	 "FROM events "
																	 "ORDER BY time DESC LIMIT ?"),
												 WI_INT32(lasteventcount),
												 NULL);
	}
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		event			= wi_dictionary_data_for_key(results, WI_STR("event"));
		parameters		= wi_dictionary_data_for_key(results, WI_STR("parameters"));
		time			= wi_dictionary_data_for_key(results, WI_STR("time"));
		nick			= wi_dictionary_data_for_key(results, WI_STR("nick"));
		login			= wi_dictionary_data_for_key(results, WI_STR("login"));
		ip				= wi_dictionary_data_for_key(results, WI_STR("ip"));
		
		reply = wi_p7_message_with_name(WI_STR("wired.event.event_list"), wd_p7_spec);
		
		wi_p7_message_set_enum_name_for_name(reply, event, WI_STR("wired.event.event"));
		wi_p7_message_set_date_for_name(reply, time, WI_STR("wired.event.time"));
		
		if(parameters != wi_null())
			wi_p7_message_set_list_for_name(reply, wi_string_components_separated_by_string(parameters, WI_STR("\34")), WI_STR("wired.event.parameters"));
		
		wi_p7_message_set_string_for_name(reply, nick == wi_null() ? WI_STR("") : nick, WI_STR("wired.user.nick"));
		wi_p7_message_set_string_for_name(reply, login == wi_null() ? WI_STR("") : login, WI_STR("wired.user.login"));
		wi_p7_message_set_string_for_name(reply, ip, WI_STR("wired.user.ip"));
		
		wd_user_reply_message(user, reply, message);
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}

	reply = wi_p7_message_with_name(WI_STR("wired.event.event_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	return true;
}



#pragma mark -

void wd_events_add_event(wi_string_t *event, wd_user_t *user, ...) {
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*results;
	wi_array_t				*parameters;
	wi_p7_message_t			*message;
	wi_date_t				*date;
	wd_user_t				*peer;
	va_list					ap;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT event "
															   "FROM events "
															   "WHERE event = ? AND nick = ? AND login = ? AND ip = ? "
															   "ORDER BY time DESC LIMIT 1"),
										   event,
										   wd_user_nick(user),
										   wd_user_login(user),
										   wd_user_ip(user),
										   NULL);
	
	if(results) {
		if(wi_dictionary_count(results) > 0) {
			if(wi_is_equal(event, WI_STR("wired.event.user.got_users")) || wi_is_equal(event, WI_STR("wired.event.user.got_info")))
				return;
		}
	} else {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
	
	va_start(ap, user);
	parameters = wi_array_with_arguments(ap);
	va_end(ap);
	
	date = wi_date();
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO events "
															   "(event, parameters, time, nick, login, ip) "
															   "VALUES "
															   "(?, ?, ?, ?, ?, ?)"),
										   event,
										   wi_array_count(parameters) > 0 ? wi_array_components_joined_by_string(parameters, WI_STR("\34")) : wi_null(),
										   wi_date_sqlite3_string(date),
										   wd_user_nick(user) ? wd_user_nick(user) : wi_null(),
										   wd_user_login(user) ? wd_user_login(user) : wi_null(),
										   wd_user_ip(user),
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return;
	}

	message = wi_p7_message_with_name(WI_STR("wired.event.event"), wd_p7_spec);
	
	wi_p7_message_set_enum_name_for_name(message, event, WI_STR("wired.event.event"));
	wi_p7_message_set_date_for_name(message, date, WI_STR("wired.event.time"));
	
	if(wi_array_count(parameters) > 0)
		wi_p7_message_set_list_for_name(message, parameters, WI_STR("wired.event.parameters"));
	
	wi_p7_message_set_string_for_name(message, wd_user_nick(user) ? wd_user_nick(user) : WI_STR(""), WI_STR("wired.user.nick"));
	wi_p7_message_set_string_for_name(message, wd_user_login(user) ? wd_user_login(user) : WI_STR(""), WI_STR("wired.user.login"));
	wi_p7_message_set_string_for_name(message, wd_user_ip(user), WI_STR("wired.user.ip"));
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_events(peer))
			wd_user_send_message(peer, message);
	}
	
	wi_dictionary_unlock(wd_users);
}



#pragma mark -

static void wd_events_create_tables(void) {
	wi_uinteger_t		version;
	
	version = wd_database_version_for_table(WI_STR("events"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE events ( "
																 "event TEXT NOT NULL, "
																 "parameters TEXT, "
																 "time TEXT NOT NULL, "
																 "nick TEXT, "
																 "login TEXT, "
																 "ip TEXT NOT NULL "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}

			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE INDEX events_time ON events(time)"), NULL))
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));

			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE INDEX events_nick ON events(nick)"), NULL))
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));

			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE INDEX events_login ON events(login)"), NULL))
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));

			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE INDEX events_ip ON events(ip)"), NULL))
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("events"));
}



static wi_boolean_t wd_events_convert_events(wi_string_t *path) {
	wi_fsenumerator_t			*fsenumerator;
	wi_enumerator_t				*enumerator;
	wi_dictionary_t				*dictionary, *results;
	wi_array_t					*parameters;
	wi_string_t					*archivepath;
	wi_runtime_instance_t		*instance;
	wi_fsenumerator_status_t	status;
	wi_boolean_t				result;
	
	fsenumerator = wi_fs_enumerator_at_path(path);
	
	if(!fsenumerator) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), path);
		
		return false;
	}
	
	wi_sqlite3_begin_immediate_transaction(wd_database);
	
	result = true;
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &archivepath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_error(WI_STR("Could not read event archive \"%@\": %m"), archivepath);
			
			result = false;
			break;
		}
		
		instance = wi_plist_read_instance_from_file(archivepath);
		
		if(!instance) {
			wi_log_error(WI_STR("Could not read events from \"%@\": %m"), archivepath);
			
			result = false;
			break;
		}
		
		if(wi_runtime_id(instance) != wi_array_runtime_id()) {
			wi_log_error(WI_STR("Could not read events from \"%@\": Invalid format"), archivepath);
			
			result = false;
			break;
		}
		
		enumerator = wi_array_data_enumerator(instance);
		
		while((dictionary = wi_enumerator_next_data(enumerator))) {
			parameters = wi_dictionary_data_for_key(dictionary, WI_STR("wired.event.parameters"));
			
			results = wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO events "
																	   "(event, parameters, time, nick, login, ip) "
																	   "VALUES "
																	   "(?, ?, ?, ?, ?, ?)"),
												   wi_dictionary_data_for_key(dictionary, WI_STR("wired.event.event")),
												   parameters ? wi_array_components_joined_by_string(parameters, WI_STR("\34")) : wi_null(),
												   wi_dictionary_data_for_key(dictionary, WI_STR("wired.event.time")),
												   wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")),
												   wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.login")),
												   wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.ip")),
												   NULL);
			
			if(!results) {
				wi_log_error(WI_STR("Could not execute database statement: %m"));
				
				result = false;
				break;
			}
		}
		
		if(!result)
			break;
	}
	
	if(result)
		wi_sqlite3_commit_transaction(wd_database);
	else
		wi_sqlite3_rollback_transaction(wd_database);
	
	return result;
}
