# Pulse SDK

[English README](README.md)

Pulse SDK는 [libmcu/metrics](https://github.com/libmcu/libmcu/metrics)를
사용하여 디바이스 메트릭을 수집하고,
이를 [Pulse](https://pulse.libmcu.org) ingest 서버로 전송합니다.

## 사용 예제

```c
#include "pulse/pulse.h"

static void update_metrics(void *ctx)
{
	metrics_set(SensorValue, get_your_sensor_value());
}

void example(void)
{
	struct pulse conf = { .token = "example-token" };

	if (pulse_init(&conf) != PULSE_STATUS_OK) {
		return;
	}

	pulse_set_prepare_handler(update_metrics, NULL);

	metrics_increase(RunCount);
	pulse_report();
}
```

> [!NOTE]
> 인증 토큰은 [Pulse](https://pulse.libmcu.org) 의 product setup에서 생성하시거나,
> 이미 발급된 토큰을 그대로 사용하시면 됩니다. 준비된 토큰은 `pulse_init()`에 전달하시면 됩니다.

metrics.def 파일 예시:

```c
METRICS_DEFINE_COUNTER(RunCount)
METRICS_DEFINE(SensorValue)
```

## 플랫폼별 통합 방법

### Zephyr

west manifest에
[west 모듈](https://docs.zephyrproject.org/latest/develop/modules.html)로 추가합니다.

```yaml
manifest:
  projects:
    - name: pulse-sdk
      url: https://github.com/libmcu/pulse-sdk.git
      revision: main
      path: modules/lib/pulse-sdk
```

`prj.conf`에서 모듈을 활성화합니다.

```conf
CONFIG_PULSE_SDK=y
#CONFIG_PULSE_SDK_METRICS_USER_DEFINES=/path/to/metrics.def
```

> [!NOTE]
> 메트릭 정의 파일 경로는 `CONFIG_PULSE_SDK_METRICS_USER_DEFINES`로 설정됩니다.
> 기본값은 `include/metrics.def` 입니다.

### ESP-IDF

프로젝트의 `components/` 디렉터리 아래에 SDK를 git 서브모듈로 추가하거나 클론합니다.

```bash
cd components
git submodule add https://github.com/libmcu/pulse-sdk.git pulse-sdk
```

> [!NOTE]
> 메트릭 정의 파일 경로는 `main/metrics.def` 입니다.

### 일반 CMake 프로젝트

Pulse SDK를 subdirectory로 추가하고 대상에 링크하시면 됩니다.

```cmake
add_subdirectory(path/to/pulse-sdk)
target_link_libraries(your_target PRIVATE pulse-sdk)
```

프로젝트 구조상 의존성 경로를 자동 탐지할 수 없으면, subdirectory 추가 전에 명시적으로 지정하시면 됩니다.

```cmake
set(PULSE_SDK_LIBMCU_ROOT /path/to/libmcu)
set(PULSE_SDK_CBOR_ROOT /path/to/cbor)

add_subdirectory(path/to/pulse-sdk)
target_link_libraries(your_target PRIVATE pulse-sdk)
```

### Linux

Linux에서는 `pulse_sdk_collect()`가 아래 파일을 추가합니다.

- `ports/linux/pulse_overrides.c`
- `ports/linux/pulse_transport_https.c`

기본 통합 방법은 일반 CMake와 동일합니다.

```cmake
add_subdirectory(path/to/pulse-sdk)
add_executable(app main.c)
target_link_libraries(app PRIVATE pulse-sdk)
```

### Baremetal

Make 기반 baremetal 프로젝트에서는 `pulse-sdk.mk`를 include한 뒤, export되는 소스 목록과 include 경로를 애플리케이션 빌드 변수에 추가하시면 됩니다.

```make
PULSE_SDK_ROOT ?= path/to/pulse-sdk
LIBMCU_ROOT ?= path/to/libmcu
CBOR_ROOT ?= path/to/cbor

include $(PULSE_SDK_ROOT)/pulse-sdk.mk

APP_SRCS += $(PULSE_SDK_SRCS)
APP_INCS += $(PULSE_SDK_INCS)
```

`LIBMCU_ROOT`가 `$(PULSE_SDK_ROOT)/external/libmcu`로 해석되면, Make 통합은 아래 항목도 함께 포함합니다.

- `ports/baremetal/pulse_overrides.c`
- `ports/baremetal/pulse_transport_https.c`
- bundled `libmcu` metrics 관련 필수 소스

## 통합 전 요구 사항

Pulse SDK는 [libmcu](https://github.com/libmcu/libmcu)와
[cbor](https://github.com/libmcu/cbor)에 의존합니다.

CMake 통합 시 의존성 루트는 아래 순서로 해석됩니다.

1. `PULSE_SDK_LIBMCU_ROOT` / `PULSE_SDK_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`
4. standalone CMake fetch fallback

Make 통합 시 `pulse-sdk.mk`는 아래 순서로 의존성을 해석합니다.

1. `PULSE_SDK_LIBMCU_ROOT` / `PULSE_SDK_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`

또한 애플리케이션은 `libmcu/metrics`가 기대하는 제품 메타데이터 hook를 제공해야 합니다.

- `metrics_get_serial_number_string()`
- `metrics_get_version_string()`

실제 전송은 `pulse_transport_transmit(const void *data, size_t datasize, const struct pulse_report_ctx *ctx)`를 통해 수행됩니다.

Pulse SDK에 포함된 플랫폼 port는 timestamp 및 lock hook를 제공할 수 있지만, 전송 경로까지 항상 완성해 주지는 않습니다. Linux를 포함한 일반 port에서는 기본 구현이 weak stub이며, 애플리케이션이 override하지 않으면 I/O 오류를 반환합니다.

Endpoint 상수는 `include/pulse/pulse.h`에 정의되어 있습니다.

- `PULSE_INGEST_HOST`: `ingest.libmcu.org`
- `PULSE_INGEST_PATH`: `/v1`
- `PULSE_INGEST_URL_HTTPS`: `https://ingest.libmcu.org/v1`
- `PULSE_INGEST_URL_COAPS`: `coaps://ingest.libmcu.org/v1`
