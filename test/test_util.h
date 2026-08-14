#ifndef RP_RADAR_TEST_UTIL_H
#define RP_RADAR_TEST_UTIL_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern int g_test_failures;
extern int g_test_checks;

#define CHECK(cond)                                                                       \
	do                                                                                    \
	{                                                                                     \
		g_test_checks++;                                                                  \
		if (!(cond))                                                                      \
		{                                                                                 \
			g_test_failures++;                                                            \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                      \
		}                                                                                 \
	} while (0)

#define CHECK_NEAR(actual, expected, tol)                                                 \
	do                                                                                    \
	{                                                                                     \
		g_test_checks++;                                                                  \
		const double a_ = (double)(actual);                                               \
		const double e_ = (double)(expected);                                             \
		if (!(fabs(a_ - e_) <= (double)(tol)))                                            \
		{                                                                                 \
			g_test_failures++;                                                            \
			printf("  FAIL %s:%d: %s = %g, expected %g +/- %g\n", __FILE__, __LINE__,     \
			       #actual, a_, e_, (double)(tol));                                       \
		}                                                                                 \
	} while (0)

#define RUN(fn)                                                                           \
	do                                                                                    \
	{                                                                                     \
		const int before = g_test_failures;                                               \
		printf("%-32s", #fn);                                                             \
		fflush(stdout);                                                                   \
		fn();                                                                             \
		printf("%s\n", (g_test_failures == before) ? " ok" : " FAILED");                  \
	} while (0)

void test_radar_math(void);
void test_fft(void);
void test_psd(void);
void test_peaks(void);
void test_range_doppler(void);
void test_detection_merging(void);
void test_interpolation_accuracy(void);
void test_dealias(void);
void test_range_walk(void);
void test_tracker(void);
void test_shot(void);
void test_shot_no_trigger(void);

#endif /* RP_RADAR_TEST_UTIL_H */
