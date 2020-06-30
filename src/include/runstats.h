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
typedef gsl_histogram_pdf stat_cdf;

struct stat_data
	{
		double *t;
		double *y;
		size_t n;
	};


int runstats_paramInit(stat_param ** x, double b);	// init parameter vector
int runstats_histInit(stat_hist ** h, double b);	// init histogram data structure

int runstats_histSolve(stat_hist * h, stat_param * x);
													// fit model (gaussian) to histogram
int runstats_paramVerify(stat_hist * h, stat_param * x);
													// verify parameter and histogram areas match
double runstats_histShape(stat_hist * h, double b);	// shape value to histogram borders
int runstats_histAdd(stat_hist * h, double b);		// add value to histogram
int runstats_histCheck(stat_hist * h);				// check prepared for fitting

int runstats_histFit(stat_hist **h);				// fit histogram bins

int runstats_mdlpdf(stat_param * x, double a,		// compute integral from a to b, to get probability p
		double b, double * p, double * error);

int runstats_mdlUpb(stat_param * x, double a,		// compute upper bound b that obtains probability p
		double * b, double p, double * error);

int runstats_cdfCreate(stat_hist **h, stat_cdf **c);// transfer histogram data to CDF and resort histogram

double runstats_cdfsample(const stat_cdf * c, double r);
													// compute time from CDF value

void runstats_cdffree(stat_cdf ** c);				// CDF free

double runstats_gaussian(const double a, const double b,
		const double c, const double t);

int runstats_paramPrint(stat_param * x, char * str, size_t len);
													// "print" parameters to buffer

void runstats_paramFree(stat_param * x);
void runstats_histFree(stat_hist * h);

#endif /* RUNSTATS_H_ */
