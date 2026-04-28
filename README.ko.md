# Pulse SDK

[English README](README.md)

Pulse SDK는 [libmcu/metrics](https://github.com/libmcu/libmcu/tree/main/modules/metrics)를
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
> 인증 토큰은 [Pulse](https://pulse.libmcu.org) 의 product setup에서 생성하거나,
> 이미 발급된 토큰을 그대로 사용하면 됩니다. 준비된 토큰은 `pulse_init()` 또는
> `pulse_update_token()`으로 설정할 수 있습니다.
> `struct pulse`의 `token`, `serial_number`, `software_version`은 필수 항목입니다.
> 세 값 모두 `NULL`이 아니고 비어 있지 않은 null-terminated 문자열이어야 합니다.
> null terminator 자체는 payload에 인코딩되지 않습니다.

metrics.def 파일 예시:

```c
METRICS_DEFINE_COUNTER(RunCount)
METRICS_DEFINE(SensorValue)
```

## 플랫폼별 연동 방법

### HTTPS / CoAPS 선택 방법

Pulse SDK는 두 가지 transport를 지원합니다.

- `coaps` — 기본값. CoAP over DTLS PSK 사용
- `https` — HTTPS over TLS 사용

`coaps`를 선택하면 `pulse_init()`에 전달한 token을 DTLS PSK로 재사용하고,
DTLS PSK identity는 SDK가 내부에서 계산합니다.

플랫폼별 선택 방법은 아래와 같습니다:

- **Zephyr**: `prj.conf`에서 `CONFIG_PULSE_SDK_TRANSPORT_COAPS=y`(기본값) 또는
  `CONFIG_PULSE_SDK_TRANSPORT_HTTPS=y` 선택하면 됩니다.
- **ESP-IDF**: `menuconfig`에서 transport 선택하면 됩니다.
- **일반 CMake / Linux**: SDK를 `add_subdirectory()` 하기 전에
  `PULSE_SDK_TRANSPORT` 설정하면 됩니다.
- **Baremetal Make**: 번들 Make 통합은 HTTPS transport만 자동 추가합니다.
  CoAPS를 사용하려면 transport 소스를 수동으로 바꿔 넣어야 합니다.

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
CONFIG_PULSE_SDK_TRANSPORT_COAPS=y
#CONFIG_PULSE_SDK_TRANSPORT_HTTPS=y
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

transport는 `menuconfig`에서 선택하면 됩니다.

```text
Component config  --->
  Pulse SDK  --->
    (X) CoAPS (CoAP over DTLS PSK)
    ( ) HTTPS
```

기본값은 CoAPS입니다.

> [!NOTE]
> 메트릭 정의 파일 경로는 `main/metrics.def` 입니다.

### 일반 CMake 프로젝트

Pulse SDK를 subdirectory로 추가하고 대상에 링크하시면 됩니다.

```cmake
set(PULSE_SDK_TRANSPORT coaps CACHE STRING "") # 기본값
# set(PULSE_SDK_TRANSPORT https CACHE STRING "")

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
- `ports/linux/pulse_transport_<transport>.c`

기본 통합 방법은 일반 CMake와 동일합니다.

```cmake
set(PULSE_SDK_TRANSPORT coaps CACHE STRING "") # 기본값
# set(PULSE_SDK_TRANSPORT https CACHE STRING "")

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

Baremetal Make 통합을 CoAPS로 바꾸려면,
`ports/baremetal/pulse_transport_https.c` 대신
`ports/baremetal/pulse_transport_coaps.c`를 빌드에 직접 추가하면 됩니다.

> [!IMPORTANT]
> 외부 `LIBMCU_ROOT`를 지정한 경우(번들로 제공되는 `external/libmcu`가 아닌 경우),
> Make 통합은 metrics 소스, 플랫폼 오버라이드, 전송 모듈을 자동으로 추가하지 않습니다.
> 빌드에 직접 추가해야 합니다.

## 메트릭 API
### 메트릭 정의 매크로

- `METRICS_DEFINE_COUNTER(name)`
  - 단조 증가 정수입니다. 이벤트 횟수 기록에 사용합니다.
- `METRICS_DEFINE_GAUGE(name, min, max)`
  - 범위가 있는 수치입니다. min과 max를 지정합니다.
- `METRICS_DEFINE_PERCENTAGE(name)`
  - 0~100 범위의 정수입니다.
- `METRICS_DEFINE_TIMER(name, unit)`
  - 시간 지속값입니다. 단위: s, ms.
- `METRICS_DEFINE_STATE(name)`
  - 이산 상태 코드입니다.
- `METRICS_DEFINE_BINARY(name)`
  - 이진 플래그로, 0 또는 1입니다.
- `METRICS_DEFINE_BYTES(name)`
  - 바이트 수입니다.
- `METRICS_DEFINE(name)`
  - 의미론적 제약이 없는 원시 수치입니다.

### 메트릭 설정 API

- `metrics_set(name, val)`
  - 메트릭 값을 설정합니다.
- `metrics_increase(name)`
  - 카운터를 1 증가시킵니다.
- `metrics_increase_by(name, val)`
  - 카운터를 지정한 값만큼 증가시킵니다.
- `metrics_reset(name)`
  - 메트릭을 초기값으로 초기화합니다.

## 참고

Pulse SDK는 [libmcu](https://github.com/libmcu/libmcu)와
[cbor](https://github.com/libmcu/cbor)에 의존합니다.

CMake 통합 시 의존성 루트는 아래 순서로 해석됩니다.

1. `PULSE_SDK_LIBMCU_ROOT` / `PULSE_SDK_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`
4. CMake fetch 폴백

Make 통합 시 `pulse-sdk.mk`는 아래 순서로 의존성을 해석합니다.

1. `PULSE_SDK_LIBMCU_ROOT` / `PULSE_SDK_CBOR_ROOT`
2. `LIBMCU_ROOT` / `CBOR_ROOT`
3. `external/libmcu` / `external/cbor`

애플리케이션은 필수 Pulse 메타데이터를 `struct pulse`로 직접 제공해야 합니다.

- `token`
- `serial_number`
- `software_version`

> [!IMPORTANT]
> `PULSE_SDK_LIBMCU_ROOT` 또는 `LIBMCU_ROOT`가 외부 libmcu 루트
> (번들로 제공되는 `external/libmcu`가 아닌 경로)를 가리키는 경우,
> `pulse_sdk_collect()`는 metrics 코어 소스, 플랫폼 오버라이드, 전송 모듈을
> 자동으로 추가하지 않습니다. 아래 소스를 빌드 대상에 직접 추가해야 합니다.
>
> - `<libmcu>/modules/metrics/src/metrics.c`
> - `<libmcu>/modules/metrics/src/metricfs.c`
> - `<libmcu>/modules/common/src/assert.c`
> - `<libmcu>/modules/common/src/base64.c` (`PULSE_SDK_TRANSPORT=coaps` 사용 시)
> - `ports/<platform>/pulse_overrides.c`
> - `ports/<platform>/pulse_transport_<transport>.c`
