/*
 * runstats.h
 *
 *  Created on: Apr 8, 2020
 *      Author: Florian Hofer
 */

#ifndef RUNSTATS_H_
#define RUNSTATS_H_

#include <stdint.h>
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_vector.h>

#define STARTBINS 30		// default bin number
#define BIN_DEFMIN 0.70		// default range: - offset * x
#define BIN_DEFMAX 1.30 	// default range: + offset * x
#define BIN_OUTMAX 0.10		// maximum fraction outside the precision range
#define BIN_NEWMARGIN 0.05	// margin around resampled outside values

#define MODEL_DEFAMP 1/(sqrt(2*M_PI)*b*MODEL_DEFSTD)	// default model amplitude
#define MODEL_DEFOFS 1.02	// default model offset: runtime (b) * x
#define MODEL_DEFSTD 0.01	// default model stddev: runtime (b) * x

// types to abstract and export information
typedef gsl_histogram stat_hist;
typedef gsl_vector stat_param;
typedef gsl_histogram_pdf stat_cdf;

typedef struct stat_scope {
	uint64_t samples;			// total amout of samples
	uint64_t underflows;
	uint64_t overflows;
	double sum;					// total value sum of all samples
	double sum_squared;			// total squared value sum of all samples
	double underflow_sum;
	double overflow_sum;
	double min;					// minimum value of all samples
	double max;					// maximum value of all samples
} stat_scope;

struct stat_data
	{
		double *t;
		double *y;
		size_t n;
	};


int runstats_paramInit(stat_param ** x, double b);	// init parameter vector
int runstats_paramVerify(stat_hist * h, stat_param * x);
													// verify parameter and histogram areas match
int runstats_paramPrint(stat_param * x, char * str, size_t len);
													// "print" parameters to buffer
void runstats_paramFree(stat_param * x);			// free parameter vector

int runstats_histInit(stat_hist ** h, double b);	// init histogram data structure
int runstats_histSolve(stat_hist * h, stat_param * x);
													// fit model (gaussian) to histogram
int runstats_histAdd(stat_hist * h, stat_scope * scope, double b);	
													// shape value to histogram borders
int runstats_histCheck(stat_hist * h, const stat_scope * scope);
													// check prepared for fitting
double runstats_histMean(const stat_hist * h, const stat_scope * scope);
													// get the mean of the PD
int runstats_histFit(stat_hist **h, const stat_scope * scope);
													// fit histogram bins
int runstats_histResample(stat_hist **h, stat_scope * scope, double percentile);
													// resample histogram if scope is insufficient
double runstats_histSixSigma(const stat_hist * h, const stat_scope * scope);
													// compute six-sigma probability time value (LSS 99.996% on normal dist)
void runstats_scopeReset(stat_scope * scope);		// reset scope information of pdf
void runstats_histFree(stat_hist * h);				// free histrogram structure

int runstats_mdlpdf(stat_param * x, double a,		// compute integral from a to b, to get probability p
		double b, double * p, double * error);

int runstats_mdlUpb(stat_param * x, double a,		// compute upper bound b that obtains probability p
		double * b, double p, double * error);

int runstats_cdfCreate(stat_hist **h, stat_cdf **c);// transfer histogram data to CDF and resort histogram
double runstats_cdfSample(const stat_cdf * c,
		const stat_scope * scope, double r);		// compute time from CDF value
void runstats_cdfFree(stat_cdf ** c);				// CDF free

double runstats_gaussian(const double a, const double b,
		const double c, const double t);

#endif /* RUNSTATS_H_ */
