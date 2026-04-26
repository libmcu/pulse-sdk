/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <unistd.h>

#include "pulse/pulse.h"

#define EXAMPLE_TOKEN			"replace-with-real-token"
#define EXAMPLE_SERIAL_NUMBER		"SN-TEST01"
#define EXAMPLE_SOFTWARE_VERSION	"1.0.0-test"
#define EXAMPLE_METRIC_VALUE		24
#define EXAMPLE_REPORT_COUNT		3

static void update_metrics(void *ctx)
{
	int *example_metric_value = (int *)ctx;
	metrics_set(PulseMetric, *example_metric_value);
}

static void print_response(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	printf("server response (%zu bytes): %.*s\n", datasize,
			(int)datasize, (const char *)data);
}

int main(void)
{
	int example_metric_value = EXAMPLE_METRIC_VALUE;
	pulse_status_t status = pulse_init(&(struct pulse) {
		.token = EXAMPLE_TOKEN,
		.serial_number = EXAMPLE_SERIAL_NUMBER,
		.software_version = EXAMPLE_SOFTWARE_VERSION,
		.transmit_timeout_ms = 15000u,
	});

	if (status != PULSE_STATUS_OK) {
		fprintf(stderr, "pulse_init failed: %s\n",
				pulse_stringify_status(status));
		return 1;
	}

	pulse_set_prepare_handler(update_metrics, &example_metric_value);
	pulse_set_response_handler(print_response, NULL);

	for (int i = 0; i < EXAMPLE_REPORT_COUNT; i++) {
		status = pulse_report();
		example_metric_value++;

		if (status != PULSE_STATUS_OK) {
			fprintf(stderr, "pulse_report failed: %s\n",
					pulse_stringify_status(status));
			continue;
		}

		printf("reported PulseMetric=%d to Pulse ingest as %s (%s)\n",
				example_metric_value, EXAMPLE_SERIAL_NUMBER,
				EXAMPLE_SOFTWARE_VERSION);

		if (i + 1 < EXAMPLE_REPORT_COUNT) {
			sleep(60);
		}
	}

	return 0;
}
