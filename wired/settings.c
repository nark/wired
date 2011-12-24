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

#include "files.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"
#include "transfers.h"

wi_config_t						*wd_config;



void wd_settings_initialize(void) {
	wi_dictionary_t		*types, *defaults;
	
	types = wi_dictionary_with_data_and_keys(
		WI_INT32(WI_CONFIG_STRINGLIST),			WI_STR("address"),
		WI_INT32(WI_CONFIG_PATH),				WI_STR("banner"),
		WI_INT32(WI_CONFIG_STRINGLIST),			WI_STR("category"),
		WI_INT32(WI_CONFIG_STRING),				WI_STR("description"),
		WI_INT32(WI_CONFIG_BOOL),				WI_STR("enable tracker"),
		WI_INT32(WI_CONFIG_PATH),				WI_STR("files"),
		WI_INT32(WI_CONFIG_GROUP),				WI_STR("group"),
		WI_INT32(WI_CONFIG_TIME_INTERVAL),		WI_STR("index time"),
		WI_INT32(WI_CONFIG_STRING),				WI_STR("ip"),
		WI_INT32(WI_CONFIG_BOOL),				WI_STR("map port"),
		WI_INT32(WI_CONFIG_STRING),				WI_STR("name"),
		WI_INT32(WI_CONFIG_PORT),				WI_STR("port"),
		WI_INT32(WI_CONFIG_BOOL),				WI_STR("register"),
		WI_INT32(WI_CONFIG_INTEGER),			WI_STR("total download speed"),
		WI_INT32(WI_CONFIG_INTEGER),			WI_STR("total downloads"),
		WI_INT32(WI_CONFIG_INTEGER),			WI_STR("total upload speed"),
		WI_INT32(WI_CONFIG_INTEGER),			WI_STR("total uploads"),
		WI_INT32(WI_CONFIG_STRINGLIST),			WI_STR("tracker"),
		WI_INT32(WI_CONFIG_USER),				WI_STR("user"),
		NULL);
	
	defaults = wi_dictionary_with_data_and_keys(
		wi_array(),								WI_STR("address"),
		WI_STR("banner.png"),					WI_STR("banner"),
		wi_array(),								WI_STR("category"),
		WI_STR("Wired Server"),					WI_STR("description"),
		wi_number_with_bool(false),				WI_STR("enable tracker"),
		WI_STR("files"),						WI_STR("files"),
		WI_STR("daemon"),						WI_STR("group"),
		WI_INT32(14400),						WI_STR("index time"),
		wi_number_with_bool(false),				WI_STR("map port"),
		WI_STR("Wired Server"),					WI_STR("name"),
		WI_INT32(4871),							WI_STR("port"),
		wi_number_with_bool(false),				WI_STR("register"),
		WI_INT32(0),							WI_STR("total download speed"),
		WI_INT32(10),							WI_STR("total downloads"),
		WI_INT32(0),							WI_STR("total upload speed"),
		WI_INT32(10),							WI_STR("total uploads"),
		wi_array(),								WI_STR("tracker"),
		WI_STR("wired"),						WI_STR("user"),
		NULL);
	
	wd_config = wi_config_init_with_path(wi_config_alloc(), wd_config_path, types, defaults);
}



wi_boolean_t wd_settings_read_config(void) {
	wi_boolean_t	result;
	
	result = wi_config_read_file(wd_config);
	
	if(result) {
		wd_settings_apply_settings(wi_config_changes(wd_config));
		
		wi_config_clear_changes(wd_config);
	}
	
	return result;
}



void wd_settings_apply_settings(wi_set_t *changes) {
	wd_files_apply_settings(changes);
	wd_server_apply_settings(changes);
	wd_trackers_apply_settings(changes);
	wd_transfers_apply_settings(changes);
}



#pragma mark -

void wd_settings_reply_settings(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_string_t			*name, *description;
	wi_array_t			*trackers, *categories;
	wi_integer_t		total_downloads, total_uploads, total_download_speed, total_upload_speed;
	wi_boolean_t		register_with_trackers, tracker;
	
	name					= wi_config_string_for_name(wd_config, WI_STR("name"));
	description				= wi_config_string_for_name(wd_config, WI_STR("description"));
	total_downloads			= wi_config_integer_for_name(wd_config, WI_STR("total downloads"));
	total_download_speed	= wi_config_integer_for_name(wd_config, WI_STR("total download speed"));
	total_uploads			= wi_config_integer_for_name(wd_config, WI_STR("total uploads"));
	total_upload_speed		= wi_config_integer_for_name(wd_config, WI_STR("total upload speed"));
	register_with_trackers	= wi_config_bool_for_name(wd_config, WI_STR("register"));
	trackers				= wi_config_stringlist_for_name(wd_config, WI_STR("tracker"));
	tracker					= wi_config_bool_for_name(wd_config, WI_STR("enable tracker"));
	categories				= wi_config_stringlist_for_name(wd_config, WI_STR("category"));
	
	reply = wi_p7_message_with_name(WI_STR("wired.settings.settings"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, name, WI_STR("wired.info.name"));
	wi_p7_message_set_string_for_name(reply, description, WI_STR("wired.info.description"));
	wi_p7_message_set_data_for_name(reply, wd_banner, WI_STR("wired.info.banner"));
	wi_p7_message_set_uint32_for_name(reply, total_downloads, WI_STR("wired.info.downloads"));
	wi_p7_message_set_uint32_for_name(reply, total_uploads, WI_STR("wired.info.uploads"));
	wi_p7_message_set_uint32_for_name(reply, total_download_speed, WI_STR("wired.info.download_speed"));
	wi_p7_message_set_uint32_for_name(reply, total_upload_speed, WI_STR("wired.info.upload_speed"));
	wi_p7_message_set_bool_for_name(reply, register_with_trackers, WI_STR("wired.settings.register_with_trackers"));
	wi_p7_message_set_list_for_name(reply, trackers, WI_STR("wired.settings.trackers"));
	wi_p7_message_set_bool_for_name(reply, tracker, WI_STR("wired.tracker.tracker"));
	wi_p7_message_set_list_for_name(reply, categories, WI_STR("wired.tracker.categories"));
	wd_user_reply_message(user, reply, message);
}



wi_boolean_t wd_settings_set_settings(wd_user_t *user, wi_p7_message_t *message) {
	wi_data_t		*banner;
	wi_string_t		*path, *name, *description;
	wi_number_t		*total_downloads, *total_uploads, *total_download_speed, *total_upload_speed;
	wi_number_t		*register_with_trackers, *tracker;
	wi_array_t		*trackers, *categories;
	
	banner = wi_p7_message_data_for_name(message, WI_STR("wired.info.banner"));
	path = wi_config_path_for_name(wd_config, WI_STR("banner"));
	
	if(!path || !wi_fs_path_exists(path, NULL)) {
		path = WI_STR("banner.png");
		
		wi_config_set_instance_for_name(wd_config, path, WI_STR("banner"));
	}
	
	if(wi_data_write_to_file(banner, path))
		wi_config_note_change(wd_config, WI_STR("banner"));
	else
		wi_log_error(WI_STR("Could not write banner to \"%@\": %m"), path);
		
	name					= wi_p7_message_string_for_name(message, WI_STR("wired.info.name"));
	description				= wi_p7_message_string_for_name(message, WI_STR("wired.info.description"));
	total_downloads			= wi_p7_message_number_for_name(message, WI_STR("wired.info.downloads"));
	total_download_speed	= wi_p7_message_number_for_name(message, WI_STR("wired.info.download_speed"));
	total_uploads			= wi_p7_message_number_for_name(message, WI_STR("wired.info.uploads"));
	total_upload_speed		= wi_p7_message_number_for_name(message, WI_STR("wired.info.upload_speed"));
	register_with_trackers	= wi_p7_message_number_for_name(message, WI_STR("wired.settings.register_with_trackers"));
	trackers				= wi_p7_message_list_for_name(message, WI_STR("wired.settings.trackers"));
	tracker					= wi_p7_message_number_for_name(message, WI_STR("wired.tracker.tracker"));
	categories				= wi_p7_message_list_for_name(message, WI_STR("wired.tracker.categories"));

	wi_config_set_instance_for_name(wd_config, name, WI_STR("name"));
	wi_config_set_instance_for_name(wd_config, description, WI_STR("description"));
	wi_config_set_instance_for_name(wd_config, total_downloads, WI_STR("total downloads"));
	wi_config_set_instance_for_name(wd_config, total_download_speed, WI_STR("total download speed"));
	wi_config_set_instance_for_name(wd_config, total_uploads, WI_STR("total uploads"));
	wi_config_set_instance_for_name(wd_config, total_upload_speed, WI_STR("total upload speed"));
	wi_config_set_instance_for_name(wd_config, register_with_trackers, WI_STR("register"));
	wi_config_set_instance_for_name(wd_config, trackers, WI_STR("tracker"));
	wi_config_set_instance_for_name(wd_config, tracker, WI_STR("enable tracker"));
	wi_config_set_instance_for_name(wd_config, categories, WI_STR("category"));
	
	wd_settings_apply_settings(wi_config_changes(wd_config));
	
	if(!wi_config_write_file(wd_config)) {
		wi_log_error(WI_STR("Could not write config: %m"));

		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return false;
	}
	
	return true;
}
