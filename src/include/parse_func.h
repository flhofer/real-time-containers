/*
 * parse_func.h
 *
 *  Created on: Dec 31, 2019
 *      Author: Florian Hofer
 */

#include <json-c/json.h>	// libjson-c for parsing
#include "error.h"		// error and stderr print functions

// inline parse function for libson-c

#ifndef _PARSE_FUNC_H_
	#define _PARSE_FUNC_H_

	#ifndef PFX
		#define PFX "[default] "
	#endif

	#ifndef PFL
		#define PFL "         "PFX
		#define PIN PFX"    "
		#define PIN2 PIN"    "
		#define PIN3 PIN2"    "
	#endif

	/* this macro set a default if key not present, or give an error and exit
	 * if key is present but does not have a default */
	#define set_default_if_needed(key, value, have_def, def_value) do {	\
		if (!value) {							\
			if (have_def) {						\
				printDbg(PIN "key: %s <default> %d\n", key, def_value);\
				return def_value;				\
			} else {						\
				err_msg(PFX "Key %s not found", key);	\
				exit(EXIT_INV_CONFIG);	\
			}							\
		}								\
	} while(0)

	/* same as before, but for string, for which we need to strdup in the
	 * default value so it can be a literal */
	#define set_default_if_needed_str(key, value, have_def, def_value) do {	\
		if (!value) {							\
			if (have_def) {						\
				if (!def_value) {				\
					printDbg(PIN "key: %s <default> NULL\n", key);\
					return NULL;						\
				}										\
				printDbg(PIN "key: %s <default> %s\n",		\
					  key, def_value);					\
				return strdup(def_value);				\
			} else {									\
				err_msg(PFX "Key %s not found", key);	\
				exit(EXIT_INV_CONFIG);					\
			}											\
		}												\
	}while (0)

	/* get an object obj and check if its type is <type>. If not, print a message
	 * (this is what parent and key are used for) and exit
	 */
	static inline void
	assure_type_is(struct json_object *obj,
			   struct json_object *parent,
			   const char *key,
			   enum json_type type)
	{
		if (!json_object_is_type(obj, type)) {
			err_msg("Invalid type for key %s", key);
			err_msg("%s", json_object_to_json_string(parent));
			exit(EXIT_INV_CONFIG);
		}
	}

	/* search a key (what) in object "where", and return a pointer to its
	 * associated object. If null-able is false, exit if key is not found */
	static inline struct json_object*
	get_in_object(struct json_object *where,
			  const char *what,
			  int nullable)
	{
		struct json_object *to;
		json_bool ret;
		ret = json_object_object_get_ex(where, what, &to);
		if (!nullable && !ret){
			err_msg(PFX "Error while parsing config\n" PFL);
			exit(EXIT_INV_CONFIG);
		}


		if (!nullable && strcmp(json_object_to_json_string(to), "null") == 0) {
			err_msg(PFX "Cannot find key %s", what);
			exit(EXIT_INV_CONFIG);
		}

		return to;
	}

	static inline int
	get_int_value_from(struct json_object *where,
			   const char *key,
			   int have_def,
			   int def_value)
	{
		struct json_object *value;
		int i_value;
		value = get_in_object(where, key, have_def);
		set_default_if_needed(key, value, have_def, def_value);
		assure_type_is(value, where, key, json_type_int);
		i_value = json_object_get_int(value);
		printDbg(PIN "key: %s, value: %d, type <int>\n", key, i_value);
		return i_value;
	}

	static inline int
	get_bool_value_from(struct json_object *where,
				const char *key,
				int have_def,
				int def_value)
	{
		struct json_object *value;
		int b_value;
		value = get_in_object(where, key, have_def);
		set_default_if_needed(key, value, have_def, def_value);
		assure_type_is(value, where, key, json_type_boolean);
		b_value = json_object_get_boolean(value);
		printDbg(PIN "key: %s, value: %d, type <bool>\n", key, b_value);
		return b_value;
	}

	static inline char*
	get_string_value_from(struct json_object *where,
				  const char *key,
				  int have_def,
				  const char *def_value)
	{
		struct json_object *value;
		char *s_value;
		value = get_in_object(where, key, have_def);
		set_default_if_needed_str(key, value, have_def, def_value);
		if (json_object_is_type(value, json_type_null)) {
			printDbg(PIN "key: %s, value: NULL, type <string>\n", key);
			return NULL;
		}
		assure_type_is(value, where, key, json_type_string);
		s_value = strdup(json_object_get_string(value));
		printDbg(PIN "key: %s, value: %s, type <string>\n", key, s_value);
		return s_value;
	}

	static inline int64_t
	get_int64_value_from(struct json_object *where,
			   const char *key,
			   int have_def,
			   int def_value)
	{
		struct json_object *value;
		int64_t i_value;
		value = get_in_object(where, key, have_def);
		set_default_if_needed(key, value, have_def, def_value);
		assure_type_is(value, where, key, json_type_int);
		i_value = json_object_get_int64(value);
		printDbg(PIN "key: %s, value: %ld, type <int64>\n", key, i_value);
		return i_value;
	}

#endif /* _PARSE_FUNC_H_ */
