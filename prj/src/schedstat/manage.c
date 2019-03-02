#include "schedstat.h"
#include "manage.h"

/* Function realloc_it() is a wrapper function for standard realloc()
 * with one difference - it frees old memory pointer in case of realloc
 * failure. Thus, DO NOT use old data pointer in anyway after call to
 * realloc_it(). If your code has some kind of fallback algorithm if
 * memory can't be re-allocated - use standard realloc() instead.
 */
static inline void *realloc_it(void *ptrmem, size_t size) {
	void *p = realloc(ptrmem, size);
	if (!p)  {
		free (ptrmem);
		fprintf(stderr, "realloc(): errno=%d\n", errno);
	}
	return p;
}

static int dump(const char *js, jsmntok_t *t, size_t count, int indent) {
	int i, j, k;
	if (count == 0) {
		return 0;
	}
	if (t->type == JSMN_PRIMITIVE) {
		printf("%.*s", t->end - t->start, js+t->start);
		return 1;
	} else if (t->type == JSMN_STRING) {
		printf("'%.*s'", t->end - t->start, js+t->start);
		return 1;
	} else if (t->type == JSMN_OBJECT) {



		printf("\n");
		j = 0;
		for (i = 0; i < t->size; i++) {
			for (k = 0; k < indent; k++) printf("  ");
			j += dump(js, t+1+j, count-j, indent+1);
			printf(": ");
			j += dump(js, t+1+j, count-j, indent+1);
			printf("\n");
		}
		return j+1;


	} else if (t->type == JSMN_ARRAY) {
		j = 0;
		printf("\n");
		for (i = 0; i < t->size; i++) {
			for (k = 0; k < indent-1; k++) printf("  ");
			printf("   - ");
			j += dump(js, t+1+j, count-j, indent+1);
			printf("\n");
		}
		return j+1;
	}
	return 0;
}

/// readParams(): Read parameters from json file, for static params
///
/// Arguments: - 
///
/// Return value: Code - o for no error - EXIT_SUCCESS
/// thing is 
int readParams() {
	int r;
	int eof_expected = 0; // ok to have end of file
	char *js = NULL;
	size_t jslen = 0;
	char buf[BUFSIZ];

	jsmn_parser p;
	jsmntok_t *tok;
	size_t tokcount = 2;

	/* Prepare parser */
	jsmn_init(&p);

	/* Allocate some tokens as a start */
	tok = malloc(sizeof(*tok) * tokcount);
	if (tok == NULL) {
		fprintf(stderr, "malloc(): errno=%d\n", errno);
		return 3;
	}

	FILE * f = fopen ("config.json", "r");

	if (f){
	for (;;) {
		/* Read another chunk */
		r = fread(buf, 1, sizeof(buf), f);
		if (r < 0) {
			fprintf(stderr, "fread(): %d, errno=%d\n", r, errno);
			return 1;
		}
		if (r == 0) {
			if (eof_expected != 0) {
				return 0;
			} else {
				fprintf(stderr, "fread(): unexpected EOF\n");
				return 2;
			}
		}

		js = realloc_it(js, jslen + r + 1);
		if (js == NULL) {
			return 3;
		}
		strncpy(js + jslen, buf, r);
		jslen = jslen + r;

again:
		r = jsmn_parse(&p, js, jslen, tok, tokcount);
		if (r < 0) {
			if (r == JSMN_ERROR_NOMEM) {
				tokcount = tokcount * 2;
				tok = realloc_it(tok, sizeof(*tok) * tokcount);
				if (tok == NULL) {
					return 3;
				}
				goto again;
			}
		} else {
			dump(js, tok, p.toknext, 0);
			eof_expected = 1;
		}
	}}
	else{
		printf("Dada\n");
	}

	fclose (f);

	return 0;
}

/// updateSched(): main function called to verify running schedule
//
/// Arguments: 
///
/// Return value: N/D
///
int manageSched(){
	uint64_t cputimes[MAX_CPUS] = {}; 
	uint64_t cpuperiod[MAX_CPUS] = {}; 
	cpu_set_t cset;

	// zero cpu-set, static size set
	CPU_ZERO(&cset);
	CPU_SET(0, &cset);

	pthread_mutex_lock(&dataMutex);

    node_t * current = head;
	while (current != NULL) {
		// get schedule of new pids
		if (current->attr.size == 0) {
			struct timespec tt;
			
			int ret = sched_rr_get_interval(current->pid, &tt);
			printDbg("Schedule pid %d: %d %ld\n", current->pid, ret, tt.tv_nsec);

			ret = sched_getattr (current->pid, &(current->attr), sizeof(node_t), 0U);
			printDbg("Attr: %d %d\n", ret, current->attr.sched_policy);

			ret = sched_setaffinity(current->pid, sizeof(cset), &cset );
			if (ret == 0)
				printDbg("Pid %d reassigned to CPU0\n", current->pid);

			// TODO: ret value evaluation 
		}

		// affinity not set?? default is 0, affinity of system stuff

		// sum of cpu-times, affinity is only 1 cpu here
		cputimes[current->affinity] += current->attr.sched_deadline;
		cpuperiod[current->affinity] += current->attr.sched_deadline;

        current = current->next;
    }


	pthread_mutex_unlock(&dataMutex);
}

/// thread_manage(): thread function call to manage schedule list
///
/// Arguments: - thread state/state machine, passed on to allow main thread stop
///
/// Return value: Exit Code - o for no error - EXIT_SUCCESS
void *thread_manage (void *arg)
{
	int32_t* pthread_state = arg;
	// initialize the thread locals
	while(1)
	{
	  switch( *pthread_state )
	  {
	  case 0: // setup thread
		if (readParams() == 99)
			pthread_state++;

	  case 1: // normal thread loop
		manageSched();
		break;
	  case -1:
		// tidy or whatever is necessary
		pthread_exit(0); // exit the thread signalling normal return
		break;
	  case 2: //
		// do something special
		
		break;
	  }
	  sleep(1);
	}
}

