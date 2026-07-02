/*
 * stats.h -- descriptive statistics over arrays of doubles.
 *
 * A small, self-contained numeric library used by the standard-library natives
 * (median/stddev/percentile/...).  Everything operates on plain double buffers,
 * so it has no dependency on the interpreter's object model and is trivially
 * testable in isolation.  Functions that need order statistics sort a private
 * copy of the input, so the caller's buffer is never mutated.
 */

#ifndef COIL_STATS_H
#define COIL_STATS_H

#include <stddef.h>
#include <stdbool.h>

/* --- reductions (n must be > 0 unless noted) --- */
double stats_sum(const double *xs, size_t n);       /* 0 for n == 0 */
double stats_product(const double *xs, size_t n);   /* 1 for n == 0 */
double stats_min(const double *xs, size_t n);
double stats_max(const double *xs, size_t n);
double stats_mean(const double *xs, size_t n);

/* --- spread --- */
/* variance/stddev: population form when `sample` is false, sample (n-1) form
 * when true.  Sample forms require n >= 2. */
double stats_variance(const double *xs, size_t n, bool sample);
double stats_stddev(const double *xs, size_t n, bool sample);

/* --- order statistics (input copied and sorted internally) --- */
double stats_median(const double *xs, size_t n);
/* linear-interpolated percentile, p in [0, 100] */
double stats_percentile(const double *xs, size_t n, double p);
/* the most frequent value; ties resolved toward the smaller value */
double stats_mode(const double *xs, size_t n);

/* --- shape --- */
double stats_skewness(const double *xs, size_t n);  /* population skew, n >= 2 */

/* --- paired --- */
/* Pearson correlation of xs and ys (both length n, n >= 2). Returns 0 when a
 * series has zero variance. */
double stats_correlation(const double *xs, const double *ys, size_t n);
/* population covariance of xs and ys (length n) */
double stats_covariance(const double *xs, const double *ys, size_t n);
/* ordinary least-squares fit ys ~ a + b*xs; writes slope and intercept */
void   stats_linfit(const double *xs, const double *ys, size_t n,
                    double *slope, double *intercept);

/* --- transforms (write n results into `out`, which must hold n doubles) --- */
void stats_cumsum(const double *xs, size_t n, double *out);
/* z-scores using the population stddev; all-zero out if stddev is 0 */
void stats_zscore(const double *xs, size_t n, double *out);
/* rescale to [0, 1] by min/max; all-zero out if the range is 0 */
void stats_normalize(const double *xs, size_t n, double *out);

#endif /* COIL_STATS_H */
