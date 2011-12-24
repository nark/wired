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

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <wired/wired.h>

#include "files.h"
#include "index.h"
#include "main.h"
#include "messages.h"
#include "server.h"
#include "settings.h"
#include "transfers.h"

#define WD_TRANSFERS_PARTIAL_EXTENSION		"WiredTransfer"

#define WD_TRANSFER_BUFFER_SIZE				16384


enum _wd_transfers_statistics_type {
	WD_TRANSFER_STATISTICS_ADD,
	WD_TRANSFER_STATISTICS_REMOVE,
	WD_TRANSFER_STATISTICS_DATA
};
typedef enum _wd_transfers_statistics_type	wd_transfers_statistics_type_t;


static void									wd_transfers_queue_thread(wi_runtime_instance_t *);
static wi_integer_t							wd_transfers_queue_compare(wi_runtime_instance_t *, wi_runtime_instance_t *);
static wi_boolean_t							wd_transfers_wait_until_ready(wd_transfer_t *, wd_user_t *, wi_p7_message_t *);
static wi_boolean_t							wd_transfers_run_download(wd_transfer_t *, wd_user_t *, wi_p7_message_t *);
static wi_boolean_t							wd_transfers_run_upload(wd_transfer_t *, wd_user_t *, wi_p7_message_t *);
static wi_string_t *						wd_transfers_transfer_key_for_user(wd_user_t *);
static void									wd_transfers_add_or_remove_transfer(wd_transfer_t *, wi_boolean_t);
static void									wd_transfers_note_statistics(wd_transfer_type_t, wd_transfers_statistics_type_t, wi_file_offset_t);

static wd_transfer_t *						wd_transfer_alloc(void);
static wd_transfer_t *						wd_transfer_init(wd_transfer_t *);
static void									wd_transfer_dealloc(wi_runtime_instance_t *);
static wi_string_t *						wd_transfer_description(wi_runtime_instance_t *);

static inline void							wd_transfer_limit_speed(wd_transfer_t *, wi_uinteger_t, wi_uinteger_t, wi_uinteger_t, wi_uinteger_t, ssize_t, wi_time_interval_t, wi_time_interval_t);

static wi_boolean_t							wd_transfer_download(wd_transfer_t *);
static wi_boolean_t							wd_transfer_upload(wd_transfer_t *);


static wi_mutable_array_t					*wd_transfers;

static wi_uinteger_t						wd_transfers_total_downloads, wd_transfers_total_uploads;
static wi_uinteger_t						wd_transfers_total_download_speed, wd_transfers_total_upload_speed;

static wi_lock_t							*wd_transfers_status_lock;
static wi_mutable_dictionary_t				*wd_transfers_user_downloads, *wd_transfers_user_uploads;
static wi_uinteger_t						wd_transfers_active_downloads, wd_transfers_active_uploads;

static wi_condition_lock_t					*wd_transfers_queue_lock;

static wi_runtime_id_t						wd_transfer_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t					wd_transfer_runtime_class = {
	"wd_transfer_t",
	wd_transfer_dealloc,
	NULL,
	NULL,
	wd_transfer_description,
	NULL
};



void wd_transfers_initialize(void) {
	wd_transfer_runtime_id = wi_runtime_register_class(&wd_transfer_runtime_class);

	wd_transfers = wi_array_init(wi_mutable_array_alloc());

	wd_transfers_status_lock = wi_lock_init(wi_lock_alloc());
	
	wd_transfers_user_downloads = wi_dictionary_init_with_capacity_and_callbacks(wi_mutable_dictionary_alloc(),
		0, wi_dictionary_default_key_callbacks, wi_dictionary_null_value_callbacks);

	wd_transfers_user_uploads = wi_dictionary_init_with_capacity_and_callbacks(wi_mutable_dictionary_alloc(),
		0, wi_dictionary_default_key_callbacks, wi_dictionary_null_value_callbacks);
	
	wd_transfers_queue_lock = wi_condition_lock_init_with_condition(wi_condition_lock_alloc(), 0);
}



void wd_transfers_apply_settings(wi_set_t *changes) {
	wd_transfers_total_downloads		= wi_config_integer_for_name(wd_config, WI_STR("total downloads"));
	wd_transfers_total_uploads			= wi_config_integer_for_name(wd_config, WI_STR("total uploads"));
	wd_transfers_total_download_speed	= wi_config_integer_for_name(wd_config, WI_STR("total download speed"));
	wd_transfers_total_upload_speed		= wi_config_integer_for_name(wd_config, WI_STR("total upload speed"));

	wi_condition_lock_lock(wd_transfers_queue_lock);	
	wi_condition_lock_unlock_with_condition(wd_transfers_queue_lock, 1);
}



void wd_transfers_schedule(void) {
	if(!wi_thread_create_thread(wd_transfers_queue_thread, NULL))
		wi_log_fatal(WI_STR("Could not create a transfers queue thread: %m"));
}



#pragma mark -

static void wd_transfers_queue_thread(wi_runtime_instance_t *argument) {
	wi_pool_t					*pool;
	wi_enumerator_t				*enumerator;
	wi_mutable_dictionary_t		*key_queues;
	wi_mutable_array_t			*key_queue, *keys;
	wi_string_t					*key;
	wd_transfer_t				*transfer;
	wd_account_t				*account;
	wi_uinteger_t				user_downloads, user_uploads, user_transfers;
	wi_uinteger_t				new_position, position, queue, i, count, longest_queue;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	key_queues = wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	while(true) {
		wi_mutable_dictionary_remove_all_data(key_queues);

		wi_condition_lock_lock_when_condition(wd_transfers_queue_lock, 1, 0.0);
		wi_array_rdlock(wd_transfers);
		
		longest_queue	= 0;
		enumerator		= wi_array_data_enumerator(wd_transfers);
		
		while((transfer = wi_enumerator_next_data(enumerator))) {
			wi_condition_lock_lock(transfer->queue_lock);
			
			if(transfer->state == WD_TRANSFER_QUEUED && transfer->queue != 0) {
				key_queue = wi_dictionary_data_for_key(key_queues, transfer->key);
				
				if(!key_queue) {
					key_queue = wi_mutable_array();
					
					wi_mutable_dictionary_set_data_for_key(key_queues, key_queue, transfer->key);
				}
				
				wi_mutable_array_add_data(key_queue, transfer);
				
				if(wi_array_count(key_queue) > longest_queue)
					longest_queue = wi_array_count(key_queue);
			}
			
			wi_condition_lock_unlock(transfer->queue_lock);
		}
		
		keys		= wi_autorelease(wi_mutable_copy(wi_dictionary_keys_sorted_by_value(key_queues, wd_transfers_queue_compare)));
		position	= 1;
		count		= wi_array_count(keys);
		
		while(longest_queue > 0) {
			for(i = 0; i < count; i++) {
				key			= WI_ARRAY(keys, i);
				key_queue	= wi_dictionary_data_for_key(key_queues, key);
				
				if(wi_array_count(key_queue) > 0) {
					transfer	= WI_ARRAY(key_queue, 0);
					account		= wd_user_account(transfer->user);

					wi_lock_lock(wd_transfers_status_lock);
					
					if(transfer->type == WD_TRANSFER_DOWNLOAD) {
						wi_dictionary_rdlock(wd_transfers_user_downloads);
						
						user_downloads	= wd_account_transfer_download_limit(account);
						user_transfers	= (wi_integer_t) wi_dictionary_data_for_key(wd_transfers_user_downloads, transfer->key);
						queue			= ((wd_transfers_total_downloads > 0 && wd_transfers_active_downloads >= wd_transfers_total_downloads) ||
										   (user_downloads > 0 && user_transfers >= user_downloads));

						wi_dictionary_unlock(wd_transfers_user_downloads);
					} else {
						wi_dictionary_rdlock(wd_transfers_user_uploads);
						
						user_uploads	= wd_account_transfer_upload_limit(account);
						user_transfers	= (wi_integer_t) wi_dictionary_data_for_key(wd_transfers_user_uploads, transfer->key);
						queue			= ((wd_transfers_total_uploads > 0 && wd_transfers_active_uploads >= wd_transfers_total_uploads) ||
										   (user_uploads > 0 && user_transfers >= user_uploads));

						wi_dictionary_unlock(wd_transfers_user_uploads);
					}
					
					wi_lock_unlock(wd_transfers_status_lock);
					
					if(queue)
						new_position = position++;
					else
						new_position = 0;
					
					if(new_position != (wi_uinteger_t) transfer->queue) {
						if(new_position == 0)
							wd_transfers_add_or_remove_transfer(transfer, true);
						
						wi_condition_lock_lock(transfer->queue_lock);
						transfer->queue = new_position;
						wi_condition_lock_unlock_with_condition(transfer->queue_lock, 1);
					}
					
					wi_mutable_array_remove_data_at_index(key_queue, 0);
				}
			}
				
			longest_queue--;
		}
		
		wi_array_unlock(wd_transfers);
		wi_condition_lock_unlock_with_condition(wd_transfers_queue_lock, 0);
		
		wi_pool_drain(pool);
	}
	
	wi_release(key_queues);
	wi_release(pool);
}



static wi_integer_t wd_transfers_queue_compare(wi_runtime_instance_t *instance1, wi_runtime_instance_t *instance2) {
	wi_array_t			*queue1 = instance1;
	wi_array_t			*queue2 = instance2;
	wd_transfer_t		*transfer1, *transfer2;
	
	transfer1 = WI_ARRAY(queue1, 0);
	transfer2 = WI_ARRAY(queue2, 0);
	
	if(transfer1->queue_time > transfer2->queue_time)
		return 1;
	else if(transfer2->queue_time > transfer1->queue_time)
		return -1;
	
	return 0;
}



static wi_boolean_t wd_transfers_wait_until_ready(wd_transfer_t *transfer, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_uinteger_t			queue;
	wi_p7_uint32_t			transaction;
	
	while(true) {
		if(wi_condition_lock_lock_when_condition(transfer->queue_lock, 1, 1.0)) {
			queue = transfer->queue;
			
			wi_condition_lock_unlock_with_condition(transfer->queue_lock, 0);
		
			if(queue > 0) {
				reply = wi_p7_message_with_name(WI_STR("wired.transfer.queue"), wd_p7_spec);
				wi_p7_message_set_string_for_name(reply, transfer->path, WI_STR("wired.file.path"));
				wi_p7_message_set_uint32_for_name(reply, queue, WI_STR("wired.transfer.queue_position"));

				if(wi_p7_message_get_uint32_for_name(message, &transaction, WI_STR("wired.transaction")))
					wi_p7_message_set_uint32_for_name(reply, transaction, WI_STR("wired.transaction"));
				
				if(!wd_user_write_message(user, 30.0, reply)) {
					wi_log_error(WI_STR("Could not write message \"%@\" to %@: %m"),
						wi_p7_message_name(reply), wd_user_identifier(user));
					
					return false;
				}
			} 
			
			if(queue == 0 || wd_user_state(user) != WD_USER_LOGGED_IN)
				return true;
		}
	}
}



static wi_boolean_t wd_transfers_run_download(wd_transfer_t *transfer, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_data_t			*data;
	wi_p7_uint32_t		transaction;
	wi_boolean_t		result;
	
	data = wi_fs_finder_info_for_path(transfer->realdatapath);
	
	reply = wi_p7_message_with_name(WI_STR("wired.transfer.download"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, transfer->path, WI_STR("wired.file.path"));
	wi_p7_message_set_oobdata_for_name(reply, transfer->remainingdatasize, WI_STR("wired.transfer.data"));
	wi_p7_message_set_oobdata_for_name(reply, transfer->remainingrsrcsize, WI_STR("wired.transfer.rsrc"));
	wi_p7_message_set_data_for_name(reply, data ? data : wi_data(), WI_STR("wired.transfer.finderinfo"));

	if(wi_p7_message_get_uint32_for_name(message, &transaction, WI_STR("wired.transaction")))
		wi_p7_message_set_uint32_for_name(reply, transaction, WI_STR("wired.transaction"));

	if(!wd_user_write_message(user, 30.0, reply)) {
		wi_log_error(WI_STR("Could not write message \"%@\" to %@: %m"),
			wi_p7_message_name(reply), wd_user_identifier(user));

		return false;
	}
	
	wi_socket_set_interactive(wd_user_socket(user), false);
	
	result = wd_transfer_download(transfer);
	
	wi_socket_set_interactive(wd_user_socket(user), true);
	
	if(transfer->transferred == transfer->datasize + transfer->rsrcsize)
		wd_accounts_add_download_statistics(wd_user_account(user), true, transfer->actualtransferred);
	else
		wd_accounts_add_download_statistics(wd_user_account(user), false, transfer->actualtransferred);
	
	return result;
}



static wi_boolean_t wd_transfers_run_upload(wd_transfer_t *transfer, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_string_t			*path;
	wi_p7_uint32_t		transaction;
	wi_boolean_t		result;
	
	reply = wi_p7_message_with_name(WI_STR("wired.transfer.upload_ready"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, transfer->path, WI_STR("wired.file.path"));
	wi_p7_message_set_oobdata_for_name(reply, transfer->dataoffset, WI_STR("wired.transfer.data_offset"));
	wi_p7_message_set_oobdata_for_name(reply, transfer->rsrcoffset, WI_STR("wired.transfer.rsrc_offset"));
	
	if(wi_p7_message_get_uint32_for_name(message, &transaction, WI_STR("wired.transaction")))
		wi_p7_message_set_uint32_for_name(reply, transaction, WI_STR("wired.transaction"));
	
	if(!wd_user_write_message(user, 30.0, reply)) {
		wi_log_error(WI_STR("Could not write message \"%@\" to %@: %m"),
			wi_p7_message_name(reply), wd_user_identifier(user));

		return false;
	}
	
	reply = wd_user_read_message(user, 30.0);
	
	if(!reply) {
		wi_log_warn(WI_STR("Could not read message from %@ while waiting for upload: %m"),
			wd_user_identifier(user));
		
		return false;
	}
	
	if(!wi_p7_spec_verify_message(wd_p7_spec, reply)) {
		wi_log_error(WI_STR("Could not verify message from %@ while waiting for upload: %m"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.invalid_message"), reply);
		
		return false;
	}
	
	if(!wi_is_equal(wi_p7_message_name(reply), WI_STR("wired.transfer.upload"))) {
		wi_log_error(WI_STR("Could not accept message %@ from %@: Expected \"wired.transfer.upload\""),
			wi_p7_message_name(reply), wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.invalid_message"), reply);
		
		return false;
	}
	
	wi_p7_message_get_uint64_for_name(reply, &transfer->remainingdatasize, WI_STR("wired.transfer.data"));
	wi_p7_message_get_uint64_for_name(reply, &transfer->remainingrsrcsize, WI_STR("wired.transfer.rsrc"));
	
	transfer->finderinfo = wi_retain(wi_p7_message_data_for_name(reply, WI_STR("wired.transfer.finderinfo")));
	
	wi_socket_set_interactive(wd_user_socket(user), false);
	
	result = wd_transfer_upload(transfer);

	wi_socket_set_interactive(wd_user_socket(user), true);

	if(transfer->transferred == transfer->datasize + transfer->rsrcsize) {
		path = wi_string_by_deleting_path_extension(transfer->realdatapath);
		
		if(wi_fs_rename_path(transfer->realdatapath, path)) {
			if(transfer->executable) {
				if(!wi_fs_set_mode_for_path(path, 0755))
					wi_log_error(WI_STR("Could not set mode for \"%@\": %m"), path);
			}
			
			wd_files_move_comment(transfer->realdatapath, path, NULL, NULL);
			wd_files_move_label(transfer->realdatapath, path, NULL, NULL);
			
			if(wi_data_length(transfer->finderinfo) > 0)
				wi_fs_set_finder_info_for_path(transfer->finderinfo, path);
			
			wd_index_add_file(path);
		} else {
			wi_log_error(WI_STR("Could not move \"%@\" to \"%@\": %m"),
				transfer->realdatapath, path);
		}
	
		wd_accounts_add_upload_statistics(wd_user_account(user), true, transfer->actualtransferred);
	} else {
		wd_accounts_add_upload_statistics(wd_user_account(user), false, transfer->actualtransferred);
	}
	
	return result;
}



static wi_string_t * wd_transfers_transfer_key_for_user(wd_user_t *user) {
	wi_string_t		*login, *ip;
	
	login	= wd_user_login(user);
	ip		= wd_user_ip(user);
	
	if(login && ip)
		return wi_string_by_appending_string(login, ip);
	
	return NULL;
}



static void wd_transfers_add_or_remove_transfer(wd_transfer_t *transfer, wi_boolean_t add) {
	wi_mutable_dictionary_t		*dictionary;
	wi_integer_t				number;
	
	wi_lock_lock(wd_transfers_status_lock);
	
	if(transfer->type == WD_TRANSFER_DOWNLOAD)
		wd_transfers_active_downloads += add ? 1 : -1;
	else
		wd_transfers_active_uploads += add ? 1 : -1;
	
	wi_lock_unlock(wd_transfers_status_lock);

	if(transfer->type == WD_TRANSFER_DOWNLOAD)
		dictionary = wd_transfers_user_downloads;
	else
		dictionary = wd_transfers_user_uploads;
	
	wi_dictionary_wrlock(dictionary);
	
	number = (wi_integer_t) wi_dictionary_data_for_key(dictionary, transfer->key);
	
	if(add) {
		wi_mutable_dictionary_set_data_for_key(dictionary, (void *) (number + 1), transfer->key);
	} else {
		if(number > 0) {
			wi_mutable_dictionary_set_data_for_key(dictionary, (void *) (number - 1), transfer->key);
		} else {
			wi_mutable_dictionary_remove_data_for_key(dictionary, transfer->key);
		}
	}
	
	wi_dictionary_unlock(dictionary);
}



static void wd_transfers_note_statistics(wd_transfer_type_t type, wd_transfers_statistics_type_t statistics, wi_file_offset_t bytes) {
	wi_lock_lock(wd_status_lock);

	if(type == WD_TRANSFER_DOWNLOAD) {
		if(statistics == WD_TRANSFER_STATISTICS_ADD) {
			wd_current_downloads++;
			wd_total_downloads++;
		}
		else if(statistics == WD_TRANSFER_STATISTICS_REMOVE) {
			wd_current_downloads--;
		}
		
		wd_downloads_traffic += bytes;
	} else {
		if(statistics == WD_TRANSFER_STATISTICS_ADD) {
			wd_current_uploads++;
			wd_total_uploads++;
		}
		else if(statistics == WD_TRANSFER_STATISTICS_REMOVE) {
			wd_current_uploads--;
		}
		
		wd_uploads_traffic += bytes;
	}

	wd_write_status((statistics != WD_TRANSFER_STATISTICS_DATA));
	
	wi_lock_unlock(wd_status_lock);
}



#pragma mark -

wi_boolean_t wd_transfers_run_transfer(wd_transfer_t *transfer, wd_user_t *user, wi_p7_message_t *message) {
	wi_boolean_t		result = false;
	
	wi_array_wrlock(wd_transfers);
	wi_mutable_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wi_condition_lock_lock(wd_transfers_queue_lock);
	wi_condition_lock_unlock_with_condition(wd_transfers_queue_lock, 1);
	
	if(wd_transfers_wait_until_ready(transfer, user, message)) {
		wi_condition_lock_lock(transfer->queue_lock);
		transfer->state = WD_TRANSFER_RUNNING;
		wi_condition_lock_unlock(transfer->queue_lock);
		
		if(transfer->type == WD_TRANSFER_DOWNLOAD)
			result = wd_transfers_run_download(transfer, user, message);
		else
			result = wd_transfers_run_upload(transfer, user, message);
			
		wi_condition_lock_lock(transfer->finished_lock);
		wi_condition_lock_unlock_with_condition(transfer->finished_lock, 1);
	} else {
		wi_log_error(WI_STR("Could not process %@ for %@: %m"),
			(transfer->type == WD_TRANSFER_DOWNLOAD)
				? WI_STR("download")
				: WI_STR("upload"),
			wd_user_identifier(user));
	}
	
	if(transfer->queue == 0)
		wd_transfers_add_or_remove_transfer(transfer, false);

	wi_array_wrlock(wd_transfers);
	wi_mutable_array_remove_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wi_condition_lock_lock(wd_transfers_queue_lock);
	wi_condition_lock_unlock_with_condition(wd_transfers_queue_lock, 1);
	
	return result;
}



void wd_transfers_remove_user(wd_user_t *user, wi_boolean_t removingallusers) {
	wi_enumerator_t			*enumerator;
	wi_string_t				*key;
	wd_user_t				*each_user;
	wd_transfer_t			*transfer;
	wi_uinteger_t			i, count;
	wi_boolean_t			update = false, present = false;
	
	key = wd_transfers_transfer_key_for_user(user);
	
	if(!key)
		return;
	
	if(!removingallusers) {
		wi_dictionary_rdlock(wd_users);
		
		enumerator = wi_dictionary_data_enumerator(wd_users);
		
		while((each_user = wi_enumerator_next_data(enumerator))) {
			if(wd_user_state(each_user) == WD_USER_LOGGED_IN && wi_is_equal(wd_transfers_transfer_key_for_user(each_user), key)) {
				present = true;
				
				break;
			}
		}
		
		wi_dictionary_unlock(wd_users);
	}
	
	if(!present) {
		wi_array_wrlock(wd_transfers);
		
		count = wi_array_count(wd_transfers);
		
		for(i = 0; i < count; i++) {
			transfer = WI_ARRAY(wd_transfers, i);
			
			if(wi_is_equal(key, transfer->key)) {
				wd_user_set_state(transfer->user, WD_USER_DISCONNECTED);
			
				if(transfer->state == WD_TRANSFER_RUNNING) {
					if(wi_condition_lock_lock_when_condition(transfer->finished_lock, 1, 1.0))
						wi_condition_lock_unlock(transfer->finished_lock);
				} else {
					wi_mutable_array_remove_data_at_index(wd_transfers, i);
					
					i--;
					count--;
					
					update = true;
				}
			}
		}
		
		if(update) {
			wi_condition_lock_lock(wd_transfers_queue_lock);
			wi_condition_lock_unlock_with_condition(wd_transfers_queue_lock, 1);
		}

		wi_array_unlock(wd_transfers);
	}
}



wd_transfer_t * wd_transfers_transfer_with_path(wd_user_t *user, wi_string_t *path) {
	wd_transfer_t	*transfer, *value = NULL;
	wi_uinteger_t	i, count;

	wi_array_rdlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(transfer->user == user && wi_is_equal(transfer->path, path)) {
			value = wi_autorelease(wi_retain(transfer));

			break;          
		}
	}

	wi_array_unlock(wd_transfers);

	return value;
}



#pragma mark -

wd_transfer_t * wd_transfer_download_transfer(wi_string_t *path, wi_file_offset_t dataoffset, wi_file_offset_t rsrcoffset, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*realdatapath, *realrsrcpath;
	wd_transfer_t			*transfer;
	wi_fs_stat_t			sb;
	wi_file_offset_t		datasize, rsrcsize;
	int						datafd, rsrcfd;
	
	realdatapath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(wi_fs_stat_path(realdatapath, &sb))
		datasize = sb.size;
	else
		datasize = 0;
	
	datafd = open(wi_string_cstring(realdatapath), O_RDONLY, 0);
	
	if(datafd < 0) {
		wi_log_error(WI_STR("Could not open \"%@\" for download: %s"),
			realdatapath, strerror(errno));
		wd_user_reply_file_errno(user, message);

		return NULL;
	}

	if(lseek(datafd, dataoffset, SEEK_SET) < 0) {
		wi_log_error(WI_STR("Could not seek to %llu in \"%@\" for download: %s"),
			dataoffset, realdatapath, strerror(errno));
		wd_user_reply_file_errno(user, message);
		
		close(datafd);
		
		return NULL;
	}
	
	realrsrcpath = wi_fs_resource_fork_path_for_path(realdatapath);
		
	if(wd_user_supports_rsrc(user) && realrsrcpath) {
		if(wi_fs_stat_path(realrsrcpath, &sb))
			rsrcsize = sb.size;
		else
			rsrcsize = 0;
		
		rsrcfd = open(wi_string_cstring(realrsrcpath), O_RDONLY, 0);
		
		if(rsrcfd >= 0) {
			if(lseek(rsrcfd, rsrcoffset, SEEK_SET) < 0) {
				wi_log_error(WI_STR("Could not seek to %llu in \"%@\" for download: %s"),
					rsrcoffset, realrsrcpath, strerror(errno));
				wd_user_reply_file_errno(user, message);
				
				close(datafd);
				close(rsrcfd);
				
				return NULL;
			}
		}
	} else {
		rsrcfd						= -1;
		rsrcsize					= 0;
	}
	
	transfer						= wd_transfer_init(wd_transfer_alloc());
	transfer->type					= WD_TRANSFER_DOWNLOAD;
	transfer->user					= user;
	transfer->key					= wi_retain(wd_transfers_transfer_key_for_user(user));
	transfer->path					= wi_retain(path);
	transfer->realdatapath			= wi_retain(realdatapath);
	transfer->realrsrcpath			= wi_retain(realrsrcpath);
	transfer->datafd				= datafd;
	transfer->rsrcfd				= rsrcfd;
	transfer->datasize				= datasize;
	transfer->rsrcsize				= rsrcsize;
	transfer->dataoffset			= dataoffset;
	transfer->rsrcoffset			= rsrcoffset;
	transfer->transferred			= dataoffset + rsrcoffset;
	transfer->remainingdatasize		= datasize - dataoffset;
	transfer->remainingrsrcsize		= rsrcsize - rsrcoffset;
	
	return wi_autorelease(transfer);
}



wd_transfer_t * wd_transfer_upload_transfer(wi_string_t *path, wi_file_offset_t datasize, wi_file_offset_t rsrcsize, wi_boolean_t executable, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*realdatapath, *realrsrcpath;
	wd_transfer_t			*transfer;
	wi_file_offset_t		dataoffset, rsrcoffset;
	wi_fs_stat_t			sb;
	int						datafd, rsrcfd;
	
	realdatapath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(wi_fs_stat_path(realdatapath, &sb)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

		return NULL;
	}
	
	if(!wi_string_has_suffix(realdatapath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION)))
		realdatapath = wi_string_by_appending_path_extension(realdatapath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION));
	
	if(wi_fs_stat_path(realdatapath, &sb))
		dataoffset = sb.size;
	else
		dataoffset = 0;
	
	datafd = open(wi_string_cstring(realdatapath), O_WRONLY | O_APPEND | O_CREAT, 0666);
	
	if(datafd < 0) {
		wi_log_error(WI_STR("Could not open \"%@\" for upload: %s"),
			realdatapath, strerror(errno));
		wd_user_reply_file_errno(user, message);

		return NULL;
	}

	if(lseek(datafd, dataoffset, SEEK_SET) < 0) {
		wi_log_error(WI_STR("Could not seek to %llu in \"%@\" for upload: %s"),
			dataoffset, realdatapath, strerror(errno));
		wd_user_reply_file_errno(user, message);
		
		close(datafd);
		
		return NULL;
	}
	
	if(rsrcsize > 0) {
		realrsrcpath = wi_fs_resource_fork_path_for_path(realdatapath);
		
		if(!realrsrcpath) {
			wd_user_reply_error(user, WI_STR("wired.error.rsrc_not_supported"), message);
			
			close(datafd);
			
			return NULL;
		}

		if(wi_fs_stat_path(realrsrcpath, &sb))
			rsrcoffset = sb.size;
		else
			rsrcoffset = 0;
		
		rsrcfd = open(wi_string_cstring(realrsrcpath), O_WRONLY | O_APPEND | O_CREAT, 0666);
		
		if(rsrcfd < 0) {
			wi_log_error(WI_STR("Could not open \"%@\" for upload: %s"),
				realrsrcpath, strerror(errno));
			wd_user_reply_file_errno(user, message);
			
			close(datafd);

			return NULL;
		}
		
		if(lseek(rsrcfd, rsrcoffset, SEEK_SET) < 0) {
			wi_log_error(WI_STR("Could not seek to %llu in \"%@\" for upload: %s"),
				rsrcoffset, realrsrcpath, strerror(errno));
			wd_user_reply_file_errno(user, message);
			
			close(datafd);
			close(rsrcfd);
			
			return NULL;
		}
	} else {
		realrsrcpath				= NULL;
		rsrcoffset					= 0;
		rsrcfd						= -1;
	}
	
	transfer						= wd_transfer_init(wd_transfer_alloc());
	transfer->type					= WD_TRANSFER_UPLOAD;
	transfer->user					= user;
	transfer->key					= wi_retain(wd_transfers_transfer_key_for_user(user));
	transfer->path					= wi_retain(path);
	transfer->realdatapath			= wi_retain(realdatapath);
	transfer->realrsrcpath			= wi_retain(realrsrcpath);
	transfer->datafd				= datafd;
	transfer->rsrcfd				= rsrcfd;
	transfer->datasize				= datasize;
	transfer->rsrcsize				= rsrcsize;
	transfer->dataoffset			= dataoffset;
	transfer->rsrcoffset			= rsrcoffset;
	transfer->transferred			= dataoffset + rsrcoffset;
	transfer->executable			= executable;
	transfer->remainingdatasize		= datasize - dataoffset;
	transfer->remainingrsrcsize		= rsrcsize - rsrcoffset;
	
	return wi_autorelease(transfer);
}



#pragma mark -

wd_transfer_t * wd_transfer_alloc(void) {
	return wi_runtime_create_instance(wd_transfer_runtime_id, sizeof(wd_transfer_t));
}



static wd_transfer_t * wd_transfer_init(wd_transfer_t *transfer) {
	transfer->queue				= -1;
	transfer->queue_lock		= wi_condition_lock_init_with_condition(wi_condition_lock_alloc(), 0);
	transfer->queue_time		= wi_time_interval();
	transfer->state				= WD_TRANSFER_QUEUED;
	transfer->finished_lock		= wi_condition_lock_init_with_condition(wi_condition_lock_alloc(), 0);
	
	return transfer;
}



static void wd_transfer_dealloc(wi_runtime_instance_t *instance) {
	wd_transfer_t		*transfer = instance;
	
	if(transfer->datafd >= 0)
		close(transfer->datafd);
	
	if(transfer->rsrcfd >= 0)
		close(transfer->rsrcfd);

	wi_release(transfer->key);

	wi_release(transfer->path);
	wi_release(transfer->realdatapath);
	wi_release(transfer->realrsrcpath);

	wi_release(transfer->finished_lock);
	
	wi_release(transfer->queue_lock);
	
	wi_release(transfer->finderinfo);
}



static wi_string_t * wd_transfer_description(wi_runtime_instance_t *instance) {
	wd_transfer_t		*transfer = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{path = %@}"),
		wi_runtime_class_name(transfer),
		transfer,
		transfer->path);
}



#pragma mark -

static inline void wd_transfer_limit_speed(wd_transfer_t *transfer, wi_uinteger_t totalspeed, wi_uinteger_t accountspeed, wi_uinteger_t totalcount, wi_uinteger_t accountcount, ssize_t bytes, wi_time_interval_t now, wi_time_interval_t then) {
	wi_uinteger_t		limit, totallimit, accountlimit;
	wi_time_interval_t	start;
	
	if(totalspeed > 0 || accountspeed > 0) {
		totallimit		= (totalspeed > 0 && totalcount > 0) ? (float) totalspeed / (float) totalcount : 0;
		accountlimit	= (accountspeed > 0 && accountcount > 0) ? (float) accountspeed / (float) accountcount : 0;
		
		if(totallimit > 0 && accountlimit > 0)
			limit = WI_MIN(totallimit, accountlimit);
		else if(totallimit > 0)
			limit = totallimit;
		else
			limit = accountlimit;

		if(limit > 0) {
			start = now;
			
			while(transfer->speed > limit && now - start < 5.0) {
				usleep(10000);
				now += 0.01;
				
				transfer->speed = bytes / (now - then);
			}
		}
	}
}



#pragma mark -

static wi_boolean_t wd_transfer_download(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wd_account_t			*account;
	char					buffer[WD_TRANSFER_BUFFER_SIZE];
	wi_socket_state_t		state;
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_file_offset_t		sendbytes, speedbytes, statsbytes;
	wi_uinteger_t			i, transfers;
	ssize_t					readbytes;
	int						sd;
	wi_boolean_t			data, result;
	wd_user_state_t			user_state;
	
	interval				= wi_time_interval();
	speedinterval			= interval;
	statusinterval			= interval;
	accountinterval			= interval;
	speedbytes				= 0;
	statsbytes				= 0;
	i						= 0;
	socket					= wd_user_socket(transfer->user);
	sd						= wi_socket_descriptor(socket);
	p7_socket				= wd_user_p7_socket(transfer->user);
	account					= wd_user_account(transfer->user);
	data					= true;
	result					= true;
	
	wd_transfers_note_statistics(WD_TRANSFER_DOWNLOAD, WD_TRANSFER_STATISTICS_ADD, 0);
	
	wi_dictionary_rdlock(wd_transfers_user_downloads);
	
	transfers = (wi_integer_t) wi_dictionary_data_for_key(wd_transfers_user_downloads, transfer->key);
	
	wi_dictionary_unlock(wd_transfers_user_downloads);

	pool = wi_pool_init(wi_pool_alloc());
	
	wd_user_lock_socket(transfer->user);
	
	while(wd_user_state(transfer->user) == WD_USER_LOGGED_IN) {
		if(data && transfer->remainingdatasize == 0)
			data = false;
			  
		if(!data && transfer->remainingrsrcsize == 0)
			break;
		
		readbytes = read(data ? transfer->datafd : transfer->rsrcfd, buffer, sizeof(buffer));
		
		if(readbytes <= 0) {
			if(readbytes < 0) {
				wi_log_error(WI_STR("Could not read download from \"%@\": %m"),
					data ? transfer->realdatapath : transfer->realrsrcpath, strerror(errno));
			}
			
			result = false;
			break;
		}

		timeout = wi_time_interval();
		
		do {
			user_state		= wd_user_state(transfer->user);
			state			= wi_socket_wait_descriptor(sd, 0.1, false, true);
			
			if(state == WI_SOCKET_TIMEOUT) {
				if(wi_time_interval() - timeout >= 30.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && user_state == WD_USER_LOGGED_IN);

		if(state == WI_SOCKET_ERROR || wi_time_interval() - timeout >= 30.0) {
			wi_log_error(WI_STR("Could not wait for download to %@: %@"),
				wd_user_identifier(transfer->user),
				(state == WI_SOCKET_ERROR) ? wi_error_string() : WI_STR("Timed out"));
			
			result = false;
			break;
		}
		
		if(user_state != WD_USER_LOGGED_IN) {
			result = false;
			break;
		}

		if(data) {
			sendbytes = (transfer->remainingdatasize < (wi_file_offset_t) readbytes)
				? transfer->remainingdatasize
				: (wi_file_offset_t) readbytes;
		} else {
			sendbytes = (transfer->remainingrsrcsize < (wi_file_offset_t) readbytes)
				? transfer->remainingrsrcsize
				: (wi_file_offset_t) readbytes;
		}
		
		if(!wi_p7_socket_write_oobdata(p7_socket, 30.0, buffer, sendbytes)) {
			wi_log_error(WI_STR("Could not write download to %@: %m"),
				wd_user_identifier(transfer->user));
			
			result = false;
			break;
		}
		
		if(data)
			transfer->remainingdatasize		-= sendbytes;
		else
			transfer->remainingrsrcsize		-= sendbytes;
		
		interval							= wi_time_interval();
		transfer->transferred				+= sendbytes;
		transfer->actualtransferred			+= sendbytes;
		speedbytes							+= sendbytes;
		statsbytes							+= sendbytes;
		transfer->speed						= speedbytes / (interval - speedinterval);

		wd_transfer_limit_speed(transfer,
								wd_transfers_total_download_speed,
								wd_account_transfer_download_speed_limit(account),
								wd_current_downloads,
								transfers,
								speedbytes,
								interval,
								speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		if(interval - statusinterval > wd_current_downloads) {
			wd_transfers_note_statistics(WD_TRANSFER_DOWNLOAD, WD_TRANSFER_STATISTICS_DATA, statsbytes);

			statsbytes = 0;
			statusinterval = interval;
		}
		
		if(interval - accountinterval > 15.0) {
			account = wd_user_account(transfer->user);
			accountinterval = interval;

			wi_dictionary_rdlock(wd_transfers_user_downloads);
			
			transfers = (wi_integer_t) wi_dictionary_data_for_key(wd_transfers_user_downloads, transfer->key);
			
			wi_dictionary_unlock(wd_transfers_user_downloads);
		}
		
		if(++i % 1000 == 0)
			wi_pool_drain(pool);
	}
	
	wd_user_unlock_socket(transfer->user);
	
	wi_release(pool);

	wd_transfers_note_statistics(WD_TRANSFER_DOWNLOAD, WD_TRANSFER_STATISTICS_REMOVE, statsbytes);
	
	return result;
}



static wi_boolean_t wd_transfer_upload(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wd_account_t			*account;
	void					*buffer;
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_socket_state_t		state;
	ssize_t					speedbytes, statsbytes, writtenbytes;
	wi_uinteger_t			i, transfers;
	wi_integer_t			readbytes;
	int						sd;
	wi_boolean_t			data, result;
	wd_user_state_t			user_state;
	
	interval				= wi_time_interval();
	speedinterval			= interval;
	statusinterval			= interval;
	accountinterval			= interval;
	speedbytes				= 0;
	statsbytes				= 0;
	i						= 0;
	socket					= wd_user_socket(transfer->user);
	sd						= wi_socket_descriptor(socket);
	p7_socket				= wd_user_p7_socket(transfer->user);
	account					= wd_user_account(transfer->user);
	data					= true;
	result					= true;
	
	wd_transfers_note_statistics(WD_TRANSFER_UPLOAD, WD_TRANSFER_STATISTICS_ADD, 0);

	wi_dictionary_rdlock(wd_transfers_user_uploads);
	
	transfers = (wi_integer_t) wi_dictionary_data_for_key(wd_transfers_user_uploads, transfer->key);
	
	wi_dictionary_unlock(wd_transfers_user_uploads);

	pool = wi_pool_init(wi_pool_alloc());
	
	wd_user_lock_socket(transfer->user);
	
	while(wd_user_state(transfer->user) == WD_USER_LOGGED_IN) {
		if(transfer->remainingdatasize == 0)
			data = false;
		
		if(!data && transfer->remainingrsrcsize == 0)
			break;
		
		timeout = wi_time_interval();
		
		do {
			user_state		= wd_user_state(transfer->user);
			state			= wi_socket_wait_descriptor(sd, 0.1, true, false);
			
			if(state == WI_SOCKET_TIMEOUT) {
				if(wi_time_interval() - timeout >= 30.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && user_state == WD_USER_LOGGED_IN);
		
		if(state == WI_SOCKET_ERROR || wi_time_interval() - timeout >= 30.0) {
			wi_log_error(WI_STR("Could not wait for upload from %@: %@"),
				wd_user_identifier(transfer->user),
				(state == WI_SOCKET_ERROR) ? wi_error_string() : WI_STR("Timed out"));
			
			result = false;
			break;
		}
		
		if(user_state != WD_USER_LOGGED_IN) {
			result = false;
			break;
		}
		
		readbytes = wi_p7_socket_read_oobdata(p7_socket, 30.0, &buffer);

		if(readbytes <= 0) {
			if(readbytes < 0) {
				wi_log_error(WI_STR("Could not read upload from %@: %m"),
					wd_user_identifier(transfer->user));
			}

			result = false;
			break;
		}

		writtenbytes = write(data ? transfer->datafd : transfer->rsrcfd, buffer, readbytes);
		
		if(writtenbytes <= 0) {
			if(writtenbytes < 0) {
				wi_log_error(WI_STR("Could not write upload to \"%@\": %s"),
					data ? transfer->realdatapath : transfer->realrsrcpath, strerror(errno));
			}
			
			result = false;
			break;
		}

		if(data)
			transfer->remainingdatasize		-= readbytes;
		else
			transfer->remainingrsrcsize		-= readbytes;

		interval							= wi_time_interval();
		transfer->transferred				+= readbytes;
		transfer->actualtransferred			+= readbytes;
		speedbytes							+= readbytes;
		statsbytes							+= readbytes;
		transfer->speed						= speedbytes / (interval - speedinterval);

		wd_transfer_limit_speed(transfer,
								wd_transfers_total_upload_speed,
								wd_account_transfer_upload_speed_limit(account),
								wd_current_uploads,
								transfers,
								speedbytes,
								interval,
								speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		if(interval - statusinterval > wd_current_uploads) {
			wd_transfers_note_statistics(WD_TRANSFER_UPLOAD, WD_TRANSFER_STATISTICS_DATA, statsbytes);

			statsbytes = 0;
			statusinterval = interval;
		}
		
		if(interval - accountinterval > 15.0) {
			account = wd_user_account(transfer->user);
			accountinterval = interval;
			
			wi_dictionary_rdlock(wd_transfers_user_uploads);
			
			transfers = (wi_integer_t) wi_dictionary_data_for_key(wd_transfers_user_uploads, transfer->key);
			
			wi_dictionary_unlock(wd_transfers_user_uploads);
		}
		
		if(++i % 1000 == 0)
			wi_pool_drain(pool);
	}
	
	wd_user_unlock_socket(transfer->user);
	
	wi_release(pool);

	wd_transfers_note_statistics(WD_TRANSFER_UPLOAD, WD_TRANSFER_STATISTICS_REMOVE, statsbytes);
	
	return result;
}
