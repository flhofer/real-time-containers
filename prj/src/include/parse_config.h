// static library includes
#include "orchdata.h"		// orchestrator data structures and types

#ifndef _PARSE_CONFIG_H
	#define _PARSE_CONFIG_H 

	void config_set_default(prgset_t *set);
	void parse_config(const char *filename, prgset_t *set, contparm_t *parm);

#endif // _PARSE_CONFIG_H
