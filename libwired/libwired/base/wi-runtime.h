/* $Id$ */

/*
 *  Copyright (c) 2005-2009 Axel Andersson
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

#ifndef WI_RUNTIME_H
#define WI_RUNTIME_H

#include <wired/wi-base.h>

typedef void							wi_runtime_instance_t;

enum {
	WI_RUNTIME_ID_NULL					= 0
};
typedef uint16_t						wi_runtime_id_t;

enum {
	WI_RUNTIME_OPTION_ZOMBIE			= (1 << 0),
	WI_RUNTIME_OPTION_IMMUTABLE			= (1 << 1),
	WI_RUNTIME_OPTION_MUTABLE			= (1 << 2)
};


typedef void							wi_dealloc_func_t(wi_runtime_instance_t *);
typedef wi_runtime_instance_t *			wi_copy_func_t(wi_runtime_instance_t *);
typedef wi_boolean_t					wi_is_equal_func_t(wi_runtime_instance_t *, wi_runtime_instance_t *);
typedef wi_string_t *					wi_description_func_t(wi_runtime_instance_t *);
typedef wi_hash_code_t					wi_hash_func_t(wi_runtime_instance_t *);

typedef wi_runtime_instance_t *			wi_retain_func_t(wi_runtime_instance_t *);
typedef void							wi_release_func_t(wi_runtime_instance_t *);
typedef wi_integer_t					wi_compare_func_t(wi_runtime_instance_t *, wi_runtime_instance_t *);

struct _wi_runtime_class {
	const char							*name;
	wi_dealloc_func_t					*dealloc;
	wi_copy_func_t						*copy;
	wi_is_equal_func_t					*is_equal;
	wi_description_func_t				*description;
	wi_hash_func_t						*hash;
};
typedef struct _wi_runtime_class		wi_runtime_class_t;


struct _wi_runtime_base {
	uint32_t							magic;
	wi_runtime_id_t						id;
	uint16_t							retain_count;
	uint8_t								options;
};
typedef struct _wi_runtime_base			wi_runtime_base_t;


WI_EXPORT wi_runtime_id_t				wi_runtime_register_class(wi_runtime_class_t *);
WI_EXPORT wi_runtime_instance_t *		wi_runtime_create_instance(wi_runtime_id_t, size_t);
WI_EXPORT wi_runtime_instance_t *		wi_runtime_create_instance_with_options(wi_runtime_id_t, size_t, uint8_t);

WI_EXPORT wi_runtime_class_t *			wi_runtime_class_with_name(wi_string_t *);
WI_EXPORT wi_runtime_class_t *			wi_runtime_class_with_id(wi_runtime_id_t);
WI_EXPORT wi_runtime_id_t				wi_runtime_id_for_class(wi_runtime_class_t *);

WI_EXPORT wi_runtime_class_t *			wi_runtime_class(wi_runtime_instance_t *);
WI_EXPORT wi_string_t *					wi_runtime_class_name(wi_runtime_instance_t *);
WI_EXPORT wi_runtime_id_t				wi_runtime_id(wi_runtime_instance_t *);
WI_EXPORT uint8_t						wi_runtime_options(wi_runtime_instance_t *);

WI_EXPORT wi_runtime_instance_t * 		wi_retain(wi_runtime_instance_t *);
WI_EXPORT uint16_t						wi_retain_count(wi_runtime_instance_t *);
WI_EXPORT void							wi_release(wi_runtime_instance_t *);

WI_EXPORT wi_runtime_instance_t *		wi_copy(wi_runtime_instance_t *);
WI_EXPORT wi_runtime_instance_t *		wi_mutable_copy(wi_runtime_instance_t *);
WI_EXPORT wi_boolean_t					wi_is_equal(wi_runtime_instance_t *, wi_runtime_instance_t *);
WI_EXPORT wi_string_t *					wi_description(wi_runtime_instance_t *);
WI_EXPORT wi_hash_code_t				wi_hash(wi_runtime_instance_t *);

WI_EXPORT void							wi_show(wi_runtime_instance_t *);


WI_EXPORT wi_boolean_t					wi_zombie_enabled;

#endif /* WI_RUNTIME_H */
