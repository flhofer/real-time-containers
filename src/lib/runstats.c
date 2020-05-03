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

#include <errno.h>			// system error management (LIBC)
#include <string.h>			// strerror print

#include "error.h"

#define NUMINT 20

const size_t num_par = 3;   /* number of model parameters, = polynomial or function size */

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

	printDbg("iter %2zu: a = %.4f, b = %.4f, c = %.4f, |a|/|v| = %.4f cond(J) = %8.4f, |f(x)| = %.4f\n",
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
	const size_t max_iter = 40;  // originally set to 200
	const double xtol = 1.0e-64; // originally set to -8
	const double gtol = 1.0e-64; // originally set to -8
	const double ftol = 1.0e-64; // originally set to -8

	const size_t n = fdf->n;
	const size_t p = fdf->p;
	gsl_multifit_nlinear_workspace *work =
			gsl_multifit_nlinear_alloc(T, params, n, p);

	if (!work){
		err_msg("Unable to allocate memory for workspace");
		return GSL_ENOMEM;
	}

	gsl_vector * f = gsl_multifit_nlinear_residual(work);
	gsl_vector * y = gsl_multifit_nlinear_position(work);
	int info, ret;
	double chisq0, chisq, rcond;

	/* initialize solver */
	if (!(ret = gsl_multifit_nlinear_init(x, fdf, work))){
		/* store initial cost */
		gsl_blas_ddot(f, f, &chisq0);

		/* iterate until convergence */
		gsl_multifit_nlinear_driver(max_iter, xtol, gtol, ftol,
#ifdef DEBUG
									  callback,
#else
									  NULL,
#endif
									  NULL, &info, work);

		/* store final cost = x^T*x */
		if ((ret = gsl_blas_ddot(f, f, &chisq))){
			err_msg("unable to compute scalar product: %s", gsl_strerror(ret));
			return ret;
		}

		/* store cond(J(x)) */
		if ((ret = gsl_multifit_nlinear_rcond(&rcond, work))){
			err_msg("unable to compute reciprocal condition number : %s", gsl_strerror(ret));
			return ret;
		}

		if ((ret = gsl_vector_memcpy(x, y))){
			err_msg("unable to copy vector: %s", gsl_strerror(ret));
			return ret;
		}

		/* print summary */
		printDbg("NITER         = %zu\n", gsl_multifit_nlinear_niter(work));
		printDbg("NFEV          = %zu\n", fdf->nevalf);
		printDbg("NJEV          = %zu\n", fdf->nevaldf);
		printDbg("NAEV          = %zu\n", fdf->nevalfvv);
		printDbg("initial cost  = %.12e\n", chisq0);
		printDbg("final cost    = %.12e\n", chisq);
		printDbg("final x       = (%.12e, %.12e, %12e)\n",
			  gsl_vector_get(x, 0), gsl_vector_get(x, 1), gsl_vector_get(x, 2));
		printDbg("final cond(J) = %.12e\n", 1.0 / rcond);
		fflush(dbg_out);
	}
	else
		err_msg("failed to initialize solver: %s", gsl_strerror(ret));

	gsl_multifit_nlinear_free(work);

	return ret;
}

/*
 * runstats_initparam: initializes the parameter vector
 *
 * Arguments: - pointer to pointer to the memory location for storage
 * 			  - expected center of distribution
 *
 * Return value: success or error code
 */
int
runstats_initparam(stat_param ** x, double b){

	/*
	 * fitting parameter vector and constant init
	 */
	*x = gsl_vector_alloc(num_par); /* model parameter vector */
	if (!*x){
		err_msg("Unable to allocate memory for vector");
		return GSL_ENOMEM;
	}

	/* (Gaussian) fitting model starting parameters, updated through iterations */
	gsl_vector_set(*x, 0, 100.0);  		/* amplitude */
	gsl_vector_set(*x, 1, b * 1.02); 	/* center */
	gsl_vector_set(*x, 2, b * 0.01); 	/* width */

	return GSL_SUCCESS;
}

/*
 * runstats_inithist: inits the histogram data structure
 *
 * Arguments: - pointer to pointer to the memory location for storage
 * 			  - expected center of distribution
 *
 * Return value: success or error code
 */
int
runstats_inithist(stat_hist ** h, double b){

	size_t n = 300;				// number of bins to fit
	double bin_min = b * 0.70;	// use +-30% range
	double bin_max = b * 1.30;

	/* Allocate memory, histogram data for RTC accumulation */
	*h = gsl_histogram_alloc (n);
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
 * runstats_addhist: inits the histogram data structure
 *
 * Arguments: - pointer to the memory location for storage
 * 			  - expected center of distribution
 *
 * Return value: success or error code
 */
int
runstats_addhist(stat_hist * h, double b){
	return gsl_histogram_increment(h, b);
}

/*
 * runstats_fithist: fit bin size to data in histogram and reset
 *
 * Arguments: - pointer holding the histogram pointer
 *
 * Return value: success or error code
 */
int
runstats_fithist(stat_hist **h)
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

	// update bin range

	// get parameters and free histogram
	double mn = gsl_histogram_mean(*h); 	// sample mean
	double sd = gsl_histogram_sigma(*h); // sample standard deviation
	double N = gsl_histogram_sum(*h);

	size_t n = gsl_histogram_bins(*h);

	// compute ideal bin size according to Scott 1979
	double W = 3.49*sd*pow(N, (double)-1/3);

	// bin count to cover 10 standard deviations both sides
	size_t new_n = (size_t)trunc(sd*20/W);

	if (n != new_n) {
		// if bin count differs, reallocate
		gsl_histogram_free (*h);
		n = new_n;
		*h = gsl_histogram_alloc (n);
		if (!*h){
			err_msg("Unable to allocate memory for histogram");
			return GSL_ENOMEM;
		}

	}

	// adjust margins bin limits
	double bin_min = mn - ((double)n/2.0)*W;
	double bin_max = mn + ((double)n/2.0)*W;
	int ret;
	if ((ret = gsl_histogram_set_ranges_uniform (*h, bin_min, bin_max)))
		err_msg("unable to initialize histogram bins: %s", gsl_strerror(ret));

	return ((ret != 0) ? GSL_FAILURE : GSL_SUCCESS);
}

/*
 * runstats_solvehist: run least squares fitting with TRS and accel
 *
 * Arguments: - pointer to the histogram
 * 			  - pointer to the parameter vector
 *
 * Return value: success or error code
 */
int
runstats_solvehist(stat_hist * h, stat_param * x)
{
	if ((!x) || (!h))
		return GSL_EINVAL;

	// pass histogram to fitting structure
	struct stat_data fit_data = {
			h->range,
			h->bin,
			h->n};

	/*
	 * 	Starting from here, fitting method setup, TRS
	 */

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
	fdf_params.trs = gsl_multifit_nlinear_trs_lmaccel;

	/*
	* Call solver
	*/
	return solve_system(x, &fdf, &fdf_params);
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
 * runstats_mdlpdf() : Integrate area under curve between a-b
 *
 * Arguments: - pointer to the parameter vector
 * 			  - parameter a, b of the model
 * 			  - address probability value (return)
 * 			  - address of error value (return)
 *
 * Return value: success or error code
 */
int
runstats_mdlpdf(stat_param * x, double a, double b, double * p, double * error){

	if (!x)
		return GSL_EINVAL;

	int ret;

	// create a normalized clone
	if ((ret = uniparm_copy(&x))) {
		err_msg("unable to clone values: %s", gsl_strerror(ret));
		return ret;
	}

	gsl_integration_workspace * w
		= gsl_integration_workspace_alloc (NUMINT);
	if (!w){
		err_msg("unable to allocate workspace memory");
		return GSL_ENOMEM;
	}

	gsl_function F = { &func_gaussian, x }; // function , parameters

	if ((ret = gsl_integration_qags (&F, a, b, 0, 1e-7, NUMINT,
						w, p, error)))
		err_msg ("curve integration failed : %s", gsl_strerror(ret));

	printDbg("integr. result  = %.18f\n", *p);
	printDbg("estimated error = %.18f\n", *error);
	printDbg("intervals       = %zu\n", w->size);

	gsl_integration_workspace_free (w);

	return ((ret != 0) ? GSL_FAILURE : GSL_SUCCESS);
}

/*
 * runstats_freeparam() : free parameter vector
 *
 * Arguments: - pointer to the parameter vector
 *
 * Return value: -
 */
void
runstats_freeparam(stat_param * x){
	gsl_vector_free(x);
}

/*
 * runstats_freehist() : free histogram structure
 *
 * Arguments: - pointer to the histogram data
 *
 * Return value: success or error code
 */
void
runstats_freehist(stat_hist * h){
	gsl_histogram_free(h);
}
