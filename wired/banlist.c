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

#include <wired/wired.h>

#include "banlist.h"
#include "main.h"
#include "server.h"

static void									wd_banlist_create_tables(void);
static wi_boolean_t							wd_banlist_convert_banlist(wi_string_t *);


void wd_banlist_initialize(void) {
	wd_banlist_create_tables();
	
	if(wi_fs_path_exists(WI_STR("banlist"), NULL)) {
		if(wd_banlist_convert_banlist(WI_STR("banlist"))) {
			wi_log_info(WI_STR("Migrated banlist to database"));
			wi_fs_delete_path(WI_STR("banlist"));
		}
	}
}



#pragma mark -

wi_boolean_t wd_banlist_ip_is_banned(wi_string_t *ip, wi_date_t **expiration_date) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_runtime_instance_t		*instance;
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM banlist "
														 "WHERE strftime('%%s', 'now') - STRFTIME('%%s', expiration_date) > 0"),
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
	}
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT ip, expiration_date FROM banlist"), NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		if(wi_ip_matches_string(wi_dictionary_data_for_key(results, WI_STR("ip")), ip)) {
			instance = wi_dictionary_data_for_key(results, WI_STR("expiration_date"));
			
			if(instance == wi_null())
				*expiration_date = NULL;
			else
				*expiration_date = wi_date_with_sqlite3_string(instance);
			
			return true;
		}
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		return false;
	}

	return false;
}



#pragma mark -

void wd_banlist_reply_bans(wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_p7_message_t				*reply;
	wi_dictionary_t				*results;
	wi_runtime_instance_t		*instance;
	
	statement = wi_sqlite3_prepare_statement(wd_database, WI_STR("SELECT ip, expiration_date FROM banlist"), NULL);
	
	if(!statement) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}
	
	while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
		reply = wi_p7_message_with_name(WI_STR("wired.banlist.list"), wd_p7_spec);
		wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(results, WI_STR("ip")), WI_STR("wired.banlist.ip"));
		
		instance = wi_dictionary_data_for_key(results, WI_STR("expiration_date"));
		
		if(instance != wi_null())
			wi_p7_message_set_date_for_name(reply, instance, WI_STR("wired.banlist.expiration_date"));
		
		wd_user_reply_message(user, reply, message);
	}
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}

	reply = wi_p7_message_with_name(WI_STR("wired.banlist.list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



wi_boolean_t wd_banlist_add_ban(wi_string_t *ip, wi_date_t *expiration_date, wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t		*results;
	wi_string_t			*string;
	
	if(expiration_date && wi_date_time_interval(expiration_date) - wi_time_interval() < 1.0) {
		wi_log_error(WI_STR("Could not add ban for \"%@\" expiring at %@: Negative expiration date"),
			ip, wi_date_string_with_format(expiration_date, WI_STR("%Y-%m-%d %H:%M:%S")));
		wd_user_reply_internal_error(user, WI_STR("Ban has negative expiration date"), message);
		
		return false;
	}
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT ip FROM banlist WHERE ip = ?"),
										   ip,
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) > 0) {
		wd_user_reply_error(user, WI_STR("wired.error.ban_exists"), message);
		
		return false;
	}
	
	string = expiration_date ? wi_date_sqlite3_string(expiration_date) : NULL;
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO banlist "
														 "(ip, expiration_date) "
														 "VALUES "
														 "(?, ?)"),
									 ip,
									 string,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	return true;
}



wi_boolean_t wd_banlist_delete_ban(wi_string_t *ip, wi_date_t *expiration_date, wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t		*results;
	
	results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT ip FROM banlist WHERE ip = ?"),
										   ip,
										   NULL);
	
	if(!results) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	if(wi_dictionary_count(results) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.ban_not_found"), message);
		
		return false;
	}
	
	if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM banlist WHERE ip = ?"),
									 ip,
									 NULL)) {
		wi_log_error(WI_STR("Could not execute database statement: %m"));
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	return true;
}



#pragma mark -

static void wd_banlist_create_tables(void) {
	wi_uinteger_t		version;
	
	version = wd_database_version_for_table(WI_STR("banlist"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE banlist ( "
																 "ip TEXT NOT NULL, "
																 "expiration_date TEXT, "
																 "PRIMARY KEY (ip) "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("banlist"));
}



static wi_boolean_t wd_banlist_convert_banlist(wi_string_t *path) {
	wi_file_t			*file;
	wi_string_t			*string;
	wi_boolean_t		result;
	
	file = wi_file_for_reading(path);
	
	if(file) {
		result = true;
		
		wi_sqlite3_begin_immediate_transaction(wd_database);
		
		while((string = wi_file_read_config_line(file))) {
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO banlist "
																 "(ip, expiration_date) "
																 "VALUES "
																 "(?, NULL)"),
											 string,
											 NULL)) {
				wi_log_error(WI_STR("Could not execute database statement: %m"));
				
				result = false;
				break;
			}
		}
		
		if(result)
			wi_sqlite3_commit_transaction(wd_database);
		else
			wi_sqlite3_rollback_transaction(wd_database);
		
		return result;
	} else {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), path);
		
		return false;
	}
}
