/*
 * stats.c -- implementations for stats.h.
 *
 * Order statistics work on a malloc'd copy so inputs are never disturbed and no
 * interpreter object is touched (keeping the collector out of the picture).
 */

#include "stats.h"
#include <stdlib.h>
#include <math.h>

double stats_sum(const double *xs, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; i++)
        s += xs[i];
    return s;
}

double stats_product(const double *xs, size_t n)
{
    double p = 1.0;
    for (size_t i = 0; i < n; i++)
        p *= xs[i];
    return p;
}

double stats_min(const double *xs, size_t n)
{
    double m = xs[0];
    for (size_t i = 1; i < n; i++)
        if (xs[i] < m)
            m = xs[i];
    return m;
}

double stats_max(const double *xs, size_t n)
{
    double m = xs[0];
    for (size_t i = 1; i < n; i++)
        if (xs[i] > m)
            m = xs[i];
    return m;
}

double stats_mean(const double *xs, size_t n)
{
    return stats_sum(xs, n) / (double)n;
}

double stats_variance(const double *xs, size_t n, bool sample)
{
    double mean = stats_mean(xs, n);
    double acc  = 0.0;
    for (size_t i = 0; i < n; i++) {
        double d = xs[i] - mean;
        acc += d * d;
    }
    double denom = sample ? (double)(n - 1) : (double)n;
    return acc / denom;
}

double stats_stddev(const double *xs, size_t n, bool sample)
{
    return sqrt(stats_variance(xs, n, sample));
}

/* ascending comparator for qsort over doubles */
static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* return a sorted malloc'd copy of xs (caller frees), or NULL on OOM */
static double *sorted_copy(const double *xs, size_t n)
{
    double *c = (double *)malloc(n * sizeof(double));
    if (!c)
        return NULL;
    for (size_t i = 0; i < n; i++)
        c[i] = xs[i];
    qsort(c, n, sizeof(double), cmp_double);
    return c;
}

double stats_median(const double *xs, size_t n)
{
    double *c = sorted_copy(xs, n);
    if (!c)
        return 0.0;
    double m;
    if (n % 2 == 1)
        m = c[n / 2];
    else
        m = 0.5 * (c[n / 2 - 1] + c[n / 2]);
    free(c);
    return m;
}

double stats_percentile(const double *xs, size_t n, double p)
{
    if (p < 0.0)   p = 0.0;
    if (p > 100.0) p = 100.0;
    double *c = sorted_copy(xs, n);
    if (!c)
        return 0.0;
    double result;
    if (n == 1) {
        result = c[0];
    } else {
        double rank = (p / 100.0) * (double)(n - 1);   /* fractional index */
        size_t lo   = (size_t)rank;
        double frac = rank - (double)lo;
        if (lo + 1 < n)
            result = c[lo] + frac * (c[lo + 1] - c[lo]);
        else
            result = c[lo];
    }
    free(c);
    return result;
}

double stats_mode(const double *xs, size_t n)
{
    double *c = sorted_copy(xs, n);
    if (!c)
        return 0.0;
    double best     = c[0];
    size_t best_run = 1;
    size_t run      = 1;
    for (size_t i = 1; i < n; i++) {
        if (c[i] == c[i - 1]) {
            run++;
        } else {
            run = 1;
        }
        if (run > best_run) {          /* strictly greater -> keeps the smaller value on ties */
            best_run = run;
            best     = c[i];
        }
    }
    free(c);
    return best;
}

double stats_skewness(const double *xs, size_t n)
{
    double mean = stats_mean(xs, n);
    double sd   = stats_stddev(xs, n, false);
    if (sd == 0.0)
        return 0.0;
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) {
        double z = (xs[i] - mean) / sd;
        acc += z * z * z;
    }
    return acc / (double)n;
}

double stats_covariance(const double *xs, const double *ys, size_t n)
{
    double mx = stats_mean(xs, n);
    double my = stats_mean(ys, n);
    double acc = 0.0;
    for (size_t i = 0; i < n; i++)
        acc += (xs[i] - mx) * (ys[i] - my);
    return acc / (double)n;
}

double stats_correlation(const double *xs, const double *ys, size_t n)
{
    double sx = stats_stddev(xs, n, false);
    double sy = stats_stddev(ys, n, false);
    if (sx == 0.0 || sy == 0.0)
        return 0.0;
    return stats_covariance(xs, ys, n) / (sx * sy);
}

void stats_linfit(const double *xs, const double *ys, size_t n,
                  double *slope, double *intercept)
{
    double mx = stats_mean(xs, n);
    double my = stats_mean(ys, n);
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < n; i++) {
        double dx = xs[i] - mx;
        num += dx * (ys[i] - my);
        den += dx * dx;
    }
    double b = (den == 0.0) ? 0.0 : num / den;
    *slope     = b;
    *intercept = my - b * mx;
}

void stats_cumsum(const double *xs, size_t n, double *out)
{
    double acc = 0.0;
    for (size_t i = 0; i < n; i++) {
        acc += xs[i];
        out[i] = acc;
    }
}

void stats_zscore(const double *xs, size_t n, double *out)
{
    double mean = stats_mean(xs, n);
    double sd   = stats_stddev(xs, n, false);
    for (size_t i = 0; i < n; i++)
        out[i] = (sd == 0.0) ? 0.0 : (xs[i] - mean) / sd;
}

void stats_normalize(const double *xs, size_t n, double *out)
{
    double lo = stats_min(xs, n);
    double hi = stats_max(xs, n);
    double range = hi - lo;
    for (size_t i = 0; i < n; i++)
        out[i] = (range == 0.0) ? 0.0 : (xs[i] - lo) / range;
}
