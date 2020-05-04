/*
 * adaptive.h
 *
 *  Created on: Feb 10, 2020
 *      Author: Florian Hofer
 */
#include "orchdata.h"

#ifndef ADAPTIVE_H_
	#define ADAPTIVE_H_

	void adaptPrepareSchedule();
	void adaptScramble();
	void adaptExecute();
	struct resTracer * adaptGetTracers();
	void adaptFreeTracer();

#endif /* ADAPTIVE_H_ */
