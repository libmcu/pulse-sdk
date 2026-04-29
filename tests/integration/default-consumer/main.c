#include "pulse/pulse.h"

#include "libmcu/ratelim.h"
#include "libmcu/retry.h"

static struct retry retry_ctx;
static struct ratelim limiter;

static void prepare_metrics(void *ctx)
{
	(void)ctx;

	if (ratelim_request(&limiter)) {
		metrics_increase(RunCount);
	}
}

int main(void)
{
	struct retry_param retry_param = {
		.min_backoff_ms = 1000,
		.max_backoff_ms = 8000,
		.max_attempts = 3,
		.max_jitter_ms = 200,
	};
	struct pulse pulse = {
		.token = "1234567890123456789012345678901234567890123",
		.serial_number = "device-1",
		.software_version = "1.0.0",
	};

	retry_new_static(&retry_ctx, &retry_param);
	ratelim_init(&limiter, RATELIM_UNIT_SECOND, 4, 1);
	pulse_init(&pulse);
	pulse_set_prepare_handler(prepare_metrics, NULL);

	return retry_exhausted(&retry_ctx) ? 1 : 0;
}
