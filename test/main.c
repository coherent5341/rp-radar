#include "test_util.h"

int g_test_failures = 0;
int g_test_checks   = 0;

int main(void)
{
	printf("rp-radar DSP test suite\n\n");

	RUN(test_radar_math);
	RUN(test_fft);
	RUN(test_psd);
	RUN(test_peaks);
	RUN(test_range_doppler);
	RUN(test_detection_merging);
	RUN(test_interpolation_accuracy);
	RUN(test_dealias);
	RUN(test_range_walk);
	RUN(test_tracker);
	RUN(test_shot);
	RUN(test_shot_no_trigger);

	printf("\n%d checks, %d failures\n", g_test_checks, g_test_failures);

	return (g_test_failures == 0) ? 0 : 1;
}
