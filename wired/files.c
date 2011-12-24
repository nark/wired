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

#include <string.h>
#include <errno.h>
#include <wired/wired.h>

#include "accounts.h"
#include "events.h"
#include "files.h"
#include "index.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"
#include "transfers.h"
#include "users.h"

#define WD_FILES_META_TYPE_PATH							".wired/type"
#define WD_FILES_META_COMMENTS_PATH						".wired/comments"
#define WD_FILES_META_PERMISSIONS_PATH					".wired/permissions"
#define WD_FILES_META_LABELS_PATH						".wired/labels"

#define WD_FILES_PERMISSIONS_FIELD_SEPARATOR			"\34"

#define WD_FILES_OLDSTYLE_COMMENT_FIELD_SEPARATOR		"\34"
#define WD_FILES_OLDSTYLE_COMMENT_SEPARATOR				"\35"


struct _wd_files_privileges {
	wi_runtime_base_t									base;
	
	wi_string_t											*owner;
	wi_string_t											*group;
	wi_uinteger_t										mode;
};


static void												wd_files_delete_path_callback(wi_string_t *);
static void												wd_files_move_path_copy_callback(wi_string_t *, wi_string_t *);
static void												wd_files_move_path_delete_callback(wi_string_t *);
static void												wd_files_move_thread(wi_runtime_instance_t *);

static void												wd_files_fsevents_thread(wi_runtime_instance_t *);
static void												wd_files_fsevents_callback(wi_string_t *);

static wi_string_t *									wd_files_comment(wi_string_t *);

static wi_string_t *									wd_files_drop_box_path_in_path(wi_string_t *, wd_user_t *);

static wd_files_privileges_t *							wd_files_privileges_alloc(void);
static wi_string_t *									wd_files_privileges_description(wi_runtime_instance_t *instance);
static void												wd_files_privileges_dealloc(wi_runtime_instance_t *);

static wd_files_privileges_t *							wd_files_privileges_with_string(wi_string_t *);
static wd_files_privileges_t *							wd_files_privileges_default_drop_box_privileges(void);

static wi_string_t *									wd_files_privileges_string(wd_files_privileges_t *);


static wi_runtime_id_t									wd_files_privileges_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t								wd_files_privileges_runtime_class = {
	"wd_files_privileges_t",
	wd_files_privileges_dealloc,
	NULL,
	NULL,
	wd_files_privileges_description,
	NULL
};

wi_string_t												*wd_files;
wi_uinteger_t											wd_files_root_volume;
wi_fsevents_t											*wd_files_fsevents;



void wd_files_initialize(void) {
	wd_files_fsevents = wi_fsevents_init(wi_fsevents_alloc());
	
	if(wd_files_fsevents)
		wi_fsevents_set_callback(wd_files_fsevents, wd_files_fsevents_callback);
	else
		wi_log_warn(WI_STR("Could not create fsevents: %m"));

	wd_files_privileges_runtime_id = wi_runtime_register_class(&wd_files_privileges_runtime_class);
}



void wd_files_apply_settings(wi_set_t *changes) {
	wi_string_t			*realpath;
	wi_fs_stat_t		sb;
	
	wi_release(wd_files);
	wd_files = wi_retain(wi_config_path_for_name(wd_config, WI_STR("files")));
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files);
	
	if(wi_fs_stat_path(realpath, &sb))
		wd_files_root_volume = sb.dev;
}



void wd_files_schedule(void) {
	if(wd_files_fsevents) {
		if(!wi_thread_create_thread(wd_files_fsevents_thread, NULL))
			wi_log_error(WI_STR("Could not create an fsevents thread: %m"));
	}
}



#pragma mark -

wi_boolean_t wd_files_reply_list(wi_string_t *path, wi_boolean_t recursive, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t				*reply;
	wi_string_t					*realpath, *filepath, *resolvedpath, *virtualpath;
	wi_fsenumerator_t			*fsenumerator;
	wd_account_t				*account;
	wd_files_privileges_t		*privileges;
	wi_fs_statfs_t				sfb;
	wi_fs_stat_t				dsb, sb, lsb;
	wi_fsenumerator_status_t	status;
	wi_file_offset_t			datasize, rsrcsize;
	wd_file_label_t				label;
	wi_uinteger_t				pathlength, depthlimit, directorycount;
	wd_file_type_t				type, pathtype;
	wi_boolean_t				root, upload, alias, readable, writable;
	uint32_t					device;
	
	root		= wi_is_equal(path, WI_STR("/"));
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	account		= wd_user_account(user);
	pathtype	= wd_files_type(realpath);
	
	if(!wi_fs_stat_path(realpath, &dsb)) {
		wi_log_error(WI_STR("Could not read info for \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	depthlimit		= wd_account_file_recursive_list_depth_limit(account);
	fsenumerator	= wi_fs_enumerator_at_path(realpath);
	
	if(!fsenumerator) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}

	pathlength = wi_string_length(realpath);
	
	if(pathlength == 1)
		pathlength--;
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &filepath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_error(WI_STR("Could not list \"%@\": %m"), filepath);
			
			continue;
		}
	
		if(depthlimit > 0 && wi_fsenumerator_level(fsenumerator) > depthlimit) {
			wi_fsenumerator_skip_descendents(fsenumerator);
			
			continue;
		}
		
		if(wi_fs_path_is_invisible(filepath)) {
			wi_fsenumerator_skip_descendents(fsenumerator);
			
			continue;
		}
		
		if(!recursive)
			wi_fsenumerator_skip_descendents(fsenumerator);
		
		virtualpath = wi_string_substring_from_index(filepath, pathlength);
		
		if(!root)
			virtualpath = wi_string_by_inserting_string_at_index(virtualpath, path, 0);
		
		alias = wi_fs_path_is_alias(filepath);
		
		if(alias)
			resolvedpath = wi_string_by_resolving_aliases_in_path(filepath);
		else
			resolvedpath = filepath;

		if(!wi_fs_lstat_path(resolvedpath, &lsb)) {
			wi_log_error(WI_STR("Could not read info for \"%@\": %m"), resolvedpath);

			continue;
		}

		if(!wi_fs_stat_path(resolvedpath, &sb))
			sb = lsb;

		readable	= false;
		writable	= false;
		type		= wd_files_type_with_stat(resolvedpath, &sb);
		
		if(type == WD_FILE_TYPE_DROPBOX) {
			privileges	= wd_files_drop_box_privileges(resolvedpath);
			readable	= wd_files_privileges_is_readable_by_account(privileges, account);
			writable	= wd_files_privileges_is_writable_by_account(privileges, account);
		}

		switch(type) {
			case WD_FILE_TYPE_DIR:
			case WD_FILE_TYPE_UPLOADS:
				datasize		= 0;
				rsrcsize		= 0;
				directorycount	= wd_files_count_path(resolvedpath, user, message);
				break;
				
			case WD_FILE_TYPE_DROPBOX:
				datasize		= 0;
				rsrcsize		= 0;
				directorycount	= readable ? wd_files_count_path(resolvedpath, user, message) : 0;
				break;

			case WD_FILE_TYPE_FILE:
			default:
				datasize		= sb.size;
				rsrcsize		= wi_fs_resource_fork_size_for_path(resolvedpath);
				directorycount	= 0;
				break;
		}
		
		label = wd_files_label(filepath);
		
		device = alias ? dsb.dev : sb.dev;
		
		if(device == wd_files_root_volume)
			device = 0;
		
		reply = wi_p7_message_init_with_name(wi_p7_message_alloc(), WI_STR("wired.file.file_list"), wd_p7_spec);
		wi_p7_message_set_string_for_name(reply, virtualpath, WI_STR("wired.file.path"));
		
		if(type == WD_FILE_TYPE_FILE) {
			wi_p7_message_set_uint64_for_name(reply, datasize, WI_STR("wired.file.data_size"));
			wi_p7_message_set_uint64_for_name(reply, rsrcsize, WI_STR("wired.file.rsrc_size"));
		} else {
			wi_p7_message_set_uint32_for_name(reply, directorycount, WI_STR("wired.file.directory_count"));
		}
		
		wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
		wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
		wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
		wi_p7_message_set_bool_for_name(reply, alias || S_ISLNK(lsb.mode), WI_STR("wired.file.link"));
		wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
		wi_p7_message_set_enum_for_name(reply, label, WI_STR("wired.file.label"));
		wi_p7_message_set_uint32_for_name(reply, device, WI_STR("wired.file.volume"));
		
		if(type == WD_FILE_TYPE_DROPBOX) {
			wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.file.readable"));
			wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.file.writable"));
		}
		
		wd_user_reply_message(user, reply, message);
		wi_release(reply);
		
		if(recursive && (type == WD_FILE_TYPE_DROPBOX && !readable)) {
			wi_fsenumerator_skip_descendents(fsenumerator);
				
			continue;
		}
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.file_list.done"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	
	if(pathtype == WD_FILE_TYPE_DROPBOX) {
		privileges	= wd_files_drop_box_privileges(realpath);
		readable	= wd_files_privileges_is_readable_by_account(privileges, account);
		writable	= wd_files_privileges_is_writable_by_account(privileges, account);
		
		wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.file.readable"));
		wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.file.writable"));
	} else {
		readable	= false;
		writable	= false;
	}
	
	if(wd_account_transfer_upload_anywhere(account))
		upload = true;
	else if(pathtype == WD_FILE_TYPE_DROPBOX)
		upload = writable;
	else if(pathtype == WD_FILE_TYPE_UPLOADS)
		upload = wd_account_transfer_upload_files(account);
	else
		upload = false;

	if(upload && wi_fs_statfs_path(realpath, &sfb))
		wi_p7_message_set_uint64_for_name(reply, (wi_file_offset_t) sfb.bavail * (wi_file_offset_t) sfb.frsize, WI_STR("wired.file.available"));
	else
		wi_p7_message_set_uint64_for_name(reply, 0, WI_STR("wired.file.available"));
	
	wd_user_reply_message(user, reply, message);
	
	return true;
}



wi_file_offset_t wd_files_count_path(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_mutable_string_t		*filepath;
	DIR						*dir;
	struct dirent			*de, *dep;
	wi_file_offset_t		count = 0;
	
	dir = opendir(wi_string_cstring(path));
	
	if(dir) {
		filepath	= wi_mutable_copy(path);
		de			= wi_malloc(sizeof(struct dirent) + WI_PATH_SIZE);
		
		wi_mutable_string_append_cstring(filepath, "/");
		
		while(readdir_r(dir, de, &dep) == 0 && dep) {
			if(dep->d_name[0] != '.') {
				wi_mutable_string_append_cstring(filepath, dep->d_name);
				
				if(!wi_fs_path_is_invisible(filepath))
					count++;
				
				wi_mutable_string_delete_characters_from_index(filepath, wi_string_length(filepath) - strlen(dep->d_name));
			}
		}
		
		wi_release(filepath);

		wi_free(de);

		closedir(dir);
		
		return count;
	} else {
		wi_log_error(WI_STR("Could not open \"%@\": %s"),
			path, strerror(errno));
		
		if(user)
			wd_user_reply_file_errno(user, message);
		
		return 0;
	}
}




wi_boolean_t wd_files_reply_info(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_string_t				*realpath, *parentpath, *comment;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges = NULL;
	wi_file_offset_t		datasize, rsrcsize;
	wd_file_type_t			type;
	wi_fs_stat_t			dsb, sb, lsb;
	wd_file_label_t			label;
	wi_uinteger_t			directorycount;
	wi_boolean_t			alias, readable, writable;
	uint32_t				device;
	
	account			= wd_user_account(user);
	realpath		= wd_files_real_path(path, user);
	alias			= wi_fs_path_is_alias(realpath);
	realpath		= wi_string_by_resolving_aliases_in_path(realpath);
	parentpath		= wi_string_by_deleting_last_path_component(realpath);

	if(!wi_fs_stat_path(parentpath, &dsb)) {
		wi_log_error(WI_STR("Could not read info for \"%@\": %m"), parentpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	if(!wi_fs_lstat_path(realpath, &lsb)) {
		wi_log_error(WI_STR("Could not read info for \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	if(!wi_fs_stat_path(realpath, &sb))
		sb = lsb;

	readable	= false;
	writable	= false;
	type		= wd_files_type_with_stat(realpath, &sb);
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		privileges	= wd_files_drop_box_privileges(realpath);
		readable	= wd_files_privileges_is_readable_by_account(privileges, account);
		writable	= wd_files_privileges_is_writable_by_account(privileges, account);
	}
	
	comment = wd_files_comment(realpath);
	
	switch(type) {
		case WD_FILE_TYPE_DIR:
		case WD_FILE_TYPE_UPLOADS:
			datasize		= 0;
			rsrcsize		= 0;
			directorycount	= wd_files_count_path(realpath, user, message);
			break;
			
		case WD_FILE_TYPE_DROPBOX:
			datasize		= 0;
			rsrcsize		= 0;
			directorycount	= readable ? wd_files_count_path(realpath, user, message) : 0;
			break;

		case WD_FILE_TYPE_FILE:
		default:
			datasize		= sb.size;
			rsrcsize		= wi_fs_resource_fork_size_for_path(realpath);
			directorycount	= 0;
			break;
	}

	label = wd_files_label(realpath);
	
	device = alias ? dsb.dev : sb.dev;
	
	if(device == wd_files_root_volume)
		device = 0;
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.info"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
	
	if(type == WD_FILE_TYPE_FILE) {
		wi_p7_message_set_uint64_for_name(reply, datasize, WI_STR("wired.file.data_size"));
		wi_p7_message_set_uint64_for_name(reply, rsrcsize, WI_STR("wired.file.rsrc_size"));
	} else {
		wi_p7_message_set_uint32_for_name(reply, directorycount, WI_STR("wired.file.directory_count"));
	}
	
	wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
	wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
	wi_p7_message_set_string_for_name(reply, comment, WI_STR("wired.file.comment"));
	wi_p7_message_set_bool_for_name(reply, (alias || S_ISLNK(lsb.mode)), WI_STR("wired.file.link"));
	wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
	wi_p7_message_set_enum_for_name(reply, label, WI_STR("wired.file.label"));
	wi_p7_message_set_uint32_for_name(reply, device, WI_STR("wired.file.volume"));
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		wi_p7_message_set_string_for_name(reply, privileges->owner, WI_STR("wired.file.owner"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_OWNER_WRITE), WI_STR("wired.file.owner.write"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_OWNER_READ), WI_STR("wired.file.owner.read"));
		wi_p7_message_set_string_for_name(reply, privileges->group, WI_STR("wired.file.group"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_GROUP_WRITE), WI_STR("wired.file.group.write"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_GROUP_READ), WI_STR("wired.file.group.read"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_EVERYONE_WRITE), WI_STR("wired.file.everyone.write"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_EVERYONE_READ), WI_STR("wired.file.everyone.read"));
		wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.file.readable"));
		wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.file.writable"));
	}
	
	wd_user_reply_message(user, reply, message);
	
	return true;
}



wi_boolean_t wd_files_reply_preview(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_string_t				*realpath;
	wi_data_t				*data;
	wi_fs_stat_t			sb;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(!wi_fs_stat_path(realpath, &sb)) {
		wi_log_error(WI_STR("Could not preview \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	if(sb.size > (10 * 1024 * 1024) - (10 * 1024)) {
		wi_log_error(WI_STR("Could not preview \"%@\": Too large"), realpath);
		wd_user_reply_internal_error(user, WI_STR("File too large to preview"), message);
		
		return false;
	}
	
	data = wi_data_with_contents_of_file(realpath);
	
	if(!data) {
		wi_log_error(WI_STR("Could not preview \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.preview"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	wi_p7_message_set_data_for_name(reply, data, WI_STR("wired.file.preview"));
	
	wd_user_reply_message(user, reply, message);
	
	return true;
}



wi_boolean_t wd_files_create_path(wi_string_t *path, wd_file_type_t type, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_create_directory(realpath, 0777)) {
		wi_log_error(WI_STR("Could not create \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(type != WD_FILE_TYPE_DIR)
		wd_files_set_type(path, type, user, message);
	
	wd_index_add_file(realpath);
	
	return true;
}



wi_boolean_t wd_files_delete_path(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_mutable_string_t		*realpath;
	wi_string_t				*component;
	wi_boolean_t			result;
	
	realpath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(path, user)));
	component	= wi_string_last_path_component(realpath);

	wi_mutable_string_delete_last_path_component(realpath);
	wi_mutable_string_resolve_aliases_in_path(realpath);
	wi_mutable_string_append_path_component(realpath, component);
	
	result = wi_fs_delete_path_with_callback(realpath, wd_files_delete_path_callback);
	
	if(result) {
		wd_files_remove_comment(path, NULL, NULL);
		wd_files_remove_label(path, NULL, NULL);
	} else {
		wi_log_error(WI_STR("Could not delete \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
	}
	
	return result;
}



static void wd_files_delete_path_callback(wi_string_t *path) {
	wd_index_delete_file(path);
}



wi_boolean_t wd_files_move_path(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_array_t				*array;
	wi_mutable_string_t		*realfrompath, *realtopath;
	wi_string_t				*realfromname, *realtoname;
	wi_string_t				*path;
	wi_fs_stat_t			sb;
	wi_boolean_t			result = false;
	
	realfrompath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(frompath, user)));
	realtopath		= wi_autorelease(wi_mutable_copy(wd_files_real_path(topath, user)));
	realfromname	= wi_string_last_path_component(realfrompath);
	realtoname		= wi_string_last_path_component(realtopath);

	wi_mutable_string_delete_last_path_component(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realfrompath);
	wi_mutable_string_append_path_component(realfrompath, realfromname);

	wi_mutable_string_delete_last_path_component(realtopath);
	wi_mutable_string_resolve_aliases_in_path(realtopath);
	wi_mutable_string_append_path_component(realtopath, realtoname);
	
	if(!wi_fs_lstat_path(realfrompath, &sb)) {
		wi_log_error(WI_STR("Could not read info for \"%@\": %m"), realfrompath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(wi_string_case_insensitive_compare(realfrompath, realtopath) == 0) {
		path = wi_fs_temporary_path_with_template(
			wi_string_with_format(WI_STR("%@/.%@.XXXXXXXX"),
				  wi_string_by_deleting_last_path_component(realfrompath),
				  wi_string_last_path_component(realfromname)));
		
		if(path) {
			result = wi_fs_rename_path(realfrompath, path);
		
			if(result)
				result = wi_fs_rename_path(path, realtopath);
		}
	} else {
		if(wi_fs_lstat_path(realtopath, &sb)) {
			wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

			return false;
		}
		
		result = wi_fs_rename_path(realfrompath, realtopath);
	}
	
	if(result) {
		wd_files_move_comment(frompath, topath, user, message);
		wd_files_move_label(frompath, topath, user, message);
		
		wd_index_delete_file(realfrompath);
		wd_index_add_file(realtopath);
	} else {
		if(wi_error_code() == EXDEV) {
			array = wi_array_init_with_data(wi_array_alloc(),
				frompath,
				topath,
				realfrompath,
				realtopath,
				(void *) NULL);
			
			result = wi_thread_create_thread(wd_files_move_thread, array);
			
			if(!result) {
				wi_log_error(WI_STR("Could not create a copy thread: %m"));
				wd_user_reply_internal_error(user, wi_error_string(), message);
			}
			
			wi_release(array);
		} else {
			wi_log_error(WI_STR("Could not rename \"%@\" to \"%@\": %m"),
				realfrompath, realtopath);
			wd_user_reply_file_errno(user, message);
		}
	}
	
	return result;
}



static void wd_files_move_thread(wi_runtime_instance_t *argument) {
	wi_pool_t		*pool;
	wi_array_t		*array = argument;
	wi_string_t		*frompath, *topath, *realfrompath, *realtopath;
	
	pool			= wi_pool_init(wi_pool_alloc());
	frompath		= WI_ARRAY(array, 0);
	topath			= WI_ARRAY(array, 1);
	realfrompath	= WI_ARRAY(array, 2);
	realtopath		= WI_ARRAY(array, 3);
	
	if(wi_fs_copy_path_with_callback(realfrompath, realtopath, wd_files_move_path_copy_callback)) {
		wd_files_move_comment(frompath, topath, NULL, NULL);
		wd_files_move_label(frompath, topath, NULL, NULL);
		
		if(!wi_fs_delete_path_with_callback(realfrompath, wd_files_move_path_delete_callback))
			wi_log_error(WI_STR("Could not delete \"%@\": %m"), realfrompath);
	} else {
		wi_log_error(WI_STR("Could not copy \"%@\" to \"%@\": %m"), realfrompath, realtopath);
	}
	
	wi_release(pool);
}



static void wd_files_move_path_copy_callback(wi_string_t *frompath, wi_string_t *topath) {
	wd_index_add_file(topath);
}



static void wd_files_move_path_delete_callback(wi_string_t *path) {
	wd_index_delete_file(path);
}



wi_boolean_t wd_files_link_path(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_mutable_string_t		*realfrompath, *realtopath;
	wi_string_t				*realfromname, *realpath;
	wi_fs_stat_t			sb;
	
	realfrompath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(frompath, user)));
	realtopath		= wi_autorelease(wi_mutable_copy(wd_files_real_path(topath, user)));
	realfromname	= wi_string_last_path_component(realfrompath);

	wi_mutable_string_delete_last_path_component(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realtopath);
	wi_mutable_string_append_path_component(realfrompath, realfromname);
	
	if(!wi_fs_lstat_path(realfrompath, &sb)) {
		wi_log_error(WI_STR("Could not read info for \"%@\": %m"), realfrompath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(wi_fs_lstat_path(realtopath, &sb)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

		return false;
	}
	
	realpath = wi_fs_real_path_for_path(realfrompath);
	
	if(!realpath) {
		wi_log_error(WI_STR("Could not get real path for \"%@\": %m"), realfrompath);
		wd_user_reply_file_errno(user, message);

		return false;
	}
	
	if(!wi_fs_symlink_path(realpath, realtopath)) {
		wi_log_error(WI_STR("Could not symlink \"%@\" to \"%@\": %m"), realpath, realtopath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	wd_index_add_file(realtopath);
	
	return true;
}



#pragma mark -

#pragma mark -

static void wd_files_fsevents_thread(wi_runtime_instance_t *instance) {
	wi_pool_t		*pool;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	while(true) {
		if(!wi_fsevents_run_with_timeout(wd_files_fsevents, 0.0))
			wi_log_error(WI_STR("Could not listen on fsevents: %m"));
		
		wi_pool_drain(pool);
	}
	
	wi_release(pool);
}



static void wd_files_fsevents_callback(wi_string_t *path) {
	wi_pool_t			*pool;
	wi_enumerator_t		*enumerator, *pathenumerator;
	wi_p7_message_t		*message;
	wi_string_t			*virtualpath;
	wd_user_t			*user;
	wi_boolean_t		exists, directory;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	wi_retain(path);
	
	exists = (wi_fs_path_exists(path, &directory) && directory);
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN && wi_set_contains_data(wd_user_subscribed_paths(user), path)) {
			pathenumerator = wi_array_data_enumerator(wd_user_subscribed_virtual_paths_for_path(user, path));
			
			while((virtualpath = wi_enumerator_next_data(pathenumerator))) {
				if(exists)
					message = wi_p7_message_with_name(WI_STR("wired.file.directory_changed"), wd_p7_spec);
				else
					message = wi_p7_message_with_name(WI_STR("wired.file.directory_deleted"), wd_p7_spec);
				
				wi_p7_message_set_string_for_name(message, virtualpath, WI_STR("wired.file.path"));
				wd_user_send_message(user, message);
			}
			
			if(!exists)
				wd_user_unsubscribe_path(user, path);
		}
	}

	wi_dictionary_unlock(wd_users);
	
	wi_release(path);
	wi_release(pool);
}



#pragma mark -

wi_boolean_t wd_files_set_type(wi_string_t *path, wd_file_type_t type, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *metapath, *typepath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	typepath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(type != WD_FILE_TYPE_DIR) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_error(WI_STR("Could not create \"%@\": %m"), metapath);
				wd_user_reply_file_errno(user, message);
				
				return false;
			}
		}
		
		if(!wi_string_write_to_file(wi_string_with_format(WI_STR("%u\n"), type), typepath)) {
			wi_log_error(WI_STR("Could not write to \"%@\": %m"), typepath);
			wd_user_reply_file_errno(user, message);
			
			return false;
		}
	} else {
		if(!wi_fs_delete_path(typepath)) {
			wi_log_error(WI_STR("Could not delete \"%@\": %m"), typepath);
			wd_user_reply_file_errno(user, message);
			
			return false;
		}
	}
	
	return true;
}



wd_file_type_t wd_files_type(wi_string_t *path) {
	wi_fs_stat_t	sb;
	
	if(!wi_fs_stat_path(path, &sb))
		return WD_FILE_TYPE_FILE;
	
	return wd_files_type_with_stat(path, &sb);
}



wd_file_type_t wd_files_type_with_stat(wi_string_t *realpath, wi_fs_stat_t *sbp) {
	wi_string_t		*typepath, *string;
	wi_fs_stat_t	sb;
	wd_file_type_t	type;
	
	if(!S_ISDIR(sbp->mode))
		return WD_FILE_TYPE_FILE;
	
	typepath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(!wi_fs_stat_path(typepath, &sb) || sb.size > 8)
		return WD_FILE_TYPE_DIR;
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), typepath));
	
	if(!string)
		return WD_FILE_TYPE_DIR;
	
	type = wi_string_uint32(wi_string_by_deleting_surrounding_whitespace(string));
	
	if(type == WD_FILE_TYPE_FILE)
		type = WD_FILE_TYPE_DIR;
	
	return type;
}



#pragma mark -

wi_boolean_t wd_files_set_executable(wi_string_t *path, wi_boolean_t executable, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_set_mode_for_path(realpath, executable ? 0755 : 0644)) {
		wi_log_error(WI_STR("Could not set mode for \"%@\": %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	return true;
}



#pragma mark -

static wi_string_t * wd_files_comment(wi_string_t *path) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	return wi_fs_finder_comment_for_path(path);
#else
	wi_runtime_instance_t	*instance;
	wi_file_t				*file;
	wi_array_t				*array;
	wi_string_t				*name, *dirpath, *commentspath, *string;

	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	commentspath	= wi_string_by_appending_path_component(dirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	instance		= wi_plist_read_instance_from_file(commentspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id()) {
		file = wi_file_for_reading(commentspath);
		
		if(!file)
			return NULL;
		
		while((string = wi_file_read_to_string(file, WI_STR(WD_FILES_OLDSTYLE_COMMENT_SEPARATOR)))) {
			array = wi_string_components_separated_by_string(string, WI_STR(WD_FILES_OLDSTYLE_COMMENT_FIELD_SEPARATOR));
			  
			if(wi_array_count(array) == 2 && wi_is_equal(WI_ARRAY(array, 0), name))
				return WI_ARRAY(array, 1);
		}
	}

	return wi_dictionary_data_for_key(instance, name);
#endif
}



wi_boolean_t wd_files_set_comment(wi_string_t *path, wi_string_t *comment, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t				*realpath;
#endif
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *commentspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	commentspath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_error(WI_STR("Could not create \"%@\": %m"), metapath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}
	}
	
	instance = wi_plist_read_instance_from_file(commentspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id())
		instance = wi_mutable_dictionary();
	
	wi_mutable_dictionary_set_data_for_key(instance, comment, name);
	
	if(!wi_plist_write_instance_to_file(instance, commentspath)) {
		wi_log_error(WI_STR("Could not write to \"%@\": %m"), commentspath);
		
		if(user)
			wd_user_reply_file_errno(user, message);
		
		return false;
	}

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_comment_for_path(comment, realpath)) {
			wi_log_error(WI_STR("Could not set Finder comment: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);
			
			return false;
		}
	}
#endif
	
	return true;
}



void wd_files_move_comment(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realfrompath, *comment;
	
	realfrompath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(frompath, user));
	comment			= wd_files_comment(realfrompath);
	
	if(comment) {
		wd_files_remove_comment(frompath, user, message);
		wd_files_set_comment(topath, comment, user, message);
	}
}



wi_boolean_t wd_files_remove_comment(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t				*realpath;
#endif
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *commentspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	commentspath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	
	instance = wi_plist_read_instance_from_file(commentspath);
	
	if(instance && wi_runtime_id(instance) != wi_dictionary_runtime_id()) {
		wi_mutable_dictionary_remove_data_for_key(instance, name);
		
		if(wi_dictionary_count(instance) > 0) {
			if(!wi_plist_write_instance_to_file(instance, commentspath)) {
				wi_log_error(WI_STR("Could not write to \"%@\": %m"), commentspath);
				
				if(user)
					wd_user_reply_file_errno(user, message);
				
				return false;
			}
		} else {
			if(!wi_fs_delete_path(commentspath)) {
				wi_log_error(WI_STR("Could not delete \"%@\": %m"), commentspath);
				
				if(user)
					wd_user_reply_file_errno(user, message);
				
				return false;
			}
		}
	}
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_comment_for_path(WI_STR(""), realpath)) {
			wi_log_error(WI_STR("Could not set Finder comment: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);
			
			return false;
		}
	}
#endif

	return true;
}



#pragma mark -

wi_boolean_t wd_files_set_label(wi_string_t *path, wd_file_label_t label, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t				*realpath;
#endif
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *labelspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	labelspath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_LABELS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_error(WI_STR("Could not create \"%@\": %m"), metapath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}
	}
	
	instance = wi_plist_read_instance_from_file(labelspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id())
		instance = wi_mutable_dictionary();
	
	wi_mutable_dictionary_set_data_for_key(instance, WI_INT32(label), name);
	
	if(!wi_plist_write_instance_to_file(instance, labelspath)) {
		wi_log_error(WI_STR("Could not write to \"%@\": %m"), labelspath);
		
		if(user)
			wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_label_for_path(label, realpath)) {
			wi_log_error(WI_STR("Could not set Finder label: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);
			
			return false;
		}
	}
#endif
	
	return true;
}



wd_file_label_t wd_files_label(wi_string_t *path) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	return wi_fs_finder_label_for_path(path);
#else
	wi_runtime_instance_t	*instance;
	wi_number_t				*label;
	wi_string_t				*name, *dirpath, *labelspath;

	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	labelspath		= wi_string_by_appending_path_component(dirpath, WI_STR(WD_FILES_META_LABELS_PATH));
	instance		= wi_plist_read_instance_from_file(labelspath);
	label			= instance ? wi_dictionary_data_for_key(instance, name) : NULL;
	
	return label ? wi_number_int32(label) : WD_FILE_LABEL_NONE;
#endif
}



wi_boolean_t wd_files_remove_label(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t				*realpath;
#endif
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *labelspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	labelspath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_LABELS_PATH));
	
	instance = wi_plist_read_instance_from_file(labelspath);
	
	if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
		wi_mutable_dictionary_remove_data_for_key(instance, name);
		
		if(wi_dictionary_count(instance) > 0) {
			if(!wi_plist_write_instance_to_file(instance, labelspath)) {
				wi_log_error(WI_STR("Could not write to \"%@\": %m"), labelspath);
				
				if(user)
					wd_user_reply_file_errno(user, message);
				
				return false;
			}
		} else {
			if(!wi_fs_delete_path(labelspath)) {
				wi_log_error(WI_STR("Could not delete \"%@\": %m"), labelspath);
				
				if(user)
					wd_user_reply_file_errno(user, message);
				
				return false;
			}
		}
	}
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_label_for_path(WI_FS_FINDER_LABEL_NONE, realpath)) {
			wi_log_error(WI_STR("Could not remove Finder label: %m"));
			wd_user_reply_internal_error(user, wi_error_string(), message);
			
			return false;
		}
	}
#endif
	
	return true;
}



void wd_files_move_label(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realfrompath;
	wd_file_label_t		label;
	
	realfrompath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(frompath, user));
	label			= wd_files_label(realfrompath);
	
	if(label != WD_FILE_LABEL_NONE) {
		wd_files_remove_label(frompath, user, message);
		wd_files_set_label(topath, label, user, message);
	}
}



#pragma mark -

wi_boolean_t wd_files_set_privileges(wi_string_t *path, wd_files_privileges_t *privileges, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *metapath, *permissionspath;
	wi_string_t		*string;
	
	realpath			= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	metapath			= wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	permissionspath		= wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PERMISSIONS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_error(WI_STR("Could not create \"%@\": %m"), metapath);
			wd_user_reply_file_errno(user, message);

			return false;
		}
	}
	
	string = wd_files_privileges_string(privileges);
	
	if(!wi_string_write_to_file(string, permissionspath)) {
		wi_log_error(WI_STR("Could not write to \"%@\": %m"), permissionspath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	return true;
}



wd_files_privileges_t * wd_files_privileges(wi_string_t *path, wd_user_t *user) {
	wi_string_t				*realpath;
	
	realpath = wd_files_drop_box_path_in_path(path, user);
	
	if(!realpath)
		return NULL;
	
	return wd_files_drop_box_privileges(realpath);
}



wd_files_privileges_t * wd_files_drop_box_privileges(wi_string_t *path) {
	wi_string_t				*permissionspath, *string;
	wd_files_privileges_t	*privileges;
	wi_fs_stat_t			sb;
	
	permissionspath = wi_string_by_appending_path_component(path, WI_STR(WD_FILES_META_PERMISSIONS_PATH));
	
	if(!wi_fs_stat_path(permissionspath, &sb))
		return wd_files_privileges_default_drop_box_privileges();
	
	if(sb.size > 128) {
		wi_log_error(WI_STR("Could not read \"%@\": Size is too large (%u"), permissionspath, sb.size);
		
		return wd_files_privileges_default_drop_box_privileges();
	}
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), permissionspath));
	
	if(!string) {
		wi_log_error(WI_STR("Could not read \"%@\": %m"), permissionspath);
		
		return wd_files_privileges_default_drop_box_privileges();
	}
	
	privileges = wd_files_privileges_with_string(string);
	
	if(!privileges) {
		wi_log_error(WI_STR("Could not read \"%@\": Contents is malformed (\"%@\")"), permissionspath, string);
		
		return wd_files_privileges_default_drop_box_privileges();
	}
	
	return privileges;
}



#pragma mark -

wi_boolean_t wd_files_path_is_valid(wi_string_t *path) {
	if(wi_string_has_prefix(path, WI_STR(".")))
		return false;

	if(wi_string_contains_string(path, WI_STR("/.."), 0))
        return false;

	if(wi_string_contains_string(path, WI_STR("../"), 0))
        return false;

	return true;
}



wi_string_t * wd_files_virtual_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*accountpath, *virtualpath;
	wd_account_t	*account;
	
	account			= user ? wd_user_account(user) : NULL;
	accountpath		= account ? wd_account_files(account) : NULL;
	
	if(accountpath && wi_string_length(accountpath) > 0)
		virtualpath = wi_string_by_normalizing_path(wi_string_with_format(WI_STR("%@/%@"), wd_account_files(account), path));
	else
		virtualpath = path;
	
	return virtualpath;
}



wi_string_t * wd_files_real_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*accountpath, *realpath;
	wd_account_t	*account;
	
	account			= user ? wd_user_account(user) : NULL;
	accountpath		= account ? wd_account_files(account) : NULL;
	
	if(accountpath && wi_string_length(accountpath) > 0)
		realpath = wi_string_with_format(WI_STR("%@/%@/%@"), wd_files, wd_account_files(account), path);
	else
		realpath = wi_string_with_format(WI_STR("%@/%@"), wd_files, path);
	
	return realpath;
}



wi_boolean_t wd_files_has_uploads_or_drop_box_in_path(wi_string_t *path, wd_user_t *user, wd_files_privileges_t **privileges) {
	wi_mutable_string_t		*realpath;
	wi_string_t				*dirpath;
	wi_array_t				*array;
	wi_uinteger_t			i, count;
	
	realpath	= wi_autorelease(wi_mutable_copy(wi_string_by_resolving_aliases_in_path(wd_files_real_path(WI_STR("/"), user))));
	dirpath		= wi_string_by_deleting_last_path_component(path);
	array		= wi_string_path_components(dirpath);
	count		= wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		wi_mutable_string_append_path_component(realpath, WI_ARRAY(array, i));
		
		switch(wd_files_type(realpath)) {
			case WD_FILE_TYPE_UPLOADS:
				*privileges = NULL;
				
				return true;
				break;
				
			case WD_FILE_TYPE_DROPBOX:
				*privileges = wd_files_drop_box_privileges(path);
				
				return true;
				break;
				
			default:
				break;
		}
	}
	
	*privileges = NULL;
	
	return false;
}



#pragma mark -

static wi_string_t * wd_files_drop_box_path_in_path(wi_string_t *path, wd_user_t *user) {
	wi_mutable_string_t		*realpath;
	wi_array_t				*array;
	wi_uinteger_t			i, count;
	
	realpath	= wi_autorelease(wi_mutable_copy(wi_string_by_resolving_aliases_in_path(wd_files_real_path(WI_STR("/"), user))));
	array		= wi_string_path_components(path);
	count		= wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		wi_mutable_string_append_path_component(realpath, WI_ARRAY(array, i));
		
		if(wd_files_type(realpath) == WD_FILE_TYPE_DROPBOX)
			return realpath;
	}
	
	return NULL;
}



#pragma mark -

wi_string_t * wd_files_string_for_bytes(wi_file_offset_t bytes) {
	double						kb, mb, gb, tb, pb;

	if(bytes < 1024)
		return wi_string_with_format(WI_STR("%llu bytes"), bytes);

	kb = (double) bytes / 1024.0;

	if(kb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f KB"), kb);

	mb = (double) kb / 1024.0;

	if(mb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f MB"), mb);

	gb = (double) mb / 1024.0;

	if(gb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f GB"), gb);

	tb = (double) gb / 1024.0;

	if(tb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f TB"), tb);

	pb = (double) tb / 1024.0;

	if(pb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f PB"), pb);

	return NULL;
}




#pragma mark -

static wd_files_privileges_t * wd_files_privileges_alloc(void) {
	return wi_runtime_create_instance(wd_files_privileges_runtime_id, sizeof(wd_files_privileges_t));
}



static wi_string_t * wd_files_privileges_description(wi_runtime_instance_t *instance) {
	wd_files_privileges_t		*privileges = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{owner = \"%@\" %c%c, group = \"%@\" %c%c, everyone = %c%c}"),
		wi_runtime_class_name(privileges),
		privileges,
		privileges->owner,
		(privileges->mode & WD_FILE_OWNER_READ) ? 'r' : '-',
		(privileges->mode & WD_FILE_OWNER_WRITE) ? 'w' : '-',
		privileges->group,
		(privileges->mode & WD_FILE_GROUP_READ) ? 'r' : '-',
		(privileges->mode & WD_FILE_GROUP_WRITE) ? 'w' : '-',
		(privileges->mode & WD_FILE_EVERYONE_READ) ? 'r' : '-',
		(privileges->mode & WD_FILE_EVERYONE_WRITE) ? 'w' : '-');
}



static void wd_files_privileges_dealloc(wi_runtime_instance_t *instance) {
	wd_files_privileges_t		*privileges = instance;
	
	wi_release(privileges->owner);
	wi_release(privileges->group);
}



#pragma mark -

wd_files_privileges_t * wd_files_privileges_with_message(wi_p7_message_t *message) {
	wd_files_privileges_t		*privileges;
	wi_p7_boolean_t				value;
	
	privileges = wd_files_privileges_alloc();
	privileges->owner = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.file.owner")));

	if(!privileges->owner)
		privileges->owner = wi_retain(WI_STR(""));
	
	privileges->group = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.file.group")));
	
	if(!privileges->group)
		privileges->group = wi_retain(WI_STR(""));
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.owner.read")) && value)
		privileges->mode |= WD_FILE_OWNER_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.owner.write")) && value)
		privileges->mode |= WD_FILE_OWNER_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.group.read")) && value)
		privileges->mode |= WD_FILE_GROUP_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.group.write")) && value)
		privileges->mode |= WD_FILE_GROUP_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.everyone.read")) && value)
		privileges->mode |= WD_FILE_EVERYONE_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.everyone.write")) && value)
		privileges->mode |= WD_FILE_EVERYONE_WRITE;

	return wi_autorelease(privileges);
}



static wd_files_privileges_t * wd_files_privileges_with_string(wi_string_t *string) {
	wi_array_t				*array;
	wd_files_privileges_t	*privileges;
	
	string				= wi_string_by_deleting_surrounding_whitespace(string);
	array				= wi_string_components_separated_by_string(string, WI_STR(WD_FILES_PERMISSIONS_FIELD_SEPARATOR));
	
	if(wi_array_count(array) != 3)
		return NULL;
	
	privileges			= wd_files_privileges_alloc();
	privileges->owner	= wi_retain(WI_ARRAY(array, 0));
	privileges->group	= wi_retain(WI_ARRAY(array, 1));
	privileges->mode	= wi_string_uint32(WI_ARRAY(array, 2));
	
	return wi_autorelease(privileges);
}



static wd_files_privileges_t * wd_files_privileges_default_drop_box_privileges(void) {
	wd_files_privileges_t	*privileges;
	
	privileges			= wd_files_privileges_alloc();
	privileges->owner	= wi_retain(WI_STR(""));
	privileges->group	= wi_retain(WI_STR(""));
	privileges->mode	= WD_FILE_EVERYONE_WRITE;
	
	return wi_autorelease(privileges);
}



#pragma mark -

static wi_string_t * wd_files_privileges_string(wd_files_privileges_t *privileges) {
	return wi_string_with_format(WI_STR("%#@%s%#@%s%u"),
	   privileges->owner,		WD_FILES_PERMISSIONS_FIELD_SEPARATOR,
	   privileges->group,		WD_FILES_PERMISSIONS_FIELD_SEPARATOR,
	   privileges->mode);
}



wi_boolean_t wd_files_privileges_is_readable_by_account(wd_files_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_FILE_EVERYONE_READ)
		return true;
	
	if(wd_account_file_access_all_dropboxes(account))
		return true;
	
	if(privileges->mode & WD_FILE_GROUP_READ && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_FILE_OWNER_READ && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



wi_boolean_t wd_files_privileges_is_writable_by_account(wd_files_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_FILE_EVERYONE_WRITE)
		return true;
	
	if(wd_account_file_access_all_dropboxes(account))
		return true;
	
	if(privileges->mode & WD_FILE_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_FILE_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



wi_boolean_t wd_files_privileges_is_readable_and_writable_by_account(wd_files_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_FILE_EVERYONE_READ && privileges->mode & WD_FILE_EVERYONE_WRITE)
		return true;
	
	if(wd_account_file_access_all_dropboxes(account))
		return true;
	
	if(privileges->mode & WD_FILE_GROUP_READ && privileges->mode & WD_FILE_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_FILE_OWNER_READ && privileges->mode & WD_FILE_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}
