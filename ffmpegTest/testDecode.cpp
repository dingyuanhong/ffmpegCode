#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"

//"../video.264"
int testDecode(const char * file)
{
	av_register_all();

	AVCodecContext	*codecContext = NULL;
	AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec) return -1;
	codecContext = avcodec_alloc_context3(codec);

	if (avcodec_open2(codecContext, codec, NULL) < 0)
	{
		return -1;
	}
	VideoDecoder *decoder = NULL;
	decoder = new VideoDecoder(codecContext);

	FILE *fp = fopen(file, "rb");
	if (fp == NULL)
	{
		delete decoder;
		decoder = NULL;

		avcodec_close(codecContext);
		avcodec_free_context(&codecContext);

		return -1;
	}
	fseek(fp, 0, SEEK_END);
	int size = ftell(fp);
	uint8_t * buffer = (uint8_t*)malloc(size);
	fseek(fp, 0, SEEK_SET);
	size_t red = fread(buffer, 1, size, fp);
	EvoPacket packet = { 0 };
	packet.data = buffer;
	packet.size = size;

	AVFrame *outFrame = NULL;
	int ret = decoder->DecodePacket(&packet, &outFrame);
	if (outFrame != NULL)
	{
		FreeAVFrame(&outFrame);
	}

	while (ret == 1)
	{
		ret = decoder->DecodePacket(NULL, &outFrame);
		if (outFrame != NULL)
		{
			FreeAVFrame(&outFrame);
		}
	}

	delete decoder;
	decoder = NULL;

	avcodec_close(codecContext);
	avcodec_free_context(&codecContext);

	return 0;
}