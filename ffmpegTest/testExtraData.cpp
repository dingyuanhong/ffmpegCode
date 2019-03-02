#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"
#include <Windows.h>

//"../Vid0616000023.mp4"
int getExtraData(const char * file)
{
	avcodec_register_all();

	EvoMediaSource source;

	//file += "gopro.mp4";
	EvoMediaSourceConfig config = { true,true };
	int ret = source.Open(file, &config);
	if (ret != 0)
	{
		return -1;
	}

	uint8_t buf[255] = {0};
	int count = source.GetSPS(buf,255);
	printf("SPS:");
	for (int i = 0; i < count; i++)
	{
		printf("%02x ",buf[i]);
	}
	printf("\nPPS:");
	count = source.GetPPS(buf, 255);
	for (int i = 0; i < count; i++)
	{
		printf("%02x ", buf[i]);
	}
	printf("\n");
	return 0;
}