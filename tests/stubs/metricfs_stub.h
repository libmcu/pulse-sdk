#ifndef PULSE_TEST_METRICFS_STUB_H
#define PULSE_TEST_METRICFS_STUB_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void metricfs_stub_reset(void);
void metricfs_stub_prime(const void *data, size_t datasize, uint16_t count);
void metricfs_stub_set_peek_first_error(int err);
void metricfs_stub_set_write_error(int err);
const void *metricfs_stub_data(void);
size_t metricfs_stub_size(void);
const void *metricfs_stub_data_at(uint16_t index);
size_t metricfs_stub_size_at(uint16_t index);

#if defined(__cplusplus)
}
#endif

#endif /* PULSE_TEST_METRICFS_STUB_H */
