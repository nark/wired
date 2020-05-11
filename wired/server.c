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

#ifdef HAVE_CORESERVICES_CORESERVICES_H
#include <CoreFoundation/CoreFoundation.h>
#endif

#ifdef HAVE_DNS_SD_H
#include <dns_sd.h>
#endif

#include <sys/types.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <wired/wired.h>

#include "accounts.h"
#include "banlist.h"
#include "events.h"
#include "files.h"
#include "index.h"
#include "main.h"
#include "messages.h"
#include "portmap.h"
#include "server.h"
#include "servers.h"
#include "settings.h"
#include "trackers.h"
#include "transfers.h"

#define WD_SERVER_PING_INTERVAL		60.0

#ifdef HAVE_CORESERVICES_CORESERVICES_H
static void							wd_server_cf_thread(wi_runtime_instance_t *);
#endif

#ifdef HAVE_DNS_SD_H
static wi_string_t *				wd_server_dnssd_error_string(int);
static void							wd_server_dnssd_register(void);
static void							wd_server_dnssd_register_service_callback(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType, const char *, const char *, const char *, void *);

#ifdef HAVE_CORESERVICES_CORESERVICES_H
static void							wd_server_dnssd_register_socket_callback(CFSocketRef, CFSocketCallBackType, CFDataRef, const void *, void *);
#endif
#endif

static void							wd_server_portmap_thread(wi_runtime_instance_t *);
static void							wd_server_portmap_timer(wi_timer_t *timer);

static void							wd_server_listen_thread(wi_runtime_instance_t *);
static void							wd_server_accept_thread(wi_runtime_instance_t *);
static void							wd_server_receive_thread(wi_runtime_instance_t *);
static void							wd_server_log_callback(wi_log_level_t, wi_string_t *);
static void							wd_server_ping_users(wi_timer_t *);

static void                     	wd_server_database_snapshot_register_with_timer(wi_timer_t *);


#ifdef HAVE_CORESERVICES_CORESERVICES_H
static CFRunLoopRef					wd_cf_runloop;
static wi_condition_lock_t			*wd_cf_lock;
#endif

#ifdef HAVE_DNS_SD_H
static DNSServiceRef				wd_dnssd_register_service;

#ifdef HAVE_CORESERVICES_CORESERVICES_H
static CFSocketRef					wd_dnssd_register_socket;
static CFRunLoopSourceRef			wd_dnssd_register_source;
#endif
#endif

static wi_timer_t					*wd_ping_timer;
static wi_p7_message_t				*wd_ping_message;
static wi_mutable_array_t			*wd_tcp_sockets, *wd_udp_sockets;
static wi_rsa_t						*wd_rsa;
static wi_mutable_array_t			*wd_log_entries;
static wi_uinteger_t				wd_max_log_entries;

static wi_timer_t               	*wd_database_snapshot_timer;

wi_uinteger_t						wd_port;
wi_data_t							*wd_banner;
wi_p7_spec_t						*wd_p7_spec;



void wd_server_initialize(void) {
	wi_string_t				*path;
	wi_time_interval_t 		interval;
	wi_boolean_t			snapshot_enabled;

	interval = 60*60*24;

	wd_ping_timer = wi_timer_init_with_function(wi_timer_alloc(),
												wd_server_ping_users,
												WD_SERVER_PING_INTERVAL,
												true);

	wd_database_snapshot_timer = wi_timer_init_with_function(wi_timer_alloc(),
	                                            wd_server_database_snapshot_register_with_timer,
	                                            interval,
	                                            true);

	wd_rsa = wi_rsa_init_with_bits(wi_rsa_alloc(), 2048);
	
	if(!wd_rsa)
		wi_log_fatal(WI_STR("Could not create RSA key: %m"));
	
	wi_p7_socket_password_provider = wd_accounts_password_for_user;
	
	path = WI_STR("wired.xml");
	wd_p7_spec = wi_p7_spec_init_with_file(wi_p7_spec_alloc(), path, WI_P7_SERVER);
	
	if(!wd_p7_spec)
		wi_log_fatal(WI_STR("Could not load protocol \"%@\": %m"), path);
	
	wi_log_info(WI_STR("Loaded protocol %@ %@"),
		wi_p7_spec_name(wd_p7_spec),
		wi_p7_spec_version(wd_p7_spec));
	
	wd_ping_message = wi_retain(wi_p7_message_with_name(WI_STR("wired.send_ping"), wd_p7_spec));
	
	wi_log_callback = wd_server_log_callback;
	
	wd_max_log_entries = (wi_log_limit > 0) ? wi_log_limit : 500;
	
	wd_log_entries = wi_array_init_with_capacity(wi_mutable_array_alloc(), wd_max_log_entries);
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wd_cf_lock = wi_condition_lock_init_with_condition(wi_condition_lock_alloc(), 0);
#endif
}



void wd_server_schedule(void) {
	wi_boolean_t			snapshot_enabled;
	wi_time_interval_t		interval;

	wi_timer_schedule(wd_ping_timer);

	snapshot_enabled	= wi_config_bool_for_name(wd_config, WI_STR("snapshots"));

	if(snapshot_enabled == false) {
		if(wd_database_snapshot_timer)
			wi_timer_invalidate(wd_database_snapshot_timer);

	} else {
		interval = wi_config_time_interval_for_name(wd_config, WI_STR("snapshot time"));

	    if(interval == 0)
	        interval = 60*60*24;
	    
	    wi_timer_reschedule(wd_database_snapshot_timer, interval);
	}
}



void wd_server_listen(void) {
	wi_enumerator_t			*enumerator;
	wi_mutable_array_t		*addresses;
	wi_array_t				*array, *config_addresses;
	wi_timer_t				*timer;
	wi_address_t			*address;
	wi_socket_t				*tcp_socket, *udp_socket;
	wi_string_t				*ip, *string;
	wi_address_family_t		family;
	
	wd_tcp_sockets		= wi_array_init(wi_mutable_array_alloc());
	wd_udp_sockets		= wi_array_init(wi_mutable_array_alloc());
	addresses			= wi_mutable_array();
	config_addresses	= wi_config_stringlist_for_name(wd_config, WI_STR("address"));
	
	if(wi_array_count(config_addresses) > 0) {
		wi_array_rdlock(config_addresses);
		
		enumerator = wi_array_data_enumerator(config_addresses);
		
		while((string = wi_enumerator_next_data(enumerator))) {
			array = wi_host_addresses(wi_host_with_string(string));

			if(array)
				wi_mutable_array_add_data_from_array(addresses, array);
			else
				wi_log_error(WI_STR("Could not resolve \"%@\": %m"), string);
		}
		
		wi_array_unlock(config_addresses);
	} else {
		wi_mutable_array_add_data(addresses, wi_address_wildcard_for_family(WI_ADDRESS_IPV4));
		wi_mutable_array_add_data(addresses, wi_address_wildcard_for_family(WI_ADDRESS_IPV6));
	}
	
	wd_port = wi_config_port_for_name(wd_config, WI_STR("port"));
	
	enumerator = wi_array_data_enumerator(addresses);
	
	while((address = wi_enumerator_next_data(enumerator))) {
		ip		= wi_address_string(address);
		family	= wi_address_family(address);

		if(wd_address_family != WI_ADDRESS_NULL && family != wd_address_family)
			continue;
		
		wi_address_set_port(address, wd_port);
		
		tcp_socket = wi_autorelease(wi_socket_init_with_address(wi_socket_alloc(), address, WI_SOCKET_TCP));
		
		if(wd_port == 0) {
			wd_port = wi_socket_port(tcp_socket);
			wi_address_set_port(address, wd_port);
		}
		
		udp_socket = wi_autorelease(wi_socket_init_with_address(wi_socket_alloc(), address, WI_SOCKET_UDP));

		if(!tcp_socket || !udp_socket) {
			wi_log_error(WI_STR("Could not create socket for %@: %m"), ip);
			
			continue;
		}

		if(!wi_socket_listen(tcp_socket) || !wi_socket_listen(udp_socket)) {
			wi_log_error(WI_STR("Could not listen on %@ port %u: %m"),
				ip, wi_socket_port(tcp_socket));
			
			continue;
		}
		
		wi_socket_set_interactive(tcp_socket, true);

		wi_mutable_array_add_data(wd_tcp_sockets, tcp_socket);
		wi_mutable_array_add_data(wd_udp_sockets, udp_socket);

		wi_log_info(WI_STR("Listening on %@ port %u"),
			ip, wi_socket_port(tcp_socket));
	}

	if(wi_array_count(wd_tcp_sockets) == 0 || wi_array_count(wd_udp_sockets) == 0)
		wi_log_fatal(WI_STR("Could not listen: No addresses available"));
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	if(!wi_thread_create_thread(wd_server_cf_thread, NULL))
		wi_log_fatal(WI_STR("Could not create a CoreFoundation thread: %m"));
#endif
	
#ifdef HAVE_DNS_SD_H
	wd_server_dnssd_register();
#endif
	
	if(wi_config_bool_for_name(wd_config, WI_STR("map port"))) {
		if(!wi_thread_create_thread(wd_server_portmap_thread, NULL))
			wi_log_fatal(WI_STR("Could not create a portmap thread: %m"));
		
		timer = wi_timer_init_with_function(wi_timer_alloc(), wd_server_portmap_timer, 86400, true);
		wi_timer_schedule(timer);
		wi_release(timer);
	}
	
	if(!wi_thread_create_thread(wd_server_listen_thread, NULL) ||
	   !wi_thread_create_thread(wd_server_receive_thread, NULL))
		wi_log_fatal(WI_STR("Could not create a listen thread: %m"));
}



void wd_server_apply_settings(wi_set_t *changes) {
	wi_string_t		*banner;
	
	banner = wi_config_path_for_name(wd_config, WI_STR("banner"));
	
	if(banner) {
		if(wi_set_contains_data(changes, WI_STR("banner"))) {
			wi_release(wd_banner);
			wd_banner = wi_data_init_with_contents_of_file(wi_data_alloc(), banner);
			
			if(!wd_banner)
				wi_log_error(WI_STR("Could not open \"%@\": %m"), banner);
		}
	} else {
		wi_release(wd_banner);
		wd_banner = NULL;
	}
	
	if(wi_set_contains_data(changes, WI_STR("name")) ||
	   wi_set_contains_data(changes, WI_STR("description")) ||
	   wi_set_contains_data(changes, WI_STR("banner"))) {
		wd_chat_broadcast_message(wd_public_chat, wd_server_info_message());
	}
		
#ifdef HAVE_DNS_SD_H
	if(wi_set_contains_data(changes, WI_STR("name"))) {
		if(wd_tcp_sockets && wd_udp_sockets)
			wd_server_dnssd_register();
	}
#endif
}



void wd_server_cleanup(void) {
	
	if(wi_config_bool_for_name(wd_config, WI_STR("map port"))) {
		wd_portmap_unmap_natpmp();
		wd_portmap_unmap_upnp();
	}
}



#pragma mark -

wi_p7_message_t * wd_client_info_message(void) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.client_info"), wd_p7_spec);
	wi_p7_message_set_string_for_name(message, WI_STR("Wired"), WI_STR("wired.info.application.name"));
	wi_p7_message_set_string_for_name(message, wi_string_with_cstring(WD_VERSION), WI_STR("wired.info.application.version"));
	wi_p7_message_set_string_for_name(message, wi_string_with_format(WI_STR("%x"), WI_REVISION), WI_STR("wired.info.application.build")); 
	wi_p7_message_set_string_for_name(message, wi_process_os_name(wi_process()), WI_STR("wired.info.os.name"));
	wi_p7_message_set_string_for_name(message, wi_process_os_release(wi_process()), WI_STR("wired.info.os.version"));
	wi_p7_message_set_string_for_name(message, wi_process_os_arch(wi_process()), WI_STR("wired.info.arch"));
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_p7_message_set_bool_for_name(message, true, WI_STR("wired.info.supports_rsrc"));
#else
	wi_p7_message_set_bool_for_name(message, false, WI_STR("wired.info.supports_rsrc"));
#endif

	return message;
}



wi_p7_message_t * wd_server_info_message(void) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.server_info"), wd_p7_spec);
	wi_p7_message_set_string_for_name(message, WI_STR("Wired"), WI_STR("wired.info.application.name"));
	wi_p7_message_set_string_for_name(message, wi_string_with_cstring(WD_VERSION), WI_STR("wired.info.application.version"));
	wi_p7_message_set_string_for_name(message, wi_string_with_format(WI_STR("%s"), WI_REVISION), WI_STR("wired.info.application.build"));    
	wi_p7_message_set_string_for_name(message, wi_process_os_name(wi_process()), WI_STR("wired.info.os.name"));
	wi_p7_message_set_string_for_name(message, wi_process_os_release(wi_process()), WI_STR("wired.info.os.version"));
	wi_p7_message_set_string_for_name(message, wi_process_os_arch(wi_process()), WI_STR("wired.info.arch"));

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_p7_message_set_bool_for_name(message, true, WI_STR("wired.info.supports_rsrc"));
#else
	wi_p7_message_set_bool_for_name(message, false, WI_STR("wired.info.supports_rsrc"));
#endif
	
	wi_p7_message_set_string_for_name(message, wi_config_string_for_name(wd_config, WI_STR("name")), WI_STR("wired.info.name"));
	wi_p7_message_set_string_for_name(message, wi_config_string_for_name(wd_config, WI_STR("description")), WI_STR("wired.info.description"));
	wi_p7_message_set_date_for_name(message, wd_start_date, WI_STR("wired.info.start_time"));
	wi_p7_message_set_uint64_for_name(message, wd_index_files_count, WI_STR("wired.info.files.count"));
	wi_p7_message_set_uint64_for_name(message, wd_index_files_size, WI_STR("wired.info.files.size"));
	wi_p7_message_set_data_for_name(message, wd_banner, WI_STR("wired.info.banner"));
	wi_p7_message_set_uint32_for_name(message, wi_config_integer_for_name(wd_config, WI_STR("total downloads")), WI_STR("wired.info.downloads"));
	wi_p7_message_set_uint32_for_name(message, wi_config_integer_for_name(wd_config, WI_STR("total uploads")), WI_STR("wired.info.uploads"));
	wi_p7_message_set_uint32_for_name(message, wi_config_integer_for_name(wd_config, WI_STR("total download speed")), WI_STR("wired.info.download_speed"));
	wi_p7_message_set_uint32_for_name(message, wi_config_integer_for_name(wd_config, WI_STR("total upload speed")), WI_STR("wired.info.upload_speed"));
    
	return message;
}



#pragma mark -

#ifdef HAVE_CORESERVICES_CORESERVICES_H

static void wd_server_cf_thread(wi_runtime_instance_t *argument) {
	wi_pool_t		*pool;
	SInt32			status;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	wi_condition_lock_lock(wd_cf_lock);
	wd_cf_runloop = CFRunLoopGetCurrent();
	wi_condition_lock_unlock_with_condition(wd_cf_lock, 1);

	while(true) {
		status = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 60, true);
		
		if(status == kCFRunLoopRunFinished || status == kCFRunLoopRunTimedOut)
			wi_thread_sleep(1.0);
		else
			wi_pool_drain(pool);
	}
	
	wi_release(pool);
}

#endif



#pragma mark -

#ifdef HAVE_DNS_SD_H

static wi_string_t * wd_server_dnssd_error_string(int code) {
	switch(code) {
		case -65537:	return WI_STR("kDNSServiceErr_Unknown");					break;
		case -65538:	return WI_STR("kDNSServiceErr_NoSuchName");					break;
		case -65539:	return WI_STR("kDNSServiceErr_NoMemory");					break;
		case -65540:	return WI_STR("kDNSServiceErr_BadParam");					break;
		case -65541:	return WI_STR("kDNSServiceErr_BadReference");				break;
		case -65542:	return WI_STR("kDNSServiceErr_BadState");					break;
		case -65543:	return WI_STR("kDNSServiceErr_BadFlags");					break;
		case -65544:	return WI_STR("kDNSServiceErr_Unsupported");				break;
		case -65545:	return WI_STR("kDNSServiceErr_NotInitialized");				break;
		case -65547:	return WI_STR("kDNSServiceErr_AlreadyRegistered");			break;
		case -65548:	return WI_STR("kDNSServiceErr_NameConflict");				break;
		case -65549:	return WI_STR("kDNSServiceErr_Invalid");					break;
		case -65550:	return WI_STR("kDNSServiceErr_Firewall");					break;
		case -65551:	return WI_STR("kDNSServiceErr_Incompatible");				break;
		case -65552:	return WI_STR("kDNSServiceErr_BadInterfaceIndex");			break;
		case -65553:	return WI_STR("kDNSServiceErr_Refused");					break;
		case -65554:	return WI_STR("kDNSServiceErr_NoSuchRecord");				break;
		case -65555:	return WI_STR("kDNSServiceErr_NoAuth");						break;
		case -65556:	return WI_STR("kDNSServiceErr_NoSuchKey");					break;
		case -65557:	return WI_STR("kDNSServiceErr_NATTraversal");				break;
		case -65558:	return WI_STR("kDNSServiceErr_DoubleNAT");					break;
		case -65559:	return WI_STR("kDNSServiceErr_BadTime");					break;
		case -65560:	return WI_STR("kDNSServiceErr_BadSig");						break;
		case -65561:	return WI_STR("kDNSServiceErr_BadKey");						break;
		case -65562:	return WI_STR("kDNSServiceErr_Transient");					break;
		case -65563:	return WI_STR("kDNSServiceErr_ServiceNotRunning");			break;
		case -65564:	return WI_STR("kDNSServiceErr_NATPortMappingUnsupported");	break;
		case -65565:	return WI_STR("kDNSServiceErr_NATPortMappingDisabled");		break;
	}

	return wi_string_with_format(WI_STR("%d"), code);
}



static void wd_server_dnssd_register(void) {
	DNSServiceErrorType		error;
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	if(wd_dnssd_register_source) {
		wi_condition_lock_lock_when_condition(wd_cf_lock, 1, 0.0);
		CFRunLoopRemoveSource(wd_cf_runloop, wd_dnssd_register_source, kCFRunLoopCommonModes);
		wi_condition_lock_unlock(wd_cf_lock);

		CFRelease(wd_dnssd_register_source);
		wd_dnssd_register_source = NULL;
	}
	
	if(wd_dnssd_register_socket) {
		CFSocketInvalidate(wd_dnssd_register_socket);

		CFRelease(wd_dnssd_register_socket);
		wd_dnssd_register_socket = NULL;
	}
#endif
	
	if(wd_dnssd_register_service) {
		DNSServiceRefDeallocate(wd_dnssd_register_service);
		wd_dnssd_register_service = NULL;
	}
	
	error = DNSServiceRegister(&wd_dnssd_register_service,
							   0,
							   kDNSServiceInterfaceIndexAny,
							   wi_string_cstring(wi_config_string_for_name(wd_config, WI_STR("name"))),
							   WD_DNSSD_NAME,
							   NULL,
							   NULL,
							   htons(wd_port),
							   0,
							   NULL,
							   wd_server_dnssd_register_service_callback,
							   NULL);
	
	if(error != kDNSServiceErr_NoError) {
		wi_log_error(WI_STR("Could not register for DNS service discovery: %@"),
			wd_server_dnssd_error_string(error));
		
		return;
	}
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
    wd_dnssd_register_socket = CFSocketCreateWithNative(NULL,
														DNSServiceRefSockFD(wd_dnssd_register_service),
														kCFSocketReadCallBack,
														&wd_server_dnssd_register_socket_callback,
														NULL);
	
	if(!wd_dnssd_register_socket) {
		wi_log_error(WI_STR("Could not create socket for DNS service discovery"));

		return;
	}
	
	CFSocketSetSocketFlags(wd_dnssd_register_socket,
		CFSocketGetSocketFlags(wd_dnssd_register_socket) & ~kCFSocketCloseOnInvalidate);
	
	wd_dnssd_register_source = CFSocketCreateRunLoopSource(NULL, wd_dnssd_register_socket, 0);

	if(!wd_dnssd_register_source) {
		wi_log_error(WI_STR("Could not create runloop source for DNS service discovery"));
		
		return;
	}
	
	wi_condition_lock_lock_when_condition(wd_cf_lock, 1, 0.0);
	CFRunLoopAddSource(wd_cf_runloop, wd_dnssd_register_source, kCFRunLoopCommonModes);
	wi_condition_lock_unlock(wd_cf_lock);
#endif
}



static void wd_server_dnssd_register_service_callback(DNSServiceRef client, DNSServiceFlags flags, DNSServiceErrorType error, const char *name, const char *regtype, const char *domain, void *context) {
	if(error == kDNSServiceErr_NoError) {
		wi_log_info(WI_STR("Registered using DNS service discovery as %s.%s%s"),
			name, regtype, domain);
	} else {
		wi_log_error(WI_STR("Could not register using DNS service discovery: %@"),
			wd_server_dnssd_error_string(error));
	}
}



#ifdef HAVE_CORESERVICES_CORESERVICES_H

static void wd_server_dnssd_register_socket_callback(CFSocketRef socket, CFSocketCallBackType type, CFDataRef address, const void *data, void *context) {
    DNSServiceErrorType		error;
	
	error = DNSServiceProcessResult(wd_dnssd_register_service);
	
	if(error != kDNSServiceErr_NoError) {
		wi_log_error(WI_STR("Could not process result for DNS service discovery: %@"),
			wd_server_dnssd_error_string(error));
	}
}

#endif

#endif



#pragma mark -

static void wd_server_portmap_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	
	pool = wi_pool_init(wi_pool_alloc());

	wd_portmap_map_natpmp();
	wd_portmap_map_upnp();
	
	wi_release(pool);
}



static void wd_server_portmap_timer(wi_timer_t *timer) {
	if(!wi_thread_create_thread(wd_server_portmap_thread, NULL))
		wi_log_fatal(WI_STR("Could not create a portmap thread: %m"));
}


#pragma mark -

static void wd_server_listen_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wi_socket_t			*socket;
	wi_address_t		*address;
	wi_string_t			*ip;
	
	pool = wi_pool_init(wi_pool_alloc());

	while(wd_running) {
		wi_pool_drain(pool);

		socket = wi_socket_accept_multiple(wd_tcp_sockets, 30.0, &address);

		if(!address) {
			wi_log_error(WI_STR("Could not accept a connection: %m"));
			
			continue;
		}
		
		ip = wi_address_string(address);
		
		if(!socket) {
			wi_log_error(WI_STR("Could not accept a connection for %@: %m"), ip);
			
			continue;
		}
		
		if(!wi_thread_create_thread(wd_server_accept_thread, socket))
			wi_log_error(WI_STR("Could not create a client thread for %@: %m"), ip);
	}
	
	wi_release(pool);
}



static void wd_server_accept_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wi_p7_socket_t		*p7_socket;
	wi_socket_t			*socket = argument;
	wi_string_t			*ip;
	wd_user_t			*user;
    wi_integer_t        cipher;
    wi_p7_options_t     options;
    
	pool = wi_pool_init(wi_pool_alloc());
	
	ip = wi_address_string(wi_socket_address(socket));
	
	wi_log_info(WI_STR("Connect from %@"), ip);

	if(!wi_socket_set_timeout(socket, 30.0)) 
		wi_log_error(WI_STR("Could not set timeout for %@: %m"), ip); 

	p7_socket = wi_autorelease(wi_p7_socket_init_with_socket(wi_p7_socket_alloc(), socket, wd_p7_spec));
	wi_p7_socket_set_private_key(p7_socket, wd_rsa);
	
    options = WI_P7_ALL;
	user = wd_user_with_p7_socket(p7_socket);
    cipher = wi_config_integer_for_name(wd_config, WI_STR("preferred cipher"));
    
    if(cipher != -1) {
        //wi_log_debug(WI_STR("Enforce preferred encryption cipher: %ld"), cipher);
        options = (WI_P7_COMPRESSION_DEFLATE | (1 << (cipher + 1)) | WI_P7_CHECKSUM_SHA1 | WI_P7_CHECKSUM_SHA256 | WI_P7_CHECKSUM_SHA512);
    }
    
	if(wi_p7_socket_accept(p7_socket, 30.0, options)) {
		if(wi_config_bool_for_name(wd_config, WI_STR("force encryption"))) {
#ifdef WI_RSA
			if(!WI_P7_ENCRYPTION_ENABLED(wi_p7_socket_options(p7_socket))) {
				wi_log_error(WI_STR("Could not accept a non-encrypted connection (have a look to 'force encryption' setting)"), ip);
				goto end;
			}
#endif	
		}

		wd_users_add_user(user);
		wd_messages_loop_for_user(user);
	} else {
		wd_user_set_login(user, wi_p7_socket_user_name(p7_socket));
		
		wi_log_error(WI_STR("Could not accept a P7 connection for %@: %m"), ip);

		if(wi_error_domain() == WI_ERROR_DOMAIN_LIBWIRED && wi_error_code() == WI_ERROR_P7_AUTHENTICATIONFAILED)
			wd_events_add_event(WI_STR("wired.event.user.login_failed"), user, NULL);
	}

end:
	wi_release(pool);
}



static void wd_server_receive_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wi_socket_t			*socket;
	wi_p7_message_t		*message, *reply;
	wi_address_t		*address;
	wi_string_t			*ip, *name;
	wi_data_t			*data;
	wi_cipher_t			*cipher;
	wd_server_t			*server;
	char				buffer[WI_SOCKET_BUFFER_SIZE];
	wi_integer_t		bytes;
	
	pool = wi_pool_init(wi_pool_alloc());

	while(wd_running) {
		wi_pool_drain(pool);

		bytes = wi_socket_recvfrom_multiple(wd_udp_sockets, buffer, sizeof(buffer), &address);
		
		if(!address) {
			wi_log_error(WI_STR("Could not receive data: %m"));

			continue;
		}
		
		ip = wi_address_string(address);

		if(bytes < 0) {
			wi_log_error(WI_STR("Could not receive data from %@: %m"), ip);

			continue;
		}
		
		server = wd_servers_server_for_ip(ip);
		
		if(!server) {
			wi_log_error(WI_STR("Could not receive data from %@: No server found"), ip);
			
			continue;
		}
		
		data		= wi_data_with_bytes(buffer, bytes);
		cipher		= wd_server_cipher(server);
		
		if(cipher) {
			data = wi_cipher_decrypt(cipher, data);
			
			if(!data) {
				wi_log_error(WI_STR("Could not decrypt data from %@: %m"), ip);

				continue;
			}
		}
		
		message = wi_p7_message_with_data(data, WI_P7_BINARY, wd_p7_spec);
		
		if(!message) {
			wi_log_error(WI_STR("Could not create message from received data from %@: %m"), ip);
			
			continue;
		}
		
		if(!wi_p7_spec_verify_message(wd_p7_spec, message)) {
			wi_log_error(WI_STR("Could not verify message from received data from %@: %m"), ip);
			
			continue;
		}
		
		reply		= NULL;
		name		= wi_p7_message_name(message);
		
		if(wi_is_equal(name, WI_STR("wired.tracker.send_update"))) {
			if(wi_config_bool_for_name(wd_config, WI_STR("enable tracker"))) {
				if(!wd_servers_update_server(ip, NULL, message)) {
					reply = wi_p7_message_with_name(WI_STR("wired.error"), wd_p7_spec);
					wi_p7_message_set_enum_name_for_name(reply, WI_STR("wired.error.not_registered"), WI_STR("wired.error"));
				}
			} else {
				reply = wi_p7_message_with_name(WI_STR("wired.error"), wd_p7_spec);
				wi_p7_message_set_enum_name_for_name(reply, WI_STR("wired.error.tracker_not_enabled"), WI_STR("wired.error"));
			}
		}
		else if(wi_is_equal(name, WI_STR("wired.error"))) {
			wd_trackers_register();
		}
		
		if(reply) {
			wi_address_set_port(address, wd_server_port(server));
			
			socket = wi_autorelease(wi_socket_init_with_address(wi_socket_alloc(), address, WI_SOCKET_UDP));
			
			if(!socket) {
				wi_log_error(WI_STR("Could not create a socket for %@: %m"), ip);
				
				continue;
			}
			
			data = wi_p7_message_data_with_serialization(reply, WI_P7_BINARY);
			
			if(cipher) {
				data = wi_cipher_encrypt(cipher, data);
			
				if(!data) {
					wi_log_error(WI_STR("Could not encrypt message for %@: %m"), ip);
					
					continue;
				}
			}
			
			bytes = wi_socket_sendto_data(socket, data);
			
			if(bytes < 0) {
				wi_log_error(WI_STR("Could not send message to \"%@\": %m"), ip);
				
				continue;
			}
		}
	}
	
	wi_release(pool);
}



static void wd_server_ping_users(wi_timer_t *timer) {
	wd_broadcast_message(wd_ping_message);
}



#pragma mark -

void wd_server_log_reply_log(wd_user_t *user, wi_p7_message_t *message) {
	wi_dictionary_t		*entry;
	wi_p7_message_t		*reply;
	wi_uinteger_t		i, count;
	
	wi_array_rdlock(wd_log_entries);

	count = wi_array_count(wd_log_entries);
	
	for(i = 0; i < count; i++) {
		entry = WI_ARRAY(wd_log_entries, i);
		
		reply = wi_p7_message_with_name(WI_STR("wired.log.list"), wd_p7_spec);
		wi_p7_message_set_date_for_name(reply, wi_dictionary_data_for_key(entry, WI_STR("wired.log.time")), WI_STR("wired.log.time"));
		wi_p7_message_set_number_for_name(reply, wi_dictionary_data_for_key(entry, WI_STR("wired.log.level")), WI_STR("wired.log.level"));
		wi_p7_message_set_string_for_name(reply, wi_dictionary_data_for_key(entry, WI_STR("wired.log.message")), WI_STR("wired.log.message"));
		
		wd_user_reply_message(user, reply, message);
	}
	
	wi_array_unlock(wd_log_entries);
	
	reply = wi_p7_message_with_name(WI_STR("wired.log.list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



static void wd_server_log_callback(wi_log_level_t level, wi_string_t *string) {
	wi_enumerator_t		*enumerator;
	wi_dictionary_t		*entry;
	wi_p7_message_t		*message;
	wi_date_t			*date;
	wd_user_t			*user;
	wi_uinteger_t		count;
	
	date	= wi_date();
	entry	= wi_dictionary_with_data_and_keys(
		date,					WI_STR("wired.log.time"),
		string,					WI_STR("wired.log.message"),
		WI_INT32(4 - level),	WI_STR("wired.log.level"),
		NULL);
	
	if(wi_array_trywrlock(wd_log_entries)) {
		count = wi_array_count(wd_log_entries);
		
		if(count > wd_max_log_entries + 100)
			wi_mutable_array_remove_data_in_range(wd_log_entries, wi_make_range(0, 100));
		
		wi_mutable_array_add_data(wd_log_entries, entry);
		wi_array_unlock(wd_log_entries);
		
		if(wd_users && wi_dictionary_tryrdlock(wd_users)) {
			enumerator = wi_dictionary_data_enumerator(wd_users);
			
			while((user = wi_enumerator_next_data(enumerator))) {
				if(wd_user_state(user) == WD_USER_LOGGED_IN && wd_user_is_subscribed_log(user)) {
					message = wi_p7_message_with_name(WI_STR("wired.log.message"), wd_p7_spec);
					wi_p7_message_set_date_for_name(message, date, WI_STR("wired.log.time"));
					wi_p7_message_set_enum_for_name(message, 4 - level, WI_STR("wired.log.level"));
					wi_p7_message_set_string_for_name(message, string, WI_STR("wired.log.message"));
					wd_user_send_message(user, message);
				}
			}
			
			wi_dictionary_unlock(wd_users);
		}
	}
}



#pragma mark -

wi_p7_message_t * wd_user_read_message(wd_user_t *user, wi_time_interval_t timeout) {
	wi_p7_message_t		*message;
	
	wd_user_lock_socket(user);
	
	message = wi_p7_socket_read_message(wd_user_p7_socket(user), timeout);
	
	wd_user_unlock_socket(user);
	
	return message;
}



wi_boolean_t wd_user_write_message(wd_user_t *user, wi_time_interval_t timeout, wi_p7_message_t *message) {
	wi_boolean_t		result;
	
	wd_user_lock_socket(user);
	
	result = wi_p7_socket_write_message(wd_user_p7_socket(user), timeout, message);
	
	wd_user_unlock_socket(user);
	
	return result;
}



#pragma mark -

void wd_user_send_message(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_user_transfer(user)) {
		if(!wd_user_write_message(user, 0.0, message)) {
			wd_user_set_state(user, WD_USER_DISCONNECTED);
			
			wi_log_error(WI_STR("Could not write message \"%@\" to %@: %m"),
				wi_p7_message_name(message), wd_user_identifier(user));
		}
	}
}



void wd_user_reply_message(wd_user_t *user, wi_p7_message_t *reply, wi_p7_message_t *message) {
	wi_p7_uint32_t	transaction;
	
	if(reply != message) {
		if(wi_p7_message_get_uint32_for_name(message, &transaction, WI_STR("wired.transaction")))
			wi_p7_message_set_uint32_for_name(reply, transaction, WI_STR("wired.transaction"));
	}
	
	wd_user_send_message(user, reply);
}



void wd_user_reply_okay(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.okay"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



void wd_user_reply_error(wd_user_t *user, wi_string_t *error, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.error"), wd_p7_spec);
	wi_p7_message_set_enum_name_for_name(reply, error, WI_STR("wired.error"));
	wd_user_reply_message(user, reply, message);
}



void wd_user_reply_file_errno(wd_user_t *user, wi_p7_message_t *message) {
	int		code;
	
	code = wi_error_code();

	if(wi_error_domain() == WI_ERROR_DOMAIN_ERRNO && (code == ENOENT || code == EEXIST)) {
		if(code == ENOENT)
			wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);
		else if(code == EEXIST)
			wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);
	} else {
		wd_user_reply_internal_error(user, wi_error_string(), message);
	}
}



void wd_user_reply_internal_error(wd_user_t *user, wi_string_t *string, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.error"), wd_p7_spec);
	wi_p7_message_set_enum_name_for_name(reply, WI_STR("wired.error.internal_error"), WI_STR("wired.error"));
	
	if(string)
		wi_p7_message_set_string_for_name(reply, string, WI_STR("wired.error.string"));
	
	wd_user_reply_message(user, reply, message);
}



void wd_broadcast_message(wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_send_message(user, message);
	}
	
	wi_dictionary_unlock(wd_users);
}


void wd_chat_broadcast_message(wd_chat_t *chat, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_array_t			*users;
	wd_user_t			*user;
	
	users = wd_chat_users(chat);
	
	wi_array_rdlock(users);

	enumerator = wi_array_data_enumerator(users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_send_message(user, message);
	}
	
	wi_array_unlock(users);
}

#pragma mark -

static void wd_server_database_snapshot_register_with_timer(wi_timer_t *timer) {
    wi_sqlite3_snapshot_database_at_path(wd_database, WI_STR("database.sqlite3.bak"));
}


