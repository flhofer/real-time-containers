/*
 * runstats.c
 *
 *  Created on: Apr 8, 2020
 *      Author: Florian Hofer
 *
 *  initial source taken from https://www.gnu.org/software/gsl/doc/html/nls.html#weighted-nonlinear-least-squares
 */

#include "runstats.h"

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_multifit_nlinear.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <gsl/gsl_histogram.h>
#include <gsl/gsl_integration.h>
#include <gsl/gsl_roots.h>
// for MUSL based systems
#ifndef _STRUCT_TIMESPEC
	#include <linux/time.h>
#endif

#include <errno.h>			// system error management (LIBC)
#include <string.h>			// strerror print

#include "error.h"		// error print definitions
#include "cmnutil.h"	// general definitions

#undef PFX
#define PFX "[runstats] "

#define FIT_NUMITR	20		// trust region number of iterations originally set to 200
#define FIT_XTOL	1e-5 	// fitting tolerance step, originally set to -8
#define FIT_GTOL	1e-5	// fitting tolerance gradient, originally set to -8
#define FIT_FTOL	1e-5	// fitting tolerance originally set to -8

#define SAMP_MINCNT 50.0	// Minimum number of samples in histogram

#define INT_NUMITER	20		// Number of iterations max for fitting
#define INT_EPSABS	0		// max error absolute value (p)
#define INT_EPSREL	1e-7	// max error relative value min (a|b)

#define ROOT_NUMITR	20		// number of iterations max
#define ROOT_EPSABS	0		// max error absolute value (p)
#define ROOT_EPSREL	1e-7	// max error relative value min (a|b)

#define STARTBINS 30		// default bin number
#define BIN_DEFMIN 0.70		// default range: - offset * x
#define BIN_DEFMAX 1.30 	// default range: + offset * x

#define MODEL_DEFAMP 1/(sqrt(2*M_PI)*b*MODEL_DEFSTD)	// default model amplitude
#define MODEL_DEFOFS 1.02	// default model offset: runtime (b) * x
#define MODEL_DEFSTD 0.01	// default model stddev: runtime (b) * x

const size_t num_par = 3;   /* number of model parameters, = polynomial or function size */

struct func_integmdl_par {
	stat_param * x;
	double a;
	double p;
	double *error;
};

static double func_integmdl (double b, void * params)
{
  struct func_integmdl_par * p = (struct func_integmdl_par *)params;
  stat_param * x = (p->x);
  double a = (p->a);
  double pdes = (p->p);
  double r;
  double * error = (p->error);

  if (runstats_mdlpdf(x, a, b, &r, error))
		  return 0; // stops immediately

  return r-pdes; // 0 = f(x) - desiredP, the closer the better
}

/*
 *  func_gaussian(): function to calculate integral for Gaussian fit
 *
 *  Arguments: - model fitting parameter vector
 * 			   - function data points, f(t) = y (real)
 * 			   - error/residual vector
 *
 *  Return value: status, success or error in computing value
 */
static double
func_gaussian (double x, void * params)
{
	double a = gsl_vector_get(params, 0);
	double b = gsl_vector_get(params, 1);
	double c = gsl_vector_get(params, 2);

	return runstats_gaussian(a, b, c, x);
}

/*
 *  func_f(): function to calculate fitting for, Gaussian fit
 *
 *  Arguments: - model fitting parameter vector
 * 			   - function data points, f(t) = y (real)
 * 			   - error/residual vector
 *
 *  Return value: status, success or error in computing value
 */
static int
func_f (const gsl_vector * x, void *params, gsl_vector * f)
{
	struct stat_data *d = (struct stat_data *) params;
	double a = gsl_vector_get(x, 0);
	double b = gsl_vector_get(x, 1);
	double c = gsl_vector_get(x, 2);
	size_t i;

	for (i = 0; i < d->n; ++i)
	{
	  double ti = d->t[i];
	  double yi = d->y[i];
	  double y = runstats_gaussian(a, b, c, ti);

	  gsl_vector_set(f, i, yi - y);
	}

	return GSL_SUCCESS;
}

/*
 *  func_df(): function to calculate differential vector, Gaussian fit
 *
 *  Arguments: - model fitting parameter vector
 * 			   - function data points, f(t) = y (real)
 * 			   - Jacobian matrix, first derivatives
 *
 *  Return value: status, success or error in computing value
 */
static int
func_df (const gsl_vector * x, void *params, gsl_matrix * J)
{
	struct stat_data *d = (struct stat_data *) params;
	double a = gsl_vector_get(x, 0);
	double b = gsl_vector_get(x, 1);
	double c = gsl_vector_get(x, 2);
	size_t i;

	for (i = 0; i < d->n; ++i)
	{
	  double ti = d->t[i];
	  double zi = (ti - b) / c;
	  double ei = exp(-0.5 * zi * zi);

	  gsl_matrix_set(J, i, 0, -ei);
	  gsl_matrix_set(J, i, 1, -(a / c) * ei * zi);
	  gsl_matrix_set(J, i, 2, -(a / c) * ei * zi * zi);
	}

	return GSL_SUCCESS;
}

/*
 *  func_fvv(): function to calculate quadratic error, Gaussian fit
 *
 *  Arguments: - model fitting parameter vector
 * 			   - velocity vector
 * 			   - function data points, f(t) = y (real)
 * 			   - d2/dx directional for geodesic acceleration
 *
 *  Return value: status, success or error in computing value
 */
static int
func_fvv (const gsl_vector * x, const gsl_vector * v,
          void *params, gsl_vector * fvv)
{
	struct stat_data *d = (struct stat_data *) params;
	double a = gsl_vector_get(x, 0);
	double b = gsl_vector_get(x, 1);
	double c = gsl_vector_get(x, 2);
	double va = gsl_vector_get(v, 0);
	double vb = gsl_vector_get(v, 1);
	double vc = gsl_vector_get(v, 2);
	size_t i;

	for (i = 0; i < d->n; ++i)
	{
	  double ti = d->t[i];
	  double zi = (ti - b) / c;
	  double ei = exp(-0.5 * zi * zi);
	  double Dab = -zi * ei / c;
	  double Dac = -zi * zi * ei / c;
	  double Dbb = a * ei / (c * c) * (1.0 - zi*zi);
	  double Dbc = a * zi * ei / (c * c) * (2.0 - zi*zi);
	  double Dcc = a * zi * zi * ei / (c * c) * (3.0 - zi*zi);
	  double sum;

	  sum = 2.0 * va * vb * Dab +
			2.0 * va * vc * Dac +
				  vb * vb * Dbb +
			2.0 * vb * vc * Dbc +
				  vc * vc * Dcc;

	  gsl_vector_set(fvv, i, sum);
	}

	return GSL_SUCCESS;
}

/*
 *  callback(): callback function for the fitting iteration, Gaussian fit
 *  			Used for printing only
 *
 *  Arguments: - iteration number
 * 			   - function data points, f(t) = y (real)
 * 			   - GSL workspace reference
 *
 *  Return value: - none
 */
#ifdef DEBUG
static void
callback(const size_t iter, void *params,
         const gsl_multifit_nlinear_workspace *w)
{
	gsl_vector *f = gsl_multifit_nlinear_residual(w);
	gsl_vector *x = gsl_multifit_nlinear_position(w);
	double avratio = gsl_multifit_nlinear_avratio(w);
	double rcond;

	(void) params; /* not used */

	/* compute reciprocal condition number of J(x) */
	int ret;
	if ((ret = gsl_multifit_nlinear_rcond(&rcond, w)))
		err_msg("failure in Jacobian computation: %s", gsl_strerror(ret));

	printDbg(PFX "iter %2zu: a = %.4f, b = %.4f, c = %.4f, |a|/|v| = %.4f cond(J) = %8.4f, |f(x)| = %.4f\n",
		  iter,
		  gsl_vector_get(x, 0),
		  gsl_vector_get(x, 1),
		  gsl_vector_get(x, 2),
		  avratio,
		  1.0 / rcond,
		  gsl_blas_dnrm2(f));
}
#endif

/*
 *  resamplehist(): resample/scale histogram to sum up to 1 (uniform)
 *
 *  Arguments: - pointer holding the histogram pointer
 *
 *  Return value: - status, success or error in computing value
 */
static int
resamplehist (stat_hist *h){

	double a,b;

	if (gsl_histogram_get_range(h, 0, &a, &b))
		return GSL_EINVAL;

	double scale =  1/MAX(gsl_histogram_sum(h)*(b-a), 100);

	return gsl_histogram_scale(h,scale);
}

/*
 *  solve_system(): solver setup and parameters, Gaussian fit
 *
 *  Arguments: - model fitting parameters
 * 			   - GSL fdf function parameters
 * 			   - GSL fdf parameters
 *
 *  Return value: - status, success or error in computing value
 */
static int
solve_system(gsl_vector *x, gsl_multifit_nlinear_fdf *fdf,
             gsl_multifit_nlinear_parameters *params)
{
	// x, fdf and params have been checked

	const gsl_multifit_nlinear_type *T = gsl_multifit_nlinear_trust;
	gsl_multifit_nlinear_workspace *work =
			gsl_multifit_nlinear_alloc(T, params, fdf->n, fdf->p);

	if (!work){
		err_msg("Unable to allocate memory for workspace");
		return GSL_ENOMEM;
	}

#ifdef DEBUG
	double chisq0, chisq, rcond;
	gsl_vector * f_res = gsl_multifit_nlinear_residual(work);
#endif
	gsl_vector * x_fit = gsl_multifit_nlinear_position(work);
	int ret; 	// return value of calls
	int info;	// convergence info (X test or G test)

	/* initialize solver */
	if (!(ret = gsl_multifit_nlinear_init(x, fdf, work))){

#ifdef DEBUG
		/* store initial cost */
		if ((ret = gsl_blas_ddot(f_res, f_res, &chisq0)))
			warn("unable to compute initial cost function: %s", gsl_strerror(ret));
#endif

		/* iterate until convergence */
		if ((ret=gsl_multifit_nlinear_driver(FIT_NUMITR, FIT_XTOL, FIT_GTOL, FIT_FTOL,
#ifdef DEBUG
									  callback, NULL,	// iteration callback & parameters
#else
									  NULL, NULL,		// no callback and parameters
#endif
									  &info, work))){
			err_msg("unable to compute trust region approximation: %s", gsl_strerror(ret));
			gsl_multifit_nlinear_free(work);
			// return, parameters remain unchanged
			return ret;
		}

#ifdef DEBUG
		// FOR PRINTING ONLY

		/* store final cost = x^T*x */
		if ((ret = gsl_blas_ddot(f_res, f_res, &chisq)))
			warn("unable to compute scalar product: %s", gsl_strerror(ret));

		/* store cond(J(x)) */
		if ((ret = gsl_multifit_nlinear_rcond(&rcond, work)))
			warn("unable to compute reciprocal condition number : %s", gsl_strerror(ret));
#endif

		// copy best fit parameters to x vector position
		if ((ret = gsl_vector_memcpy(x, x_fit)))
			err_msg("unable to copy vector: %s", gsl_strerror(ret));

#ifdef DEBUG
		/* print summary */
		printDbg(PFX "NITER         = %zu\n", gsl_multifit_nlinear_niter(work));
		printDbg(PFX "NFEV          = %zu\n", fdf->nevalf);
		printDbg(PFX "NJEV          = %zu\n", fdf->nevaldf);
		printDbg(PFX "NAEV          = %zu\n", fdf->nevalfvv);
		printDbg(PFX "initial cost  = %.12e\n", chisq0);
		printDbg(PFX "final cost    = %.12e\n", chisq);
		printDbg(PFX "final x       = (%.12e, %.12e, %12e)\n",
			  gsl_vector_get(x, 0), gsl_vector_get(x, 1), gsl_vector_get(x, 2));
		printDbg(PFX "final cond(J) = %.12e\n", 1.0 / rcond);
		fflush(dbg_out);
#endif
	}
	else
		err_msg("failed to initialize solver: %s", gsl_strerror(ret));

	gsl_multifit_nlinear_free(work);
	return ret;
}

/*
 * uniparm_copy: uniform parameters to area of 1
 *				 replaces the reference with a uniform copy, to be freed
 *
 * Arguments: - pointer to pointer to parameters
 *
 * Return value: success or error code
 */
static int
uniparm_copy(stat_param ** x){

	if (!x || !*x)
		return GSL_EINVAL;

	// clone vector
	stat_param * x0 = gsl_vector_alloc(num_par);
	if (!x0){
		err_msg("unable to allocate parameter vector");
		return GSL_ENOMEM;
	}

	int ret;
	if ((ret = gsl_vector_memcpy(x0, *x)))
		err_msg("unable to copy parameter vector : %s", gsl_strerror(ret));

	// Update to uniform value
	double c = gsl_vector_get(x0, 2);
	gsl_vector_set(x0, 0, 1/(sqrt(2*M_PI)*c));

	*x = x0;

	return ((ret != 0) ? GSL_FAILURE : GSL_SUCCESS);
}

/*
 *  runstats_gaussian(): function to calculate Normal (Gaussian) distribution values
 *
 *  Arguments: - amplitude of Normal
 * 			   - offset of Normal
 * 			   - width of Normal
 *
 *  Return value: curve value on that point
 *
 * model function: a * exp( -1/2 * [ (t - b) / c ]^2 )
 *  */
double
runstats_gaussian(const double a, const double b, const double c, const double t)
{
	const double z = (t - b) / c;
	return (a * exp(-0.5 * z * z));
}

/*
 * runstats_paramInit: initializes the parameter vector
 *
 * Arguments: - pointer to pointer to the memory location for storage
 * 			  - expected center of distribution
 *
 * Return value: success or error code
 */
int
runstats_paramInit(stat_param ** x, double b){

	/*
	 * fitting parameter vector and constant init
	 */
	*x = gsl_vector_alloc(num_par); /* model parameter vector */
	if (!*x){
		err_msg("Unable to allocate memory for vector");
		return GSL_ENOMEM;
	}

	/* (Gaussian) fitting model starting parameters, updated through iterations */
	gsl_vector_set(*x, 0, MODEL_DEFAMP);  		/* amplitude */
	gsl_vector_set(*x, 1, b * MODEL_DEFOFS); 	/* center */
	gsl_vector_set(*x, 2, b * MODEL_DEFSTD); 	/* width */

	return GSL_SUCCESS;
}

/*
 * runstats_histInit: inits the histogram data structure
 *
 * Arguments: - pointer to pointer to the memory location for storage
 * 			  - expected center of distribution
 *
 * Return value: success or error code
 */
int
runstats_histInit(stat_hist ** h, double b){

	if (0.0 == b)
		return GSL_FAILURE;

	double bin_min = b * BIN_DEFMIN;		// default ranges
	double bin_max = b * BIN_DEFMAX;

	// preventive
	if (bin_min >= bin_max){
		err_msg("Invalid ranges adapt %f,%f", bin_min, bin_max);
		return GSL_FAILURE;
	}

	/* Allocate memory, histogram data for RTC accumulation */
	*h = gsl_histogram_alloc (STARTBINS);	// number of bins to fit
	if (!*h){
		err_msg("Unable to allocate memory for histogram");
		return GSL_ENOMEM;
	}

	// set ranges and reset bins, fixed to n bin count
	int ret;
	if ((ret = gsl_histogram_set_ranges_uniform (*h, bin_min, bin_max)))
		err_msg("unable to initialize histogram bins: %s", gsl_strerror(ret));

	return ((ret != 0) ? GSL_FAILURE : GSL_SUCCESS);
}

/*
 * runstats_histShape: adjust the value to stay in histogram range
 *
 * Arguments: - pointer to the histogram
 * 			  - value to check
 *
 * Return value: returns the adjusted value
 */
double
runstats_histShape(stat_hist * h, double b){

	// reshape into LIMIT
	if (b >= gsl_histogram_max(h)){
		double dummy;
		(void)gsl_histogram_get_range(h, h->n-1, &b, &dummy);
	}
	if (b < gsl_histogram_min(h))
		b = gsl_histogram_min(h);
	return b;
}

/*
 * runstats_paramVerify: check if the two coincide in range
 *
 * Arguments: - pointer to the histogram
 * 			  - pointer to the parameter vector
 *
 * Return value: success or error code
 */
int
runstats_paramVerify(stat_hist * h, stat_param * x){
	if (!h || !x)
		return GSL_FAILURE;

	double a = gsl_vector_get(x, 0);
	double b = gsl_vector_get(x, 1);
	double c = gsl_vector_get(x, 2);

	double min = gsl_histogram_min(h);
	double max = gsl_histogram_max(h);

	return ( 1.0 > a  || 0 > b || 0 > c		// avoid negative (or cnt <1) values
			 || min > b || max < b			// center out of range
			 || (min > b-c && max <= b+c)) 	// or left and right width are out of range (at least one wing must be in)
			?  GSL_FAILURE : GSL_SUCCESS;
}

/*
 * runstats_histCheck: check if minimum amount for bin fitting is met
 *
 * Arguments: - pointer to the memory location for storage
 *
 * Return value: success or error code
 */
int
runstats_histCheck(stat_hist * h){
	if (!h)
		return GSL_FAILURE;

	return (gsl_histogram_sum(h) < SAMP_MINCNT)
		?  GSL_FAILURE : GSL_SUCCESS;
}

/*
 * runstats_histMean: returns the mean value of the histogram
 *
 * Arguments: - pointer to the memory location for storage
 *
 * Return value: - double- mean value
 */
double
runstats_histMean(stat_hist * h){
	if (!h)
		return 0.0;

	return gsl_histogram_mean(h);

}

/*
 * runstats_histAdd: increases the count of an occurrence value
 *
 * Arguments: - pointer to the memory location for storage
 * 			  - occurrence value
 *
 * Return value: success or error code
 */
int
runstats_histAdd(stat_hist * h, double b){
	if (!h)
		return GSL_FAILURE;

	return gsl_histogram_increment(h,
			runstats_histShape(h, b));
}

/*
 * runstats_histFit: fit bin size to data in histogram and reset
 *
 * Arguments: - pointer holding the histogram pointer
 *
 * Return value: success or error code
 */
int
runstats_histFit(stat_hist **h)
/*
 * Scott, D. 1979.
 * On optimal and data-based histograms.
 * Biometrika, 66:605-610.
 * https://www.fmrib.ox.ac.uk/datasets/techrep/tr00mj2/tr00mj2/node24.html
 *
 */
{
	if (!h || !*h)
		return GSL_EINVAL;

	double N = gsl_histogram_sum(*h);
	if (SAMP_MINCNT > N)
		return GSL_EDOM; // small input count

	// get parameters of bins
	size_t maxbin = gsl_histogram_max_bin(*h);
	size_t n = gsl_histogram_bins(*h);
	double mn = gsl_histogram_mean(*h);	 // sample mean
	size_t mn_bin = n/2;

	int ret = GSL_SUCCESS;

	if ((0.0 != mn) && (gsl_histogram_find(*h, mn, &mn_bin))){
		err_msg("Unable to find mean in histogram!");
		ret = GSL_FAILURE;
	}

	// outside margins? 20-80%.. STDev has no meaning!!
	if (n * 2 > MIN(maxbin, mn_bin) * 10	// 10er bins 0-1
			|| n * 8 <= MAX(maxbin,mn_bin) * 10){ // 10er bins 8-9

		gsl_histogram_free(*h); // clear all because of out of range, force re-init
		*h = NULL;	// reset variable
		err_msg("Mean out of boundaries!");
		return GSL_EDOM; // out of range
	}

	// get parameters of histogram
	double sd = gsl_histogram_sigma(*h); // sample standard deviation

	// update bin range
	if (0 == sd){ // if standard deviation = 0, i.e., all points in one bin, set to bin-with
		sd = (gsl_histogram_max(*h) - gsl_histogram_min(*h))/n;
		err_msg("Error determining STDev!");
		ret = GSL_FAILURE;
	}

	// compute ideal bin size according to Scott 1979, with N = ~min 10 bins
	double W = 3.49*sd*pow(N, -1.0/3.0);

	// bin count to cover 5 standard deviations both sides
	size_t new_n = (size_t)trunc(sd*5.0/W);

	// adjust margins bin limits
	double bin_min = MAX(0.0, mn - ((double)new_n/2.0)*W); // no negative values
	double bin_max = mn + ((double)new_n/2.0)*W;

//	if ((n == new_n)
//		&& ()){
//
//	}

	if (n != new_n){
		// if bin count differs, reallocate
		gsl_histogram_free (*h);
		n = new_n;
		*h = gsl_histogram_alloc (n);
		if (!*h){
			err_msg("Unable to allocate memory for histogram");
			return GSL_ENOMEM;
		}
	}
	// preventive
	if (bin_min >= bin_max){
		err_msg("Invalid ranges resize %f,%f", bin_min, bin_max);
		return GSL_FAILURE;
	}

	if (gsl_histogram_set_ranges_uniform (*h, bin_min, bin_max)){
		err_msg("unable to initialize histogram bins: %s", gsl_strerror(ret));
		ret = GSL_FAILURE;
	}

	return ret;
}

/*
 * runstats_histSolve: run least squares fitting with TRS and accel
 *
 * Arguments: - pointer to the histogram
 * 			  - pointer to the parameter vector
 *
 * Return value: success or error code
 */
int
runstats_histSolve(stat_hist * h, stat_param * x)
{
	if ((!x) || (!h))
		return GSL_EINVAL;

	// pass histogram to fitting structure
	struct stat_data fit_data = {
			h->range,
			h->bin,
			h->n};

	// Normalize histogram


	/*
	 * 	Starting from here, fitting method setup, TRS
	 */
	if (resamplehist(h)){
		err_msg("Unable to resample histogram!");
		return GSL_EINVAL;
	}

	// function definition setup for solver ->
	// pointers to f (model function), df (model differential), and fvv (model acceleration)
	gsl_multifit_nlinear_fdf fdf;
	// function solver parameters for TRS problem
	gsl_multifit_nlinear_parameters fdf_params =
		gsl_multifit_nlinear_default_parameters();

	/* define function parameters to be minimized */
	fdf.f = func_f;			// fitting test to Gaussian
	fdf.df = func_df;		// first derivative Gaussian
	fdf.fvv = func_fvv;		// acceleration method function for Gaussian
	fdf.n = fit_data.n;		// number of functions => fn(tn) = yn
	fdf.p = num_par;		// number of independent variables in model
	fdf.params = &fit_data;	// data-vector for the n functions

	// enable Levenberg-Marquardt Geodesic acceleration method for trust-region subproblem
	fdf_params.trs = //gsl_multifit_nlinear_trs_lm;
				gsl_multifit_nlinear_trs_lmaccel;

	/*
	* Call solver
	*/
	int ret = solve_system(x, &fdf, &fdf_params);
	if (!ret)
		// even though successful, it happens that the gaussian contains negative pars
		// check
		return runstats_paramVerify(h, x);
	return ret;
}

/*
 * runstats_histSixSigma() : return six-sigma probability value of histogram = mean + 6 stdev
 *
 * Arguments: - histogram addr pointer
 *
 * Return value: time value for six sigma
 */
double
runstats_histSixSigma(const stat_hist * h){
	if (!h)
		return 0.0;
	return gsl_histogram_mean(h) + 6 * gsl_histogram_sigma(h);
}

/*
 * runstats_mdlpdf() : Integrate area under curve between a-b
 *
 * Arguments: - pointer to the parameter vector
 * 			  - bounds a, b of the integral
 * 			  - address probability value (return)
 * 			  - address of error value (return)
 *
 * Return value: success or error code
 */
int
runstats_mdlpdf(stat_param * x, double a, double b, double * p, double * error){

	if (!x || !p || !error)
		return GSL_EINVAL;

	int ret;

	// create a normalized clone
	if ((ret = uniparm_copy(&x))) {
		err_msg("unable to clone values: %s", gsl_strerror(ret));
		return ret;
	}

	gsl_integration_workspace * w
		= gsl_integration_workspace_alloc (INT_NUMITER);
	if (!w){
		err_msg("unable to allocate workspace memory");
		return GSL_ENOMEM;
	}

	gsl_function F = { &func_gaussian, x }; // function , parameters

	if ((ret = gsl_integration_qags (&F, a, b,
						INT_EPSABS, INT_EPSREL, INT_NUMITER,
						w, p, error)))
		err_msg ("curve integration failed : %s", gsl_strerror(ret));

	printDbg(PFX "integr. result  = %.18f\n", *p);
	printDbg(PFX "estimated error = %.18f\n", *error);
	printDbg(PFX "intervals       = %zu\n", w->size);

	gsl_integration_workspace_free (w);
	gsl_vector_free(x);

	return ((ret != 0) ? GSL_FAILURE : GSL_SUCCESS);
}

/*
 * runstats_mdlUpb() : Compute the bound b that integrates the area under curve to p
 * Uses Root finding algorithm, by approximating f(x)-p = 0
 *
 * Arguments: - pointer to the parameter vector
 * 			  - bounds a (input), and address to b of the integral (return)
 * 			  - probability value that is needed
 * 			  - address of error value (return)
 *
 * Return value: success or error code
 */
int
runstats_mdlUpb(stat_param * x, double a, double * b, double p, double * error){

	if (!x || !b || !error)
		return GSL_EINVAL;

	int ret;

	// create a normalized clone
	if ((ret = uniparm_copy(&x))) {
		err_msg("unable to clone values: %s", gsl_strerror(ret));
		return ret;
	}

	// 68–95–99.7 rule on normalized Gaussian to shorten the search range
	// https://en.wikipedia.org/wiki/68–95–99.7_rule
	double bmin, bmax;
	{
		double gb = gsl_vector_get(x, 1);
		double gc = gsl_vector_get(x, 2);

		// we ignore everything below 0.5
		if (p < 0.68){
			// 0.5..0.68
			bmin = gb - 0.03 * gc;
			bmax = gb + 1.03 * gc;
		}
		else if (p < 0.95) {
			// 0.68..0.95
			bmin = gb + 0.97 * gc;
			bmax = gb + 2.03 * gc;
		}
		else if (p < 0.997) {
			// 0.95..0.997
			bmin = gb + 1.97 * gc;
			bmax = gb + 3.03 * gc;
		}
		else {
			// beyond 0.997
			bmin = gb + 2.97 * gc;
			bmax = gb + 10 * gc;
		}
	}

	// Start code from GNU sample
	int status;
	int iter = 0;

	const gsl_root_fsolver_type *T;
	gsl_root_fsolver *s;

	gsl_function F; // define function to solve for
	{
		struct func_integmdl_par params = { x, a, p, error};
		F.function = &func_integmdl;
		F.params = &params;
	}


	{	// solver type and initialization
		T = gsl_root_fsolver_brent;
		s = gsl_root_fsolver_alloc (T);
		gsl_root_fsolver_set (s, &F, bmin, bmax);
	}

	printDbg(PFX "using %s method\n",
		  gsl_root_fsolver_name (s));

	printDbg(PFX "%5s [%9s, %9s] %9s %9s\n",
		  "iter", "lower", "upper", "root",
		  "err(est)");

	// Root solver iteration
	do
	{
	  iter++;
	  status = gsl_root_fsolver_iterate (s);
	  *b = gsl_root_fsolver_root (s);
	  bmin = gsl_root_fsolver_x_lower (s);
	  bmax = gsl_root_fsolver_x_upper (s);
	  status = gsl_root_test_interval (bmin, bmax,
			  	  	  	  ROOT_EPSABS, ROOT_EPSREL); // absolute + relative error

	  if (status == GSL_SUCCESS)
		  printDbg(PFX "Converged:\n");

	  printDbg(PFX "%5d [%.7f, %.7f] %.7f %.7f\n",
			  iter, bmin, bmax,
			  *b,
			  bmax - bmin);
	}
	while (status == GSL_CONTINUE && iter < ROOT_NUMITR);

	gsl_root_fsolver_free (s);
	gsl_vector_free(x);

	return status;
}

/*
 * runstats_cdfCreate() : Compute the bound b that integrates the area under curve to p
 *
 * Arguments: - histogram addr pointer
 * 			  - CDF destination pointer
 *
 * Return value: success or error code
 */
int
runstats_cdfCreate(stat_hist **h, stat_cdf **c){

	if (!c || !h || !(*h))
		return GSL_EINVAL;

	int ret = GSL_SUCCESS;

	// re-alloc if number of bins differs
	if (*c && ((*c)->n != (*h)->n))
		runstats_cdfFree(c);

	if (!*c)
		*c = gsl_histogram_pdf_alloc((*h)->n);

	if ((ret = gsl_histogram_pdf_init(*c, *h))){
		err_msg ("CDF creation failed : %s", gsl_strerror(ret));
		return ret;
	}

	return GSL_SUCCESS;

}

/*
 * runstats_cdfsample() : CDF sample
 *
 * Arguments: - histogram CDF pointer
 * 			  - probability value to look for
 *
 * Return value: time value
 */
double
runstats_cdfSample(const stat_cdf * c, double r){
	if (!c)
		return 0.0;
	return gsl_histogram_pdf_sample(c, r);
}

/*
 * runstats_cdffree() : CDF free
 *
 * Arguments: - CDF pointer
 *
 * Return value: -
 */
void
runstats_cdfFree(stat_cdf ** c){

	gsl_histogram_pdf_free(*c);
	*c = NULL;
}

/*
 * runstats_paramPrint: adjust the value to stay in histogram range
 *
 * Arguments: - pointer to the histogram
 * 			  - value to check
 *
 * Return value: returns the adjusted value
 */
int
runstats_paramPrint(stat_param * x, char * str, size_t len){

	if (!str || len < 42) // total length of format string
		return GSL_EINVAL;

	double a = gsl_vector_get(x, 0);
	double b = gsl_vector_get(x, 1);
	double c = gsl_vector_get(x, 2);

	// length = 12+(.) x3 + ' ' x2 + \0 = 42
	(void)sprintf(str, "%12.9f %12.9f %12.9f", a, b, c);

	return GSL_SUCCESS;
}


/*
 * runstats_paramFree() : free parameter vector
 *
 * Arguments: - pointer to the parameter vector
 *
 * Return value: -
 */
void
runstats_paramFree(stat_param * x){
	gsl_vector_free(x);
}

/*
 * runstats_histFree() : free histogram structure
 *
 * Arguments: - pointer to the histogram data
 *
 * Return value: success or error code
 */
void
runstats_histFree(stat_hist * h){
	gsl_histogram_free(h);
}
