/*
 * Small platform services Acconeer's example applications expect to find.
 * Same names and signatures as the reference SDK ports.
 */
#ifndef ACC_INTEGRATION_H_
#define ACC_INTEGRATION_H_

#include <stddef.h>
#include <stdint.h>

/** Busy-wait for at least @p time_usec microseconds. */
void acc_integration_sleep_us(uint32_t time_usec);

/** Sleep for at least @p time_msec milliseconds. */
void acc_integration_sleep_ms(uint32_t time_msec);

/** Milliseconds since boot. Wraps after ~49 days; compare with subtraction. */
uint32_t acc_integration_get_time(void);

/** Microseconds since boot, for frame timing. Wraps after ~71 minutes. */
uint32_t acc_integration_get_time_us(void);

void *acc_integration_mem_alloc(size_t size);

void acc_integration_mem_free(void *ptr);

#endif /* ACC_INTEGRATION_H_ */
