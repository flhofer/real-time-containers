/*
 * runstats.h
 *
 *  Created on: Apr 8, 2020
 *      Author: Florian Hofer
 */

#ifndef RUNSTATS_H_
#define RUNSTATS_H_

#include <gsl/gsl_histogram.h>
#include <gsl/gsl_vector.h>

// types to abstract and export information
typedef gsl_histogram stat_hist;
typedef gsl_vector stat_param;
struct stat_data
	{
		double *t;
		double *y;
		size_t n;
	};


int runstats_initparam(stat_param ** x, double b);	// init parameter vector
int runstats_inithist(stat_hist ** h, double b);	// init histogram data structure

int runstats_solvehist(stat_hist * h, stat_param * x);
													// fit model (gaussian) to histogram
int runstats_verifyparam(stat_hist * h, stat_param * x);
													// verify parameter and histogram areas match
double runstats_shapehist(stat_hist * h, double b);	// shape value to histogram borders
int runstats_addhist(stat_hist * h, double b);		// add value to histogram
int runstats_checkhist(stat_hist * h);				// check prepared for fitting

int runstats_fithist(stat_hist **h);				// fit histogram bins

int runstats_mdlpdf(stat_param * x, double a,		// compute integral from a to b, to get probability p
		double b, double * p, double * error);

int runstats_mdlUpb(stat_param * x, double a,		// compute upper bound b that obtains probability p
		double * b, double p, double bmin,
		double bmax, double * error);

double runstats_gaussian(const double a, const double b,
		const double c, const double t);

int runstats_printparam(stat_param * x, char * str, size_t len);
													// "print" parameters to buffer

void runstats_freeparam(stat_param * x);
void runstats_freehist(stat_hist * h);

#endif /* RUNSTATS_H_ */
