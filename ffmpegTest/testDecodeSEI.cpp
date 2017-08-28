#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"

//"../Vid0616000023.mp4"
inline int testDecodeSEI(const char * file)
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

	AVStream * stream = source.GetVideoStream();
	AVCodecContext * sourceContext = stream->codec;

	if (sourceContext != NULL && sourceContext->codec != NULL)
	{
		printf("Stream DECODE:%s\n", sourceContext->codec->name);
	}

	VideoDecoder *decoder = NULL;
	AVCodecContext	*codecContext = NULL;
	bool newContext = false;
	if (newContext) {
		AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
		if (!codec) return -1;
		codecContext = avcodec_alloc_context3(codec);

		uint8_t extData[64];
		int size = source.GetExtData(extData, 64);
		codecContext->extradata = extData;
		codecContext->extradata_size = size;

		int rate = source.GetFrameRate();
		int duration = source.GetDuration();

		if (avcodec_open2(codecContext, codec, NULL) < 0)
		{
			return -1;
		}
		decoder = new VideoDecoder(codecContext);
	}
	else
	{
		//使用自身解码器解码
		AVCodec *codec = (AVCodec*)sourceContext->codec;
		if (codec == NULL) codec = avcodec_find_decoder(sourceContext->codec_id);

		if (avcodec_open2(sourceContext, codec, NULL) < 0)
		{
			return -1;
		}

		decoder = new VideoDecoder(sourceContext);
	}

	while (true)
	{
		EvoFrame *out = NULL;
		ret = source.ReadFrame(&out);
		if (out != NULL)
		{
			uint8_t * buffer = NULL;
			int count = 0;
			int ret = get_sei_content(out->data, out->size, TIME_STAMP_UUID, &buffer, &count);
			if (buffer != NULL)
			{
				printf("%s\n", buffer);
				free_sei_content(&buffer);
			}

			AVFrame *outFrame = NULL;

			printf("Decode begin:%lld\n", av_gettime() / 1000);
			decoder->DecodeFrame(out, &outFrame);
			printf("Decode end:%lld Success:%d\n", av_gettime() / 1000, (outFrame != NULL));

			if (outFrame != NULL)
			{
				FreeAVFrame(&outFrame);
			}

			EvoFreeFrame(&out);
		}
		if (ret == AVERROR_EOF)
		{
			break;
		}
	}

	if (decoder != NULL)
	{
		delete decoder;
		decoder = NULL;
	}

	if (codecContext != NULL)
	{
		codecContext->extradata = NULL;
		codecContext->extradata_size = 0;
		avcodec_close(codecContext);
		avcodec_free_context(&codecContext);
	}

	return 0;
}