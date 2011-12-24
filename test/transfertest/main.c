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

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <wired/wired.h>

static void						wc_usage(void);

static void						wc_test(wi_url_t *, wi_uinteger_t, wi_string_t *);
static void						wc_test_thread(wi_runtime_instance_t *);
static void						wc_download(wi_p7_socket_t *, wi_string_t *);
static void						wc_upload(wi_p7_socket_t *, wi_string_t *);
static wi_p7_socket_t *			wc_connect(wi_url_t *);
static wi_boolean_t				wc_login(wi_p7_socket_t *, wi_url_t *);
static wi_p7_message_t *		wc_write_message_and_read_reply(wi_p7_socket_t *, wi_p7_message_t *, wi_string_t *);


static wi_p7_spec_t				*wc_spec;


int main(int argc, const char **argv) {
	wi_pool_t			*pool;
	wi_string_t			*user, *password, *root_path;
	wi_mutable_url_t	*url;
	int					ch;
	
	wi_initialize();
	wi_load(argc, argv);
	
	wi_log_tool 	= true;
	wi_log_level 	= WI_LOG_DEBUG;
	
	pool			= wi_pool_init(wi_pool_alloc());
	
	user 			= WI_STR("guest");
	password		= WI_STR("");
	root_path		= WI_STR(WD_ROOT);
	
	while((ch = getopt(argc, (char * const *) argv, "d:p:u:")) != -1) {
		switch(ch) {
			case 'd':
				root_path = wi_string_with_cstring(optarg);
				break;
				
			case 'p':
				password = wi_string_with_cstring(optarg);
				break;
				
			case 'u':
				user = wi_string_with_cstring(optarg);
				break;
				
			case '?':
			case 'h':
			default:
				wc_usage();
				break;
		}
	}
	
	argc -= optind;
	argv += optind;
	
	if(argc != 1)
		wc_usage();
	
	if(!wi_fs_change_directory(root_path))
		wi_log_fatal(WI_STR("Could not change directory to %@: %m"), root_path);
	
	wc_spec = wi_p7_spec_init_with_file(wi_p7_spec_alloc(), WI_STR("wired.xml"), WI_P7_CLIENT);
	
	if(!wc_spec)
		wi_log_fatal(WI_STR("Could not open wired.xml: %m"));
	
	url = wi_url_init_with_string(wi_mutable_url_alloc(), wi_string_with_cstring(argv[0]));
	wi_mutable_url_set_scheme(url, WI_STR("wired"));
	
	if(!url)
		wc_usage();
	
	wi_mutable_url_set_user(url, user);
	wi_mutable_url_set_password(url, password);
	
	if(wi_url_port(url) == 0)
		wi_mutable_url_set_port(url, 4871);
	
	if(!wi_url_is_valid(url))
		wc_usage();
	
	signal(SIGPIPE, SIG_IGN);
	
	wc_test(url, 10, WI_STR("/transfertest"));
	
	wi_release(pool);
	
	return 0;
}



static void wc_usage(void) {
	fprintf(stderr,
"Usage: wiredclient [-p password] [-u user] host\n\
\n\
Options:\n\
    -p password         password\n\
    -u user             user\n\
\n\
By Axel Andersson <axel@zankasoftware.com>\n");
	
	exit(2);
}



#pragma mark -

static void wc_test(wi_url_t *url, wi_uinteger_t count, wi_string_t *path) {
	wi_p7_socket_t		*socket;
	wi_p7_message_t		*message;
	wi_mutable_url_t	*testurl;
	wi_uinteger_t		i;
	
	wi_log_info(WI_STR("Connecting initial socket..."));
	
	socket = wc_connect(url);
	
	if(!socket)
		return;
	
	if(!wc_login(socket, url))
		wi_log_fatal(WI_STR("Could not login: %m"));

	wi_log_info(WI_STR("Creating %@..."), path);
	
	message = wi_p7_message_with_name(WI_STR("wired.file.create_directory"), wc_spec);
	wi_p7_message_set_string_for_name(message, path, WI_STR("wired.file.path"));
	wi_p7_message_set_enum_name_for_name(message, WI_STR("wired.file.type.uploads"), WI_STR("wired.file.type"));
	
	message = wc_write_message_and_read_reply(socket, message, WI_STR("wired.error.file_exists"));

	wi_log_info(WI_STR("Connecting test sockets..."));
	
	for(i = 0; i < count; i++) {
		testurl = wi_autorelease(wi_mutable_copy(url));
		
		wi_mutable_url_set_path(testurl, wi_string_with_format(WI_STR("/transfertest/%u"), i));

		if(!wi_thread_create_thread(wc_test_thread, testurl))
			wi_log_error(WI_STR("Could not create a thread: %m"));
	}
	
	wi_thread_sleep(86400.0);
}



static void wc_test_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wi_p7_socket_t		*socket;
	wi_url_t			*url = argument;
	wi_string_t			*path;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	socket = wc_connect(url);
	
	if(!socket)
		return;
	
	if(!wc_login(socket, url))
		wi_log_fatal(WI_STR("Could not login: %m"));
	
	path = wi_url_path(url);
	
	while(true) {
		wc_upload(socket, path);
		wc_download(socket, path);
	}
	
	wi_release(pool);
}



static void wc_download(wi_p7_socket_t *socket, wi_string_t *path) {
	wi_p7_message_t		*message, *reply;
	wi_string_t			*name, *error;
	void				*file;
	wi_p7_uint32_t		queue;
	wi_p7_uint64_t		size;
	wi_integer_t		readsize;
	
	message = wi_p7_message_with_name(WI_STR("wired.transfer.download_file"), wc_spec);
	wi_p7_message_set_string_for_name(message, path, WI_STR("wired.file.path"));
	wi_p7_message_set_uint64_for_name(message, 0, WI_STR("wired.transfer.data_offset"));
	wi_p7_message_set_uint64_for_name(message, 0, WI_STR("wired.transfer.rsrc_offset"));
	
	if(!wi_p7_socket_write_message(socket, 0.0, message))
		wi_log_fatal(WI_STR("Could not write message for %@: %m"), path);
	
	while(true) {
		message = wi_p7_socket_read_message(socket, 0.0);
		
		if(!message)
			wi_log_fatal(WI_STR("Could not read message for %@: %m"), path);
		
		name = wi_p7_message_name(message);
		
		if(wi_is_equal(name, WI_STR("wired.transfer.download"))) {
			wi_p7_message_get_oobdata_for_name(message, &size, WI_STR("wired.transfer.data"));
			
			wi_log_info(WI_STR("Downloading %@..."), path);
			
			while(size > 0) {
				readsize = wi_p7_socket_read_oobdata(socket, 0.0, &file);
				
				if(readsize < 0)
					wi_log_fatal(WI_STR("Could not read download for %@: %m"), path);
				
				size -= readsize;
			}
			
			return;
		}
		else if(wi_is_equal(name, WI_STR("wired.transfer.queue"))) {
			wi_p7_message_get_uint32_for_name(message, &queue, WI_STR("wired.transfer.queue_position"));
			
			wi_log_info(WI_STR("Queued at position %u for %@"), queue, path);
		}
		else if(wi_is_equal(name, WI_STR("wired.send_ping"))) {
			reply = wi_p7_message_with_name(WI_STR("wired.ping"), wc_spec);
			
			if(!wi_p7_socket_write_message(socket, 0.0, reply))
				wi_log_fatal(WI_STR("Could not send message for %@: %m"), path);
		}
		else if(wi_is_equal(name, WI_STR("wired.error"))) {
			error = wi_p7_message_enum_name_for_name(message, WI_STR("wired.error"));
			
			wi_log_fatal(WI_STR("Could not download %@: %@"), path, error);
		}
		else {
			wi_log_fatal(WI_STR("Unexpected message %@ for download of %@"), name, path);
		}
	}
}



static void wc_upload(wi_p7_socket_t *socket, wi_string_t *path) {
	wi_p7_message_t		*message, *reply;
	wi_string_t			*name, *error;
	char				file[8192];
	wi_uinteger_t		sendsize;
	wi_p7_uint64_t		size, offset;
	wi_p7_uint32_t		queue;
	
	memset(file, 42, sizeof(file));
	
	size = 1000 * sizeof(file);
	
	wi_log_info(WI_STR("Deleting %@..."), path);
	
	message = wi_p7_message_with_name(WI_STR("wired.file.delete"), wc_spec);
	wi_p7_message_set_string_for_name(message, path, WI_STR("wired.file.path"));
	
	message = wc_write_message_and_read_reply(socket, message, WI_STR("wired.error.file_not_found"));
	
	message = wi_p7_message_with_name(WI_STR("wired.transfer.upload_file"), wc_spec);
	wi_p7_message_set_string_for_name(message, path, WI_STR("wired.file.path"));
	wi_p7_message_set_uint64_for_name(message, size, WI_STR("wired.transfer.data_size"));
	wi_p7_message_set_uint64_for_name(message, 0, WI_STR("wired.transfer.rsrc_size"));
	
	if(!wi_p7_socket_write_message(socket, 0.0, message))
		wi_log_fatal(WI_STR("Could not write message for %@: %m"), path);
	
	while(true) {
		message = wi_p7_socket_read_message(socket, 0.0);
		
		if(!message)
			wi_log_fatal(WI_STR("Could not read message for %@: %m"), path);
		
		name = wi_p7_message_name(message);
		
		if(wi_is_equal(name, WI_STR("wired.transfer.upload_ready"))) {
			wi_p7_message_get_uint64_for_name(message, &offset, WI_STR("wired.transfer.data_offset"));
			
			wi_log_info(WI_STR("Uploading %@..."), path);
			
			message = wi_p7_message_with_name(WI_STR("wired.transfer.upload"), wc_spec);
			wi_p7_message_set_string_for_name(message, path, WI_STR("wired.file.path"));
			wi_p7_message_set_oobdata_for_name(message, size - offset, WI_STR("wired.transfer.data"));
			wi_p7_message_set_oobdata_for_name(message, 0, WI_STR("wired.transfer.rsrc"));
			wi_p7_message_set_data_for_name(message, wi_data(), WI_STR("wired.transfer.finderinfo"));
			
			if(!wi_p7_socket_write_message(socket, 0.0, message))
				wi_log_fatal(WI_STR("Could not write message for %@: %m"), path);
			
			while(size - offset > 0) {
				sendsize = WI_MIN(size - offset, sizeof(file));
				
				if(!wi_p7_socket_write_oobdata(socket, 0.0, file, sendsize))
					wi_log_fatal(WI_STR("Could not write data for %@: %m"), path);
				
				size -= sendsize;
			}
			
			return;
		}
		else if(wi_is_equal(name, WI_STR("wired.transfer.queue"))) {
			wi_p7_message_get_uint32_for_name(message, &queue, WI_STR("wired.transfer.queue_position"));
			
			wi_log_info(WI_STR("Queued at position %u for %@"), queue, path);
		}
		else if(wi_is_equal(name, WI_STR("wired.send_ping"))) {
			reply = wi_p7_message_with_name(WI_STR("wired.ping"), wc_spec);
			
			if(!wi_p7_socket_write_message(socket, 0.0, reply))
				wi_log_fatal(WI_STR("Could not send message for %@: %m"), path);
		}
		else if(wi_is_equal(name, WI_STR("wired.error"))) {
			error = wi_p7_message_enum_name_for_name(message, WI_STR("wired.error"));
			
			wi_log_fatal(WI_STR("Could not upload %@: %@"), path, error);
		}
		else {
			wi_log_fatal(WI_STR("Unexpected message %@ for upload of %@"), name, path);
		}
	}
}



#pragma mark -

static wi_p7_socket_t * wc_connect(wi_url_t *url) {
	wi_enumerator_t		*enumerator;
	wi_socket_t			*socket;
	wi_p7_socket_t		*p7_socket;
	wi_array_t			*addresses;
	wi_address_t		*address;
	
	addresses = wi_host_addresses(wi_host_with_string(wi_url_host(url)));
	
	if(!addresses)
		return NULL;
	
	enumerator = wi_array_data_enumerator(addresses);
	
	while((address = wi_enumerator_next_data(enumerator))) {
		wi_address_set_port(address, wi_url_port(url));
		
		socket = wi_socket_with_address(address, WI_SOCKET_TCP);
		
		if(!socket)
			continue;
		
		wi_socket_set_interactive(socket, true);
		
		wi_log_info(WI_STR("Connecting to %@:%u..."), wi_address_string(address), wi_address_port(address));
		
		if(!wi_socket_connect(socket, 10.0)) {
			wi_socket_close(socket);
			
			continue;
		}
		
		wi_log_info(WI_STR("Connected, performing handshake"));

		p7_socket = wi_autorelease(wi_p7_socket_init_with_socket(wi_p7_socket_alloc(), socket, wc_spec));
		
		if(!wi_p7_socket_connect(p7_socket,
								 10.0,
								 WI_P7_ENCRYPTION_RSA_AES256_SHA1 | WI_P7_CHECKSUM_SHA1,
								 WI_P7_BINARY,
								 wi_url_user(url),
								 wi_string_sha1(wi_url_password(url)))) {
			wi_log_error(WI_STR("Could not connect to %@: %m"), wi_address_string(address));
			
			wi_socket_close(socket);
			
			continue;
		}
		
		wi_log_info(WI_STR("Connected to P7 server with protocol %@ %@"),
			wi_p7_socket_remote_protocol_name(p7_socket), wi_p7_socket_remote_protocol_version(p7_socket));
		
		return p7_socket;
	}
	
	return NULL;
}



static wi_boolean_t wc_login(wi_p7_socket_t *socket, wi_url_t *url) {
	wi_p7_message_t		*message;
	
	wi_log_info(WI_STR("Performing Wired handshake..."));
	
	message = wi_p7_message_with_name(WI_STR("wired.client_info"), wc_spec);
	wi_p7_message_set_string_for_name(message, WI_STR("transfertest"), WI_STR("wired.info.application.name"));
	wi_p7_message_set_string_for_name(message, WI_STR("1.0"), WI_STR("wired.info.application.version"));
	wi_p7_message_set_uint32_for_name(message, 1, WI_STR("wired.info.application.build"));
	wi_p7_message_set_string_for_name(message, wi_process_os_name(wi_process()), WI_STR("wired.info.os.name"));
	wi_p7_message_set_string_for_name(message, wi_process_os_release(wi_process()), WI_STR("wired.info.os.version"));
	wi_p7_message_set_string_for_name(message, wi_process_os_arch(wi_process()), WI_STR("wired.info.arch"));
	wi_p7_message_set_bool_for_name(message, false, WI_STR("wired.info.supports_rsrc"));

	message = wc_write_message_and_read_reply(socket, message, NULL);
									  
	wi_log_info(WI_STR("Connected to \"%@\""), wi_p7_message_string_for_name(message, WI_STR("wired.info.name")));
	wi_log_info(WI_STR("Logging in as \"%@\"..."), wi_url_user(url));
	
	message = wi_p7_message_with_name(WI_STR("wired.send_login"), wc_spec);
	wi_p7_message_set_string_for_name(message, wi_url_user(url), WI_STR("wired.user.login"));
	wi_p7_message_set_string_for_name(message, wi_string_sha1(wi_url_password(url)), WI_STR("wired.user.password"));
	
	message = wc_write_message_and_read_reply(socket, message, NULL);
	
	if(!wi_is_equal(wi_p7_message_name(message), WI_STR("wired.login"))) {
		wi_log_info(WI_STR("Login failed"));
		
		return false;
	}

	message = wi_p7_socket_read_message(socket, 0.0);
	
	if(!message)
		return false;
	
	if(!wi_is_equal(wi_p7_message_name(message), WI_STR("wired.account.privileges"))) {
		wi_log_info(WI_STR("Login failed"));
		
		return false;
	}

	message = wi_p7_message_with_name(WI_STR("wired.user.set_nick"), wc_spec);
	wi_p7_message_set_string_for_name(message, WI_STR("transfertest"), WI_STR("wired.user.nick"));
	
	wc_write_message_and_read_reply(socket, message, NULL);
	
	return true;
}



static wi_p7_message_t * wc_write_message_and_read_reply(wi_p7_socket_t *socket, wi_p7_message_t *message, wi_string_t *expected_error) {
	wi_string_t		*name, *error;
	
	if(!wi_p7_socket_write_message(socket, 0.0, message))
		wi_log_fatal(WI_STR("Could not write message: %m"));
	
	message = wi_p7_socket_read_message(socket, 0.0);
	
	if(!message)
		wi_log_fatal(WI_STR("Could not read message: %m"));
	
	name = wi_p7_message_name(message);
	
	if(wi_is_equal(name, WI_STR("wired.error"))) {
		error = wi_p7_message_enum_name_for_name(message, WI_STR("wired.error"));
		
		if(expected_error) {
			if(!wi_is_equal(error, expected_error))
			   wi_log_fatal(WI_STR("Unexpected error %@"), error);
		} else {
			wi_log_fatal(WI_STR("Unexpected error %@"), error);
		}
	}
	
	return message;
}
