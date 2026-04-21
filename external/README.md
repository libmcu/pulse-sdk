# external

이 디렉터리는 fallback dependency 위치임.

기본 정책은 상위 프로젝트가 이미 제공하는 `libmcu`, `cbor`를 우선 재사용하는 것임.

필요할 때만 아래 구조로 둘 수 있음.

```text
external/
├── libmcu/
└── cbor/
```

`pulse-sdk`는 이 경로를 자동으로 마지막 fallback으로만 사용함.
