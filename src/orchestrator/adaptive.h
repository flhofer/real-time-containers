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
	void adaptPlanSchedule();
	void adaptScramble();
	void adaptExecute();
	void adaptFree();

#endif /* ADAPTIVE_H_ */
