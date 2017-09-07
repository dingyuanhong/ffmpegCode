#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"
#include "ImageFile.h"

//std::string path = "../temp_frame/";
inline int testDecodePath(std::string path)
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

	EvoVideoConvert convert;
	struct EvoVideoInfo info;
	info.Width = 0;
	info.Height = 0;
	info.Format = AV_PIX_FMT_NONE;

	struct EvoVideoInfo des = info;
	des.Width = 800;
	des.Height = 600;
	des.Format = AV_PIX_FMT_BGR24;
	convert.Initialize(info, des);

	if (decoder != NULL)
	{
		decoder->Attach(&convert);
	}

	AVFrame *outFrame = NULL;
	int ret = 0;
	int index = 0;

	while (true) {
		char Value[64] = { 0 };
		sprintf(Value, "%d", index);
		index++;
		std::string file = path + Value + "_frame.txt";
		FILE *fp = fopen(file.c_str(), "rb");
		if (fp == NULL)
		{
			break;
		}
		fseek(fp, 0, SEEK_END);
		int size = ftell(fp);
		if (size == 0) {
			continue;
		}
		uint8_t * buffer = (uint8_t*)malloc(size);
		fseek(fp, 0, SEEK_SET);
		size_t red = fread(buffer, 1, size, fp);

		uint8_t * seibuffer = NULL;
		int seicount = 156;
		int ret = get_sei_content(buffer, size, TIME_STAMP_UUID, &seibuffer, &seicount);

		int flags = 0;
		if (seibuffer != NULL)
		{
			printf("%s\n", seibuffer);
			sscanf((char*)seibuffer, "flags:%d", &flags);
			free_sei_content(&seibuffer);
		}

		EvoPacket packet = { 0 };
		packet.data = buffer;
		packet.size = size;
		packet.flags = flags;
		ret = decoder->DecodePacket(&packet, &outFrame);
		if (outFrame != NULL)
		{
#ifdef _WIN32
			SaveAsBMP(outFrame, outFrame->width, outFrame->height, index, 24);
#endif
			FreeAVFrame(&outFrame);
		}
		free(buffer);
		fclose(fp);
	}

	do
	{
		//ret = decoder->DecodePacket(NULL, &outFrame);
		if (outFrame != NULL)
		{
			FreeAVFrame(&outFrame);
		}
		break;
	} while (ret == 1);

	delete decoder;
	decoder = NULL;

	avcodec_close(codecContext);
	avcodec_free_context(&codecContext);

	return 0;
}