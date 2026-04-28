#include "pulse/pulse.h"

#include "cbor/base.h"
#include "cbor/encoder.h"

int main(void)
{
	struct pulse pulse = {
		.token = "1234567890123456789012345678901234567890123",
		.serial_number = "device-2",
		.software_version = "1.0.0",
	};
	cbor_writer_t writer;
	unsigned char buf[8];

	cbor_writer_init(&writer, buf, sizeof(buf));
	cbor_encode_map(&writer, 0);

	return pulse_init(&pulse);
}
