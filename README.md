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
> [Pulse](https://pulse.libmcu.org). Pass that token to `pulse_init()` or
> set it later via `pulse_update_token()`.
> `token`, `serial_number`, and `software_version` are required fields in
> `struct pulse`. All three must be non-NULL, non-empty, null-terminated
> strings. The null terminator is not encoded into the payload.

Example metrics.def file:

```c
METRICS_DEFINE_COUNTER(RunCount)
METRICS_DEFINE(SensorValue)
```

## Platform integration

### Selecting HTTPS or CoAPS

Pulse SDK supports two transports:

- `coaps` — default. Uses CoAP over DTLS PSK.
- `https` — uses HTTPS over TLS.

When `coaps` is selected, the token passed to `pulse_init()` is reused as the
DTLS PSK, and the SDK derives the DTLS PSK identity internally.

Platform-specific selection methods:

- **Zephyr**: choose `CONFIG_PULSE_TRANSPORT_COAPS=y` (default) or
  `CONFIG_PULSE_TRANSPORT_HTTPS=y` in `prj.conf`.
- **ESP-IDF**: choose the transport in `menuconfig`.
- **Generic CMake / Linux**: set `PULSE_TRANSPORT` before adding the SDK
  subdirectory.
- **Baremetal Make**: the Make integration auto-adds the HTTPS
  transport only. To use CoAPS, add the CoAPS transport source manually.

### Zephyr

Add as a [west module](https://docs.zephyrproject.org/latest/develop/modules.html)
in your manifest:

```yaml
manifest:
  projects:
    - name: pulse
      url: https://github.com/libmcu/pulse-sdk.git
      revision: main
      path: modules/lib/pulse
```

Enable the module in `prj.conf`:

```conf
CONFIG_PULSE=y
CONFIG_PULSE_TRANSPORT_COAPS=y
#CONFIG_PULSE_TRANSPORT_HTTPS=y
#CONFIG_PULSE_METRICS_USER_DEFINES=/path/to/metrics.def
```

> [!NOTE]
> The path to the metric definition file is set by `CONFIG_PULSE_METRICS_USER_DEFINES`.
> The default value is `include/metrics.def`.

### ESP-IDF

Clone or add as a git submodule under your project's `components/` directory:

```bash
cd components
git submodule add https://github.com/libmcu/pulse-sdk.git pulse
```

Select the transport in `menuconfig`:

```text
Component config  --->
  Pulse  --->
    (X) CoAPS (CoAP over DTLS PSK)
    ( ) HTTPS
```

CoAPS is the default.

> [!NOTE]
> The metric definition file must be placed at `main/metrics.def`.

### Generic CMake projects

Add Pulse SDK as a subdirectory and link it to your target.

```cmake
set(METRICS_USER_DEFINES "${CMAKE_CURRENT_SOURCE_DIR}/metrics.def")
set(PULSE_TRANSPORT coaps CACHE STRING "") # default
# set(PULSE_TRANSPORT https CACHE STRING "")

add_subdirectory(path/to/pulse)
target_link_libraries(your_target PRIVATE pulse)
```

If dependency roots are not discoverable from your project layout, set them explicitly before adding the subdirectory:

```cmake
set(PULSE_LIBMCU_ROOT /path/to/libmcu)
set(PULSE_CBOR_ROOT /path/to/cbor)
set(METRICS_USER_DEFINES "${CMAKE_CURRENT_SOURCE_DIR}/metrics.def")

add_subdirectory(path/to/pulse)
target_link_libraries(your_target PRIVATE pulse)
```

If your application already builds and links `libmcu` on its own, keep doing so.
Pulse SDK uses `libmcu` as a public dependency, while `cbor` stays internal to
the SDK build.

If your application wants to use regular `libmcu` modules such as `retry` or
`ratelim`, prefer reusing the dependency target resolved by Pulse SDK instead of
cloning and linking a second `libmcu` copy:

```cmake
target_link_libraries(your_target PRIVATE
	pulse
	pulse::libmcu)
```

### Linux

On Linux, `pulse_collect()` adds:

- `ports/linux/pulse_overrides.c`
- `ports/linux/pulse_transport_<transport>.c`

Basic integration is the same as generic CMake:

```cmake
set(METRICS_USER_DEFINES "${CMAKE_CURRENT_SOURCE_DIR}/metrics.def")
set(PULSE_TRANSPORT coaps CACHE STRING "") # default
# set(PULSE_TRANSPORT https CACHE STRING "")

add_subdirectory(path/to/pulse)
add_executable(app main.c)
target_link_libraries(app PRIVATE pulse)
```

### Baremetal

For Make-based baremetal projects, include `pulse.mk` and append the exported source and include lists.

```make
PULSE_ROOT ?= path/to/pulse
LIBMCU_ROOT ?= path/to/libmcu
CBOR_ROOT ?= path/to/cbor

include $(PULSE_ROOT)/pulse.mk

APP_SRCS += $(PULSE_SRCS)
APP_INCS += $(PULSE_INCS)
# LDFLAGS += $(PULSE_LDFLAGS)  # only needed if you archive Pulse SDK separately
```

When `LIBMCU_ROOT` resolves to `$(PULSE_ROOT)/external/libmcu`, the Make integration also pulls in:

- `ports/baremetal/pulse_overrides.c`
- `ports/baremetal/pulse_transport_https.c`
- required bundled `libmcu` metrics sources

If your project already builds `cbor` separately, switch from `PULSE_SRCS`
to `PULSE_CORE_SRCS` and keep your existing `cbor` ownership:

```make
APP_SRCS += $(PULSE_CORE_SRCS)
APP_SRCS += $(YOUR_CBOR_SRCS)
APP_INCS += $(PULSE_INCS)
```

To switch the baremetal Make integration to CoAPS, remove
`ports/baremetal/pulse_transport_https.c` from your build and add
`ports/baremetal/pulse_transport_coaps.c` instead.

## API Reference
### Metric definition macros

- `METRICS_DEFINE_COUNTER(name)`
  - Monotonically increasing integer. Use for event counts.
- `METRICS_DEFINE_GAUGE(name, min, max)`
  - Bounded numeric value. Specify min and max.
- `METRICS_DEFINE_PERCENTAGE(name)`
  - Integer value in the 0–100 range.
- `METRICS_DEFINE_TIMER(name, unit)`
  - Time duration. Units: s, ms.
- `METRICS_DEFINE_STATE(name)`
  - Discrete state code.
- `METRICS_DEFINE_BINARY(name)`
  - Boolean flag: 0 or 1.
- `METRICS_DEFINE_BYTES(name)`
  - Byte count.
- `METRICS_DEFINE(name)`
  - Raw numeric value with no semantic constraint.

### Metric manipulation functions

- `metrics_set(name, val)`
  - Set the metric value.
- `metrics_increase(name)`
  - Increment a counter by 1.
- `metrics_increase_by(name, val)`
  - Increment a counter by a given amount.
- `metrics_reset(name)`
  - Reset a metric to its initial value.

## Notes

Pulse SDK depends on
[libmcu](https://github.com/libmcu/libmcu) and
[cbor](https://github.com/libmcu/cbor).

- `libmcu` is a public dependency. `pulse/pulse.h` exposes libmcu metrics types
  and applications are expected to call `metrics_*()` directly.
- `cbor` is an internal dependency. Pulse SDK builds against it privately and
  does not expose it in the public API.

When CMake integration is used, dependency roots are resolved in this order:

1. `PULSE_LIBMCU_ROOT` / `PULSE_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`
4. standalone CMake fetch fallback

When Make integration is used, `pulse.mk` resolves dependencies through:

1. `PULSE_LIBMCU_ROOT` / `PULSE_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`

Your application must provide required Pulse metadata directly via `struct pulse`:

- `token`
- `serial_number`
- `software_version`

> [!IMPORTANT]
> When `PULSE_LIBMCU_ROOT` or `LIBMCU_ROOT` points to an external libmcu root
> (i.e. not the bundled `external/libmcu`), `pulse_collect()` still adds
> Pulse SDK's own platform override and transport sources automatically.
> You only need to ensure the following libmcu implementation is linked into the
> final application, either from your existing libmcu target/library or by
> adding these sources manually:
>
> - `<libmcu>/modules/metrics/src/metrics.c`
> - `<libmcu>/modules/metrics/src/metricfs.c`
> - `<libmcu>/modules/common/src/assert.c`
> - `<libmcu>/modules/common/src/base64.c` (when `PULSE_TRANSPORT=coaps`)
