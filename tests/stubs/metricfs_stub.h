#ifndef PULSE_SDK_TEST_METRICFS_STUB_H
#define PULSE_SDK_TEST_METRICFS_STUB_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void metricfs_stub_reset(void);
void metricfs_stub_prime(const void *data, size_t datasize, uint16_t count);

#if defined(__cplusplus)
}
#endif

#endif /* PULSE_SDK_TEST_METRICFS_STUB_H */
