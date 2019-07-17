// static library includes
#include "orchdata.h"		// orchestrator data structures and types

#ifndef _PARSE_CONFIG_H
	#define _PARSE_CONFIG_H 

	void parse_config_set_default(prgset_t *set);
	void parse_config_file(const char *filename, prgset_t *set, containers_t *parm);
	void parse_config_stdin(prgset_t *set, containers_t *parm);

#endif // _PARSE_CONFIG_H
