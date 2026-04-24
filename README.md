# Pulse SDK

[한국어 문서 보기](README.ko.md)

Pulse SDK collects device metrics using
[libmcu/metrics](https://github.com/libmcu/libmcu/tree/main/modules/metrics)
and sends them to the [Pulse](https://pulse.libmcu.org) ingest server.

## Usage example

```c
#include "pulse/pulse.h"

static void update_metrics(void *ctx)
{
	metrics_set(SensorValue, get_your_sensor_value());
}

void example(void)
{
	struct pulse conf = {
		.token = "example-token",
		.serial_number = "device-1234",
		.software_version = "1.0.0",
	};

	if (pulse_init(&conf) != PULSE_STATUS_OK) {
		return;
	}

	pulse_set_prepare_handler(update_metrics, NULL);

	metrics_increase(RunCount);
	pulse_report();
}
```

> [!NOTE]
> Create or copy your authentication token from product setup on
> [Pulse](https://pulse.libmcu.org), then pass that token to `pulse_init()`.
> `token`, `serial_number`, and `software_version` are required fields in
> `struct pulse`. All three must be non-NULL, non-empty, null-terminated
> strings. The null terminator is not encoded into the payload.

Example metrics.def file:

```c
METRICS_DEFINE_COUNTER(RunCount)
METRICS_DEFINE(SensorValue)
```

## Platform integration

### Zephyr

Add as a [west module](https://docs.zephyrproject.org/latest/develop/modules.html)
in your manifest:

```yaml
manifest:
  projects:
    - name: pulse-sdk
      url: https://github.com/libmcu/pulse-sdk.git
      revision: main
      path: modules/lib/pulse-sdk
```

Enable the module in `prj.conf`:

```conf
CONFIG_PULSE_SDK=y
#CONFIG_PULSE_SDK_METRICS_USER_DEFINES=/path/to/metrics.def
```

> [!NOTE]
> The path to the metric definition file is set by `CONFIG_PULSE_SDK_METRICS_USER_DEFINES`.
> The default value is `include/metrics.def`.

### ESP-IDF

Clone or add as a git submodule under your project's `components/` directory:

```bash
cd components
git submodule add https://github.com/libmcu/pulse-sdk.git pulse-sdk
```

> [!NOTE]
> The metric definition file must be placed at `main/metrics.def`.

### Generic CMake projects

Add Pulse SDK as a subdirectory and link it to your target.

```cmake
add_subdirectory(path/to/pulse-sdk)
target_link_libraries(your_target PRIVATE pulse-sdk)
```

If dependency roots are not discoverable from your project layout, set them explicitly before adding the subdirectory:

```cmake
set(PULSE_SDK_LIBMCU_ROOT /path/to/libmcu)
set(PULSE_SDK_CBOR_ROOT /path/to/cbor)

add_subdirectory(path/to/pulse-sdk)
target_link_libraries(your_target PRIVATE pulse-sdk)
```

> [!IMPORTANT]
> When `PULSE_SDK_LIBMCU_ROOT` or `LIBMCU_ROOT` points to an external libmcu root
> (i.e. not the bundled `external/libmcu`), `pulse_sdk_collect()` does **not**
> automatically add metrics core sources, platform overrides, or transport.
> You must add the following sources to your build target manually:
>
> - `<libmcu>/modules/metrics/src/metrics.c`
> - `<libmcu>/modules/metrics/src/metrics_overrides.c`
> - `<libmcu>/modules/common/src/assert.c`
> - `ports/<platform>/pulse_overrides.c`
> - `ports/<platform>/pulse_transport_https.c`
> - `ports/pulse_metricfs_stub.c`

### Linux

On Linux, `pulse_sdk_collect()` adds:

- `ports/linux/pulse_overrides.c`
- `ports/linux/pulse_transport_https.c`

Basic integration is the same as generic CMake:

```cmake
add_subdirectory(path/to/pulse-sdk)
add_executable(app main.c)
target_link_libraries(app PRIVATE pulse-sdk)
```

### Baremetal

For Make-based baremetal projects, include `pulse-sdk.mk` and append the exported source and include lists.

```make
PULSE_SDK_ROOT ?= path/to/pulse-sdk
LIBMCU_ROOT ?= path/to/libmcu
CBOR_ROOT ?= path/to/cbor

include $(PULSE_SDK_ROOT)/pulse-sdk.mk

APP_SRCS += $(PULSE_SDK_SRCS)
APP_INCS += $(PULSE_SDK_INCS)
```

When `LIBMCU_ROOT` resolves to `$(PULSE_SDK_ROOT)/external/libmcu`, the Make integration also pulls in:

- `ports/baremetal/pulse_overrides.c`
- `ports/baremetal/pulse_transport_https.c`
- required bundled `libmcu` metrics sources

> [!IMPORTANT]
> When an external `LIBMCU_ROOT` is set (not the bundled `external/libmcu`),
> the Make integration does **not** automatically add metrics sources,
> platform overrides, or transport. Add them manually to your build.

## Integration requirements

Pulse SDK depends on
[libmcu](https://github.com/libmcu/libmcu) and
[cbor](https://github.com/libmcu/cbor).

When CMake integration is used, dependency roots are resolved in this order:

1. `PULSE_SDK_LIBMCU_ROOT` / `PULSE_SDK_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`
4. standalone CMake fetch fallback

When Make integration is used, `pulse-sdk.mk` resolves dependencies through:

1. `PULSE_SDK_LIBMCU_ROOT` / `PULSE_SDK_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`

Your application must provide required Pulse metadata directly via `struct pulse`:

- `token`
- `serial_number`
- `software_version`

Transmission happens through `pulse_transport_transmit(const void *data, size_t datasize, const struct pulse_report_ctx *ctx)`.

Platform ports included by Pulse SDK may provide timestamp and lock hooks, but the transmit path still needs a working implementation. On Linux and other generic ports, the built-in implementation is a weak stub that returns an I/O error unless your application overrides it.

The endpoint constants are defined in `include/pulse/pulse.h`:

- `PULSE_INGEST_HOST`: `ingest.libmcu.org`
- `PULSE_INGEST_PATH`: `/v1`
- `PULSE_INGEST_URL_HTTPS`: `https://ingest.libmcu.org/v1`
- `PULSE_INGEST_URL_COAPS`: `coaps://ingest.libmcu.org/v1`
