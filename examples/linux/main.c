/*
 * SPDX-FileCopyrightText: 2026 권경환 Kyunghwan Kwon <k@libmcu.org>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include "pulse/pulse.h"

#define EXAMPLE_TOKEN			"replace-with-real-token"
#define EXAMPLE_SERIAL_NUMBER		"SN-TEST01"
#define EXAMPLE_SOFTWARE_VERSION	"1.0.0-test"
#define EXAMPLE_METRIC_VALUE		24

#if defined(PULSE_TRANSPORT_COAPS) && PULSE_TRANSPORT_COAPS
#define EXAMPLE_TRANSPORT_NAME	"coaps"
#else
#define EXAMPLE_TRANSPORT_NAME	"https"
#endif

static void update_metrics(void *ctx)
{
	(void)ctx;
	metrics_set(PulseMetric, EXAMPLE_METRIC_VALUE);
}

static void print_response(const void *data, size_t datasize, void *ctx)
{
	(void)ctx;
	printf("server response (%zu bytes): %.*s\n", datasize,
			(int)datasize, (const char *)data);
}

static void call_this_periodically(void)
{
	/* In a real application, you would typically call pulse_report()
	* periodically, e.g. once per hour, to report updated metrics to Pulse
	* ingest. For this example, we just call it once from main(). */
	const pulse_status_t status = pulse_report();
	if (status != PULSE_STATUS_OK) {
		fprintf(stderr, "pulse_report failed: %s\n",
				pulse_stringify_status(status));
		return;
	}

	printf("reported PulseMetric=%d to Pulse ingest via %s as %s (%s)\n",
			EXAMPLE_METRIC_VALUE, EXAMPLE_TRANSPORT_NAME,
			EXAMPLE_SERIAL_NUMBER, EXAMPLE_SOFTWARE_VERSION);
}

int main(void)
{
	const pulse_status_t status = pulse_init(&(struct pulse) {
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

	pulse_set_prepare_handler(update_metrics, NULL);
	pulse_set_response_handler(print_response, NULL);

	call_this_periodically();

	return 0;
}
