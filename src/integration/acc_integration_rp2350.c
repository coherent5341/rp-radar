#include "acc_integration.h"

#include <stdlib.h>

#include "pico/time.h"

void acc_integration_sleep_us(uint32_t time_usec)
{
	busy_wait_us_32(time_usec);
}

void acc_integration_sleep_ms(uint32_t time_msec)
{
	sleep_ms(time_msec);
}

uint32_t acc_integration_get_time(void)
{
	return to_ms_since_boot(get_absolute_time());
}

uint32_t acc_integration_get_time_us(void)
{
	return time_us_32();
}

void *acc_integration_mem_alloc(size_t size)
{
	return malloc(size);
}

void acc_integration_mem_free(void *ptr)
{
	free(ptr);
}
