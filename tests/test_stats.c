/*
 * test_stats.c -- the statistics standard-library natives (stats.{h,c}).
 *
 * Values are checked at Coil level with a tolerance, since the natives return
 * floats.  Error paths (empty input, non-numeric elements, too-few points for a
 * sample statistic, length mismatch) must fail cleanly as runtime errors.
 */
#include "../src/vm.h"
#include <stdio.h>
#include <string.h>

static int run(const char *src)
{
    VM vm;
    vm_init(&vm);
    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return r == INTERPRET_OK;
}

static int errs(const char *src)
{
    VM vm;
    vm_init(&vm);
    InterpretResult r = vm_interpret(&vm, src, strlen(src));
    vm_free(&vm);
    return r == INTERPRET_RUNTIME_ERROR;
}

int main(void)
{
    /* central tendency */
    if (!run(
        "let a = [2, 4, 4, 4, 5, 5, 7, 9];"
        "assert(abs(mean(a) - 5.0) < 0.0001);"
        "assert(abs(median(a) - 4.5) < 0.0001);"
        "assert(abs(median([3, 1, 2]) - 2.0) < 0.0001);"
        "assert(abs(mode(a) - 4.0) < 0.0001);"
        "assert(abs(mode([1, 1, 2, 2, 3]) - 1.0) < 0.0001);"      /* tie -> smaller */
        "assert(abs(product([1, 2, 3, 4]) - 24.0) < 0.0001);")) { puts("FAIL stats.central"); return 1; }

    /* spread: population vs sample */
    if (!run(
        "let a = [2, 4, 4, 4, 5, 5, 7, 9];"
        "assert(abs(pvariance(a) - 4.0) < 0.0001);"
        "assert(abs(pstdev(a) - 2.0) < 0.0001);"
        "assert(abs(variance([2, 4, 6]) - 4.0) < 0.0001);"        /* sample: /(n-1) */
        "assert(abs(stdev([2, 4, 6]) - 2.0) < 0.0001);")) { puts("FAIL stats.spread"); return 1; }

    /* quantiles */
    if (!run(
        "let a = [1, 2, 3, 4, 5];"
        "assert(abs(percentile(a, 0) - 1.0) < 0.0001);"
        "assert(abs(percentile(a, 50) - 3.0) < 0.0001);"
        "assert(abs(percentile(a, 100) - 5.0) < 0.0001);"
        "assert(abs(percentile(a, 25) - 2.0) < 0.0001);")) { puts("FAIL stats.quantile"); return 1; }

    /* paired: correlation / covariance / linear fit */
    if (!run(
        "assert(abs(correlation([1, 2, 3], [2, 4, 6]) - 1.0) < 0.0001);"
        "assert(abs(correlation([1, 2, 3], [6, 4, 2]) + 1.0) < 0.0001);"   /* -1 */
        "let f = linfit([0, 1, 2, 3], [1, 3, 5, 7]);"                       /* y = 1 + 2x */
        "assert(abs(f[0] - 2.0) < 0.0001);"
        "assert(abs(f[1] - 1.0) < 0.0001);"
        "assert(abs(covariance([1, 2, 3], [1, 2, 3]) - 0.6666) < 0.001);")) { puts("FAIL stats.paired"); return 1; }

    /* transforms return arrays */
    if (!run(
        "let c = cumsum([1, 2, 3, 4]);"
        "assert(len(c) == 4); assert(c[0] == 1.0); assert(c[3] == 10.0);"
        "let z = zscore([2, 4, 4, 4, 5, 5, 7, 9]);"                         /* mean 5, sd 2 */
        "assert(abs(z[0] + 1.5) < 0.0001);"                                 /* (2-5)/2 */
        "let nm = normalize([0, 5, 10]);"
        "assert(abs(nm[0]) < 0.0001); assert(abs(nm[1] - 0.5) < 0.0001); assert(abs(nm[2] - 1.0) < 0.0001);"
        "let flat = normalize([3, 3, 3]);"                                  /* zero range -> zeros */
        "assert(abs(flat[0]) < 0.0001);")) { puts("FAIL stats.transform"); return 1; }

    /* skew: symmetric data has ~zero skew, right-tailed is positive */
    if (!run(
        "assert(abs(skew([1, 2, 3, 4, 5])) < 0.0001);"
        "assert(skew([1, 1, 1, 1, 10]) > 0.5);")) { puts("FAIL stats.skew"); return 1; }

    /* single-element inputs where defined */
    if (!run(
        "assert(mean([7]) == 7.0);"
        "assert(median([7]) == 7.0);"
        "assert(percentile([7], 50) == 7.0);"
        "assert(pstdev([7]) == 0.0);")) { puts("FAIL stats.single"); return 1; }

    /* error paths */
    if (!errs("print mean([]);"))                     { puts("FAIL stats.err.empty"); return 1; }
    if (!errs("print mean([1, \"x\", 3]);"))          { puts("FAIL stats.err.nonnum"); return 1; }
    if (!errs("print stdev([5]);"))                   { puts("FAIL stats.err.sample1"); return 1; }
    if (!errs("print variance([5]);"))                { puts("FAIL stats.err.var1"); return 1; }
    if (!errs("print correlation([1, 2], [1, 2, 3]);")) { puts("FAIL stats.err.len"); return 1; }
    if (!errs("print median(5);"))                    { puts("FAIL stats.err.type"); return 1; }
    if (!errs("print correlation([1], [1]);"))        { puts("FAIL stats.err.corr1"); return 1; }

    puts("PASS test_stats");
    return 0;
}
