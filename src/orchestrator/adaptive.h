/*
 * adaptive.h
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */
#include "orchdata.h"

#ifndef __ADAPTIVE_H_
	#define __ADAPTIVE_H_

	void adaptPrepareSchedule();
	void adaptScramble();
	void adaptExecute();
	struct resTracer * adaptGetTracers();
	void adaptFreeTracer();

#endif /* _ADAPTIVE_H_ */
