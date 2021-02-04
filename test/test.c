/* 
###############################
# test runner main script by Florian Hofer
# last change: 25/07/2019
# ©2019 all rights reserved ☺
###############################
*/
#include "test.h"

// ############################ common global variables ###########################333

// debug output file
FILE  * dbg_out;

containers_t * contparm; // container parameter settings
prgset_t * prgset; // program settings structure

// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t dataMutex;

// local head of PID list - PID runtime and configuration details
node_t * nhead = NULL;

// mutex to avoid read while updater fills or empties existing threads
pthread_mutex_t resMutex; // UNUSED for now
// heads of resource allocations for CPU and Tasks
resTracer_t * rHead = NULL;

// ############################ end common global variables ###########################333

// include library and main suite code
#include "lib/library_suite.h"
#include "orchestrator/orchestrator_suite.h"

#include <time.h>

int main(void)
{
	// init pseudo-random tables
	srand(time(NULL));

    int nf=0;
    SRunner *sr;

	Suite * s1 = library_suite();
	Suite * s2 = orchestrator_suite();
	sr = srunner_create(s1);
	srunner_add_suite(sr, s2);
	srunner_run_all(sr, CK_NORMAL);
    nf = srunner_ntests_failed(sr);
    srunner_free(sr);

    return nf == 0 ? 0 : 1;
}


