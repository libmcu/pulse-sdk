extern "C" {
#include "esp_timer.h"
}

static int64_t g_time_us;
static int64_t g_step_us;

extern "C" int64_t esp_timer_get_time(void)
{
	int64_t now = g_time_us;

	g_time_us += g_step_us;

	return now;
}

extern "C" void esp_timer_mock_reset(void)
{
	g_time_us = 0;
	g_step_us = 0;
}

extern "C" void esp_timer_mock_set_time(int64_t time_us)
{
	g_time_us = time_us;
}

extern "C" void esp_timer_mock_set_step(int64_t step_us)
{
	g_step_us = step_us;
}
