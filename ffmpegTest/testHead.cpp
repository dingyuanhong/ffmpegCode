#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"

//"../Vid0616000023.mp4"
inline int TestHeader(const char *file)
{
	avcodec_register_all();

	EvoMediaSource source;
	printf("≤‚ ‘ø™ º! %lld\n", av_gettime() / 1000);
	source.SetVideoCodecName("h264");
	int ret = source.Open(file);
	if (ret != 0)
	{
		return -1;
	}
	printf("≤‚ ‘Ω· ¯! %lld\n", av_gettime() / 1000);
	return 1;
}