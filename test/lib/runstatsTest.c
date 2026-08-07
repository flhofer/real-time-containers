/*
###############################
# test script by Florian Hofer
###############################
*/

#include "runstatsTest.h"

// tested
#include "../../src/lib/runstats.c"

static stat_hist * histogram;
static stat_param * parameters;
static stat_cdf * distribution;
static stat_scope scope;

static void runstats_setup(void) {
	histogram = NULL;
	parameters = NULL;
	distribution = NULL;
	runstats_scopeReset(&scope);
}

static void runstats_teardown(void) {
	if (distribution)
		runstats_cdfFree(&distribution);
	if (parameters)
		runstats_paramFree(parameters);
	if (histogram)
		runstats_histFree(histogram);
}

/// TEST CASE -> evaluate the Gaussian model at and around its center
/// EXPECTED -> peak amplitude and symmetry follow the model equation
START_TEST(runstats_gaussian_model)
{
	ck_assert_double_eq_tol(runstats_gaussian(4.0, 2.0, 0.5, 2.0),
		4.0, 0.000000001);
	double left = runstats_gaussian(4.0, 2.0, 0.5, 1.5);
	double right = runstats_gaussian(4.0, 2.0, 0.5, 2.5);
	ck_assert_double_eq_tol(left, right, 0.000000001);
	ck_assert_double_eq_tol(left, 4.0 * exp(-0.5), 0.000000001);
}
END_TEST

/// TEST CASE -> initialize, print and verify Gaussian model parameters
/// EXPECTED -> defaults are centered on the estimate and fit its histogram range
START_TEST(runstats_parameters)
{
	ck_assert_int_eq(runstats_paramInit(&parameters, 1.0), GSL_SUCCESS);
	ck_assert_double_eq_tol(gsl_vector_get(parameters, 0),
		1.0 / (sqrt(2.0 * M_PI) * MODEL_DEFSTD), 0.000000001);
	ck_assert_double_eq_tol(gsl_vector_get(parameters, 1), MODEL_DEFOFS,
		0.000000001);
	ck_assert_double_eq_tol(gsl_vector_get(parameters, 2), MODEL_DEFSTD,
		0.000000001);

	char output[64];
	ck_assert_int_eq(runstats_paramPrint(parameters, output, sizeof(output)),
		GSL_SUCCESS);
	double amplitude, center, width;
	ck_assert_int_eq(sscanf(output, "%lf %lf %lf", &amplitude, &center, &width), 3);
	ck_assert_double_eq_tol(amplitude, gsl_vector_get(parameters, 0), 0.000000001);
	ck_assert_double_eq_tol(center, gsl_vector_get(parameters, 1), 0.000000001);
	ck_assert_double_eq_tol(width, gsl_vector_get(parameters, 2), 0.000000001);
	ck_assert_int_eq(runstats_paramPrint(parameters, NULL, sizeof(output)), GSL_EINVAL);
	ck_assert_int_eq(runstats_paramPrint(parameters, output, 41), GSL_EINVAL);

	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_paramVerify(histogram, parameters), GSL_SUCCESS);
	gsl_vector_set(parameters, 0, 0.5);
	ck_assert_int_eq(runstats_paramVerify(histogram, parameters), GSL_FAILURE);
	gsl_vector_set(parameters, 0, 2.0);
	gsl_vector_set(parameters, 1, 2.0);
	ck_assert_int_eq(runstats_paramVerify(histogram, parameters), GSL_FAILURE);
	gsl_vector_set(parameters, 1, 1.0);
	gsl_vector_set(parameters, 2, -0.1);
	ck_assert_int_eq(runstats_paramVerify(histogram, parameters), GSL_FAILURE);
	ck_assert_int_eq(runstats_paramVerify(NULL, parameters), GSL_FAILURE);
	ck_assert_int_eq(runstats_paramVerify(histogram, NULL), GSL_FAILURE);
}
END_TEST

/// TEST CASE -> reset an accumulated histogram scope
/// EXPECTED -> counters are zero and extrema return to sentinel values
START_TEST(runstats_scope_reset)
{
	scope.samples = 10;
	scope.underflows = 2;
	scope.overflows = 3;
	scope.sum = 45.0;
	scope.sum_squared = 250.0;
	scope.min = 1.0;
	scope.max = 9.0;
	runstats_scopeReset(&scope);

	ck_assert_uint_eq(scope.samples, 0);
	ck_assert_uint_eq(scope.underflows, 0);
	ck_assert_uint_eq(scope.overflows, 0);
	ck_assert_double_eq(scope.sum, 0.0);
	ck_assert_double_eq(scope.sum_squared, 0.0);
	ck_assert_double_eq(scope.underflow_sum, 0.0);
	ck_assert_double_eq(scope.overflow_sum, 0.0);
	ck_assert_double_eq(scope.min, DBL_MAX);
	ck_assert_double_eq(scope.max, -DBL_MAX);
	runstats_scopeReset(NULL);
}
END_TEST

/// TEST CASE -> initialize a fixed histogram and reject invalid estimates
/// EXPECTED -> default bin count and 70-130 percent range are applied
START_TEST(runstats_histogram_init)
{
	ck_assert_int_eq(runstats_histInit(&histogram, 10.0), GSL_SUCCESS);
	ck_assert_uint_eq(gsl_histogram_bins(histogram), STARTBINS);
	ck_assert_double_eq_tol(gsl_histogram_min(histogram), 7.0, 0.000000001);
	ck_assert_double_eq_tol(gsl_histogram_max(histogram), 13.0, 0.000000001);

	stat_hist * invalid = NULL;
	ck_assert_int_eq(runstats_histInit(&invalid, 0.0), GSL_FAILURE);
	ck_assert_ptr_eq(invalid, NULL);
	ck_assert_int_eq(runstats_histInit(&invalid, -1.0), GSL_FAILURE);
	ck_assert_ptr_eq(invalid, NULL);
}
END_TEST

/// TEST CASE -> add values below, inside and above the histogram range
/// EXPECTED -> scope tracks every value while bins contain only in-range samples
START_TEST(runstats_histogram_add)
{
	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 0.5), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 0.9), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 1.1), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 1.5), GSL_SUCCESS);

	ck_assert_uint_eq(scope.samples, 4);
	ck_assert_uint_eq(scope.underflows, 1);
	ck_assert_uint_eq(scope.overflows, 1);
	ck_assert_double_eq_tol(scope.sum, 4.0, 0.000000001);
	ck_assert_double_eq_tol(scope.sum_squared, 4.52, 0.000000001);
	ck_assert_double_eq_tol(scope.underflow_sum, 0.5, 0.000000001);
	ck_assert_double_eq_tol(scope.overflow_sum, 1.5, 0.000000001);
	ck_assert_double_eq_tol(scope.min, 0.5, 0.000000001);
	ck_assert_double_eq_tol(scope.max, 1.5, 0.000000001);
	ck_assert_double_eq_tol(gsl_histogram_sum(histogram), 2.0, 0.000000001);
	ck_assert_int_eq(runstats_histAdd(NULL, &scope, 1.0), GSL_FAILURE);
	ck_assert_int_eq(runstats_histAdd(histogram, NULL, 1.0), GSL_FAILURE);
}
END_TEST

/// TEST CASE -> compute readiness, mean and six-sigma values from accumulated scope
/// EXPECTED -> readiness starts at 50 samples and calculations use all samples
START_TEST(runstats_histogram_summary)
{
	ck_assert_int_eq(runstats_histInit(&histogram, 2.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 1.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 2.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 3.0), GSL_SUCCESS);

	ck_assert_double_eq_tol(runstats_histMean(histogram, &scope), 2.0,
		0.000000001);
	ck_assert_double_eq_tol(runstats_histSixSigma(histogram, &scope),
		2.0 + 6.0 * sqrt(2.0 / 3.0), 0.000000001);
	ck_assert_int_eq(runstats_histCheck(histogram, &scope), GSL_FAILURE);
	for (int i = 3; i < 50; i++)
		ck_assert_int_eq(runstats_histAdd(histogram, &scope, 2.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histCheck(histogram, &scope), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histCheck(NULL, &scope), GSL_FAILURE);
	ck_assert_int_eq(runstats_histCheck(histogram, NULL), GSL_FAILURE);
	ck_assert_double_eq(runstats_histMean(NULL, &scope), 0.0);
	ck_assert_double_eq(runstats_histMean(histogram, NULL), 0.0);
	stat_scope empty;
	runstats_scopeReset(&empty);
	ck_assert_double_eq(runstats_histSixSigma(histogram, &empty), 0.0);
}
END_TEST

/// TEST CASE -> keep a histogram whose observations remain within tolerance
/// EXPECTED -> resampling is deferred and existing samples remain intact
START_TEST(runstats_histogram_resample_unchanged)
{
	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	for (int i = 0; i < 50; i++)
		ck_assert_int_eq(runstats_histAdd(histogram, &scope, 1.0), GSL_SUCCESS);
	stat_hist * original = histogram;
	ck_assert_int_eq(runstats_histResample(&histogram, &scope, 0.98),
		-GSL_CONTINUE);
	ck_assert_ptr_eq(histogram, original);
	ck_assert_uint_eq(scope.samples, 50);

	ck_assert_int_eq(runstats_histResample(NULL, &scope, 0.98), -GSL_EINVAL);
	ck_assert_int_eq(runstats_histResample(&histogram, NULL, 0.98), -GSL_EINVAL);
	stat_scope small;
	runstats_scopeReset(&small);
	ck_assert_int_eq(runstats_histResample(&histogram, &small, 0.98),
		-GSL_CONTINUE);
}
END_TEST

/// TEST CASE -> extend a histogram after excessive upper-range samples
/// EXPECTED -> range includes overflow average with margin and scope is reset
START_TEST(runstats_histogram_resample_extended)
{
	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	for (int i = 0; i < 50; i++)
		ck_assert_int_eq(runstats_histAdd(histogram, &scope, 1.0), GSL_SUCCESS);
	for (int i = 0; i < 6; i++)
		ck_assert_int_eq(runstats_histAdd(histogram, &scope, 2.0), GSL_SUCCESS);

	ck_assert_int_eq(runstats_histResample(&histogram, &scope, 0.98), GSL_SUCCESS);
	ck_assert_double_eq_tol(gsl_histogram_min(histogram), 0.7, 0.000000001);
	ck_assert_double_eq_tol(gsl_histogram_max(histogram), 2.1, 0.000000001);
	ck_assert_uint_eq(gsl_histogram_bins(histogram), STARTBINS);
	ck_assert_uint_eq(scope.samples, 0);
	ck_assert_double_eq(scope.min, DBL_MAX);
	ck_assert_double_eq(scope.max, -DBL_MAX);
}
END_TEST

/// TEST CASE -> adapt histogram binning to a sufficiently large data set
/// EXPECTED -> fitted histogram retains a valid range around the observed mean
START_TEST(runstats_histogram_fit)
{
	ck_assert_int_eq(runstats_histFit(NULL, &scope), GSL_EINVAL);
	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histFit(&histogram, NULL), GSL_EINVAL);
	ck_assert_int_eq(runstats_histFit(&histogram, &scope), GSL_EDOM);

	for (int i = 0; i < 600; i++) {
		double value = 0.9 + 0.02 * (double)(i % 11);
		ck_assert_int_eq(runstats_histAdd(histogram, &scope, value), GSL_SUCCESS);
	}
	double mean = runstats_histMean(histogram, &scope);
	ck_assert_int_eq(runstats_histFit(&histogram, &scope), GSL_SUCCESS);
	ck_assert_ptr_ne(histogram, NULL);
	ck_assert_uint_gt(gsl_histogram_bins(histogram), 0);
	ck_assert_double_lt(gsl_histogram_min(histogram), mean);
	ck_assert_double_gt(gsl_histogram_max(histogram), mean);
}
END_TEST

/// TEST CASE -> fit a Gaussian model to deterministic histogram weights
/// EXPECTED -> solver returns positive parameters centered within the histogram
START_TEST(runstats_histogram_solve)
{
	ck_assert_int_eq(runstats_histSolve(NULL, NULL), GSL_EINVAL);
	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_paramInit(&parameters, 1.0), GSL_SUCCESS);

	for (size_t i = 0; i < gsl_histogram_bins(histogram); i++) {
		double lower, upper;
		ck_assert_int_eq(gsl_histogram_get_range(histogram, i, &lower, &upper),
			GSL_SUCCESS);
		double center = (lower + upper) / 2.0;
		double weight = runstats_gaussian(100.0, 1.0, 0.05, center);
		ck_assert_int_eq(gsl_histogram_accumulate(histogram, center, weight),
			GSL_SUCCESS);
	}

	ck_assert_int_eq(runstats_histSolve(histogram, parameters), GSL_SUCCESS);
	ck_assert_double_gt(gsl_vector_get(parameters, 0), 1.0);
	ck_assert_double_gt(gsl_vector_get(parameters, 1), gsl_histogram_min(histogram));
	ck_assert_double_lt(gsl_vector_get(parameters, 1), gsl_histogram_max(histogram));
	ck_assert_double_gt(gsl_vector_get(parameters, 2), 0.0);
}
END_TEST

/// TEST CASE -> integrate a normalized Gaussian and solve its percentile bound
/// EXPECTED -> probability and upper bound match standard normal values
START_TEST(runstats_model_probability)
{
	parameters = gsl_vector_alloc(num_par);
	ck_assert_ptr_ne(parameters, NULL);
	gsl_vector_set(parameters, 0, 7.0);
	gsl_vector_set(parameters, 1, 0.0);
	gsl_vector_set(parameters, 2, 1.0);
	double probability = 0.0;
	double error = 0.0;

	ck_assert_int_eq(runstats_mdlpdf(parameters, -1.0, 1.0,
		&probability, &error), GSL_SUCCESS);
	ck_assert_double_eq_tol(probability, 0.682689492, 0.00000001);
	ck_assert_double_lt(error, 0.000001);
	ck_assert_double_eq_tol(gsl_vector_get(parameters, 0), 7.0, 0.000000001);

	double upper = 0.0;
	ck_assert_int_eq(runstats_mdlUpb(parameters, -10.0, &upper, 0.90, &error),
		GSL_SUCCESS);
	ck_assert_double_eq_tol(upper, 1.281551566, 0.000001);
	ck_assert_int_eq(runstats_mdlpdf(NULL, -1.0, 1.0, &probability, &error),
		GSL_EINVAL);
	ck_assert_int_eq(runstats_mdlpdf(parameters, -1.0, 1.0, NULL, &error),
		GSL_EINVAL);
	ck_assert_int_eq(runstats_mdlUpb(NULL, -10.0, &upper, 0.90, &error),
		GSL_EINVAL);
	ck_assert_int_eq(runstats_mdlUpb(parameters, -10.0, NULL, 0.90, &error),
		GSL_EINVAL);
}
END_TEST

/// TEST CASE -> create and sample a histogram-based CDF
/// EXPECTED -> quantiles remain in range and account for outside samples
START_TEST(runstats_cdf_distribution)
{
	ck_assert_int_eq(runstats_cdfCreate(NULL, &distribution), GSL_EINVAL);
	ck_assert_int_eq(runstats_histInit(&histogram, 1.0), GSL_SUCCESS);
	ck_assert_int_eq(runstats_cdfCreate(&histogram, NULL), GSL_EINVAL);
	for (int i = 0; i < 10; i++) {
		ck_assert_int_eq(runstats_histAdd(histogram, &scope,
			0.8 + 0.04 * (double)i), GSL_SUCCESS);
	}
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 0.5), GSL_SUCCESS);
	ck_assert_int_eq(runstats_histAdd(histogram, &scope, 1.5), GSL_SUCCESS);
	ck_assert_int_eq(runstats_cdfCreate(&histogram, &distribution), GSL_SUCCESS);
	ck_assert_uint_eq(distribution->n, gsl_histogram_bins(histogram));

	double low = runstats_cdfSample(distribution, &scope, 0.2);
	double median = runstats_cdfSample(distribution, &scope, 0.5);
	double high = runstats_cdfSample(distribution, &scope, 0.8);
	ck_assert_double_ge(low, gsl_histogram_min(histogram));
	ck_assert_double_le(high, gsl_histogram_max(histogram));
	ck_assert_double_lt(low, median);
	ck_assert_double_lt(median, high);
	ck_assert_double_eq(runstats_cdfSample(NULL, &scope, 0.5), 0.0);
	ck_assert_double_eq(runstats_cdfSample(distribution, NULL, 0.5), 0.0);

	stat_scope outside;
	runstats_scopeReset(&outside);
	outside.samples = 2;
	outside.underflows = 1;
	outside.overflows = 1;
	ck_assert_double_eq(runstats_cdfSample(distribution, &outside, 0.5), 0.0);

	runstats_cdfFree(&distribution);
	ck_assert_ptr_eq(distribution, NULL);
}
END_TEST

void library_runstats(Suite * s) {
	TCase *tc1 = tcase_create("runstats");
	tcase_add_checked_fixture(tc1, runstats_setup, runstats_teardown);
	tcase_add_test(tc1, runstats_gaussian_model);
	tcase_add_test(tc1, runstats_parameters);
	tcase_add_test(tc1, runstats_scope_reset);
	tcase_add_test(tc1, runstats_histogram_init);
	tcase_add_test(tc1, runstats_histogram_add);
	tcase_add_test(tc1, runstats_histogram_summary);
	tcase_add_test(tc1, runstats_histogram_resample_unchanged);
	tcase_add_test(tc1, runstats_histogram_resample_extended);
	tcase_add_test(tc1, runstats_histogram_fit);
	tcase_add_test(tc1, runstats_histogram_solve);
	tcase_add_test(tc1, runstats_model_probability);
	tcase_add_test(tc1, runstats_cdf_distribution);
	suite_add_tcase(s, tc1);
}
