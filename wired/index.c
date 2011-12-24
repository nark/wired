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

#include "accounts.h"
#include "files.h"
#include "index.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"

#define WD_INDEX_MAX_LEVEL						20


static void										wd_index_create_tables(void);

static void										wd_index_update_index(wi_timer_t *);
static void										wd_index_thread(wi_runtime_instance_t *);
static void										wd_index_index_path(wi_string_t *, wi_string_t *);


static wi_time_interval_t						wd_index_time;
static wi_timer_t								*wd_index_timer;
static wi_lock_t								*wd_index_lock;
static wi_uinteger_t							wd_index_level;
static wi_mutable_dictionary_t					*wd_index_dictionary;

wi_uinteger_t									wd_index_files_count;
wi_uinteger_t									wd_index_directories_count;
wi_file_offset_t								wd_index_files_size;



void wd_index_initialize(void) {
	wd_index_create_tables();
	
	wi_fs_delete_path(WI_STR("index"));
	wi_fs_delete_path(WI_STR("files.index"));
	
	wd_index_lock	= wi_lock_init(wi_lock_alloc());
	wd_index_timer	= wi_timer_init_with_function(wi_timer_alloc(), wd_index_update_index, 0.0, true);
}



void wd_index_schedule(void) {
	wd_index_time = wi_config_time_interval_for_name(wd_config, WI_STR("index time"));
	
	if(wd_index_time > 0.0)
		wi_timer_reschedule(wd_index_timer, wd_index_time);
	else
		wi_timer_invalidate(wd_index_timer);
}



#pragma mark -

static void wd_index_create_tables(void) {
	wi_uinteger_t		version;
	
	version = wd_database_version_for_table(WI_STR("index"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE `index` ( "
																 "name TEXT NOT NULL, "
																 "virtual_path TEXT NOT NULL, "
																 "real_path TEXT NOT NULL, "
																 "alias INTEGER NOT NULL "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}

			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE INDEX index_real_path ON `index`(real_path)"), NULL))
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("index"));

	version = wd_database_version_for_table(WI_STR("index_metadata"));
	
	switch(version) {
		case 0:
			if(!wi_sqlite3_execute_statement(wd_database, WI_STR("CREATE TABLE index_metadata ( "
																 "date TEXT NOT NULL, "
																 "files_count INTEGER NOT NULL, "
																 "directories_count INTEGER NOT NULL, "
																 "files_size INTEGER NOT NULL "
																 ")"),
											 NULL)) {
				wi_log_fatal(WI_STR("Could not execute database statement: %m"));
			}
			break;
	}
	
	wd_database_set_version_for_table(1, WI_STR("index_metadata"));
}



#pragma mark -

static void wd_index_update_index(wi_timer_t *timer) {
	wd_index_index_files(false);
}



void wd_index_index_files(wi_boolean_t startup) {
	wi_dictionary_t		*results;
	wi_time_interval_t	interval, index_time;
	wi_boolean_t		index = true;
	
	if(startup) {
		results = wi_sqlite3_execute_statement(wd_database, WI_STR("SELECT date, files_count, directories_count, files_size "
																   "FROM index_metadata"), NULL);
		
		if(results) {
			if(wi_dictionary_count(results) > 0) {
				interval = wi_date_time_interval_since_now(wi_date_with_sqlite3_string(
					wi_dictionary_data_for_key(results, WI_STR("date"))));
				index_time = (wd_index_time > 0.0) ? wd_index_time : 3600.0;
				
				if(interval < index_time) {
					wd_index_files_count		= wi_number_integer(wi_dictionary_data_for_key(results, WI_STR("files_count")));
					wd_index_directories_count	= wi_number_integer(wi_dictionary_data_for_key(results, WI_STR("directories_count")));
					wd_index_files_size			= wi_number_int64(wi_dictionary_data_for_key(results, WI_STR("files_size")));

					wi_log_info(WI_STR("Found %u %s and %u %s for a total of %@ (%llu bytes) created %.2f seconds ago"),
						wd_index_files_count,
						wd_index_files_count == 1
							? "file"
							: "files",
						wd_index_directories_count,
						wd_index_directories_count == 1
							? "directory"
							: "directories",
						wd_files_string_for_bytes(wd_index_files_size),
						wd_index_files_size,
						interval);
					
					wd_trackers_register();
					
					index = false;
				}
			}
		} else {
			wi_log_fatal(WI_STR("Could not execute database statement: %m"));
		}
	}
	
	if(index) {
		if(!wi_thread_create_thread_with_priority(wd_index_thread, wi_number_with_bool(startup), 0.0))
			wi_log_fatal(WI_STR("Could not create an index thread: %m"));
	}
}



static void wd_index_thread(wi_runtime_instance_t *argument) {
	wi_pool_t					*pool;
	wi_time_interval_t			interval;
	wi_boolean_t				startup = wi_number_bool(argument);
	
	pool = wi_pool_init(wi_pool_alloc());
	
	if(wi_lock_trylock(wd_index_lock)) {
		wi_log_info(WI_STR("Indexing files..."));
		
		interval					= wi_time_interval();
		wd_index_files_count		= 0;
		wd_index_directories_count	= 0;
		wd_index_files_size			= 0;
		wd_index_level				= 0;
		
		wd_index_dictionary = wi_dictionary_init_with_capacity_and_callbacks(wi_mutable_dictionary_alloc(), 0,
			wi_dictionary_null_key_callbacks, wi_dictionary_default_value_callbacks);
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM `index`"), NULL))
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("PRAGMA synchronous=OFF"), NULL))
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		wd_index_index_path(wi_string_by_resolving_aliases_in_path(wd_files), NULL);
		
		wi_log_info(WI_STR("Indexed %u %s and %u %s for a total of %@ (%llu bytes) in %.2f seconds"),
			wd_index_files_count,
			wd_index_files_count == 1
				? "file"
				: "files",
			wd_index_directories_count,
			wd_index_directories_count == 1
				? "directory"
				: "directories",
			wd_files_string_for_bytes(wd_index_files_size),
			wd_index_files_size,
			wi_time_interval() - interval);
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("PRAGMA synchronous=ON"), NULL))
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM index_metadata"), NULL))
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO index_metadata "
															 "(date, files_count, directories_count, files_size) "
															 "VALUES "
															 "(?, ?, ?, ?)"),
										 wi_date_sqlite3_string(wi_date()),
										 wi_number_with_integer(wd_index_files_count),
										 wi_number_with_integer(wd_index_directories_count),
										 wi_number_with_int64(wd_index_files_size),
										 NULL)) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		}
		   
		wi_release(wd_index_dictionary);
			
		wd_broadcast_message(wd_server_info_message());
			
		if(startup)
			wd_trackers_register();
		
		wi_lock_unlock(wd_index_lock);
	}
	
	wi_release(pool);
}



static void wd_index_index_path(wi_string_t *path, wi_string_t *pathprefix) {
	wi_pool_t					*pool;
	wi_fsenumerator_t			*fsenumerator;
	wi_string_t					*filepath, *virtualpath, *resolvedpath, *newpathprefix;
	wi_mutable_set_t			*set;
	wi_number_t					*number;
	wi_fs_stat_t				sb, lsb;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				i = 0, pathlength;
	wi_boolean_t				alias, recurse;
	
	if(wd_index_level >= WD_INDEX_MAX_LEVEL) {
		wi_log_warn(WI_STR("Skipping index of \"%@\": %s"),
			path, "Directory too deep");
		
		return;
	}

	fsenumerator = wi_fs_enumerator_at_path(path);

	if(!fsenumerator) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), path);
		
		return;
	}
	
	pool = wi_pool_init_with_debug(wi_pool_alloc(), false);
	
	pathlength = wi_string_length(path);
	
	if(pathlength == 1)
		pathlength--;
	
	wd_index_level++;

	while((status = wi_fsenumerator_get_next_path(fsenumerator, &filepath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_warn(WI_STR("Skipping index of \"%@\": %m"), filepath);
			
			continue;
		}
		
		if(wi_fs_path_is_invisible(filepath)) {
			wi_fsenumerator_skip_descendents(fsenumerator);
			
			continue;
		}

		alias = wi_fs_path_is_alias(filepath);
		
		if(alias)
			resolvedpath = wi_string_by_resolving_aliases_in_path(filepath);
		else
			resolvedpath = filepath;
		
		if(!wi_fs_lstat_path(resolvedpath, &lsb)) {
			wi_log_warn(WI_STR("Skipping index of \"%@\": %m"), resolvedpath);
			wi_fsenumerator_skip_descendents(fsenumerator);
		} else {
			if(!wi_fs_stat_path(resolvedpath, &sb))
				sb = lsb;
			
			set = wi_dictionary_data_for_key(wd_index_dictionary, (void *) (intptr_t) lsb.dev);
			
			if(!set) {
				set = wi_set_init_with_capacity(wi_mutable_set_alloc(), 1000, false);
				wi_mutable_dictionary_set_data_for_key(wd_index_dictionary, set, (void *) (intptr_t) lsb.dev);
				wi_release(set);
			}
			
			number = wi_number_init_with_value(wi_number_alloc(), WI_NUMBER_INT64, &lsb.ino);
			
			if(!wi_set_contains_data(set, number)) {
				wi_mutable_set_add_data(set, number);
				
				recurse = (alias && S_ISDIR(sb.mode));
				
				virtualpath	= wi_string_substring_from_index(filepath, pathlength);
				
				if(pathprefix)
					virtualpath = wi_string_by_inserting_string_at_index(virtualpath, pathprefix, 0);
				
				if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO `index` "
																	 "(name, virtual_path, real_path, alias) "
																	 "VALUES "
																	 "(?, ?, ?, ?)"),
												 wi_string_last_path_component(virtualpath),
												 virtualpath,
												 resolvedpath,
												 wi_number_with_bool(alias),
												 NULL)) {
					wi_log_error(WI_STR("Could not execute database statement: %m"));
				}
				
				if(S_ISDIR(sb.mode)) {
					wd_index_directories_count++;
				} else {
					wd_index_files_count++;
					wd_index_files_size += sb.size + wi_fs_resource_fork_size_for_path(resolvedpath);
				}
				
				if(wd_files_type_with_stat(resolvedpath, &sb) == WD_FILE_TYPE_DROPBOX) {
					wi_fsenumerator_skip_descendents(fsenumerator);
				}
				else if(recurse) {
					if(pathprefix) {
						newpathprefix = wi_string_by_appending_path_component(pathprefix,
							wi_string_substring_from_index(filepath, pathlength + 1));
					} else {
						newpathprefix = wi_string_substring_from_index(filepath, pathlength);
					}
					
					wd_index_index_path(resolvedpath, newpathprefix);
				}
			}
		
			wi_release(number);
		}

		if(++i % 100 == 0)
			wi_pool_drain(pool);
	}
	
	wd_index_level--;
	
	wi_release(pool);
}



#pragma mark -

void wd_index_add_file(wi_string_t *path) {
	wi_string_t			*virtualpath;
	wi_uinteger_t		pathlength;
	
	if(wi_lock_trylock(wd_index_lock)) {
		pathlength = wi_string_length(wd_files);
		
		if(pathlength == 1)
			pathlength--;
		
		virtualpath	= wi_string_substring_from_index(path, pathlength);

		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("INSERT INTO `index` "
															 "(name, virtual_path, real_path, alias) "
															 "VALUES "
															 "(?, ?, ?, ?)"),
										 wi_string_last_path_component(virtualpath),
										 virtualpath,
										 path,
										 wi_number_with_bool(false),
										 NULL)) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		}
		
		wi_lock_unlock(wd_index_lock);
	}
}



void wd_index_delete_file(wi_string_t *path) {
	if(wi_lock_trylock(wd_index_lock)) {
		wi_log_info(WI_STR("DELETE FROM index WHERE real_path = %@"), path);
		
		if(!wi_sqlite3_execute_statement(wd_database, WI_STR("DELETE FROM `index` WHERE real_path = ?"), path, NULL))
			wi_log_error(WI_STR("Could not execute database statement: %m"));
		
		wi_lock_unlock(wd_index_lock);
	}
}



#pragma mark -

wi_boolean_t wd_index_search(wi_string_t *query, wd_user_t *user, wi_p7_message_t *message) {
	wi_sqlite3_statement_t		*statement;
	wi_dictionary_t				*results;
	wi_mutable_string_t			*string;
	wi_p7_message_t				*reply;
	wi_string_t					*accountpath, *virtualpath, *realpath;
	wd_account_t				*account;
	wd_files_privileges_t		*privileges;
	wi_fs_stat_t				sb, lsb;
	wi_file_offset_t			datasize, rsrcsize;
	wi_uinteger_t				accountpathlength, directorycount, device;
	wi_boolean_t				alias, readable, writable;
	wd_file_type_t				type;
	wd_file_label_t				label;
	
	if(wi_lock_trylock(wd_index_lock)) {
		account				= wd_user_account(user);
		accountpath			= wd_account_files(account);
		accountpathlength	= accountpath ? wi_string_length(accountpath) : 0;
		string				= wi_mutable_copy(query);
		
		if(accountpathlength == 1)
			accountpathlength--;
		
		wi_mutable_string_replace_string_with_string(string, WI_STR("\\"), WI_STR("\\\\"), 0);
		wi_mutable_string_replace_string_with_string(string, WI_STR("%"), WI_STR("\\%"), 0);
		wi_mutable_string_replace_string_with_string(string, WI_STR("_"), WI_STR("\\_"), 0);
		
		if(accountpath) {
			statement = wi_sqlite3_prepare_statement(wd_database,
													 wi_string_with_format(WI_STR("SELECT name, virtual_path, real_path, alias "
																				  "FROM `index` "
																				  "WHERE name LIKE '%%%q%%' ESCAPE '\\' "
																				  "AND virtual_path LIKE '%q%%' ESCAPE '\\'"),
																		   query,
																		   accountpath),
													 NULL);
		} else {
			statement = wi_sqlite3_prepare_statement(wd_database,
													 wi_string_with_format(WI_STR("SELECT name, virtual_path, real_path, alias "
																				  "FROM `index` "
																				  "WHERE name LIKE '%%%q%%' ESCAPE '\\'"),
																		   query),
													 NULL);
		}
		
		if(!statement) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);

			wi_lock_unlock(wd_index_lock);
			
			return false;
		}
		
		while((results = wi_sqlite3_fetch_statement_results(wd_database, statement)) && wi_dictionary_count(results) > 0) {
			virtualpath		= wi_dictionary_data_for_key(results, WI_STR("virtual_path"));
			realpath		= wi_dictionary_data_for_key(results, WI_STR("real_path"));
			alias			= wi_number_bool(wi_dictionary_data_for_key(results, WI_STR("alias")));
			
			if(!wi_fs_lstat_path(realpath, &lsb))
				continue;
			
			if(!wi_fs_stat_path(realpath, &sb))
				sb = lsb;
			
			type = wd_files_type_with_stat(realpath, &sb);
			
			switch(type) {
				case WD_FILE_TYPE_DROPBOX:
					privileges				= wd_files_drop_box_privileges(realpath);
					readable				= wd_files_privileges_is_readable_by_account(privileges, account);
					writable				= wd_files_privileges_is_writable_by_account(privileges, account);
					datasize                = 0;
					rsrcsize                = 0;
					directorycount			= readable ? wd_files_count_path(realpath, NULL, NULL) : 0;
					break;
					
				case WD_FILE_TYPE_DIR:
				case WD_FILE_TYPE_UPLOADS:
					readable				= true;
					writable				= true;
					datasize                = 0;
					rsrcsize                = 0;
					directorycount			= wd_files_count_path(realpath, NULL, NULL);
					break;
					
				case WD_FILE_TYPE_FILE:
				default:
					readable				= true;
					writable				= true;
					datasize                = sb.size;
					rsrcsize                = wi_fs_resource_fork_size_for_path(realpath);
					directorycount			= 0;
					break;
			}
			
			if(sb.dev == wd_files_root_volume)
				device = 0;
			else
				device = sb.dev;
			
			label = wd_files_label(realpath);
			
			if(accountpathlength > 0)
				virtualpath = wi_string_substring_from_index(virtualpath, accountpathlength);
			
			reply = wi_p7_message_with_name(WI_STR("wired.file.search_list"), wd_p7_spec);
			wi_p7_message_set_string_for_name(reply, virtualpath, WI_STR("wired.file.path"));
			wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
			wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
			wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
			wi_p7_message_set_bool_for_name(reply, (alias || S_ISLNK(sb.mode)), WI_STR("wired.file.link"));
			wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
			wi_p7_message_set_enum_for_name(reply, label, WI_STR("wired.file.label"));
			wi_p7_message_set_uint32_for_name(reply, device, WI_STR("wired.file.volume"));
			
			if(type == WD_FILE_TYPE_FILE) {
				wi_p7_message_set_uint64_for_name(reply, datasize, WI_STR("wired.file.data_size"));
				wi_p7_message_set_uint64_for_name(reply, rsrcsize, WI_STR("wired.file.rsrc_size"));
			} else {
				wi_p7_message_set_uint32_for_name(reply, directorycount, WI_STR("wired.file.directory_count"));
			}
			
			if(type == WD_FILE_TYPE_DROPBOX) {
				wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.file.readable"));
				wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.file.writable"));
			}
			
			wd_user_reply_message(user, reply, message);
		}
		
		if(!results) {
			wi_log_error(WI_STR("Could not execute database statement: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);

			wi_lock_unlock(wd_index_lock);
			
			return false;
		}
		
		wi_lock_unlock(wd_index_lock);
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.search_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	return true;
}
