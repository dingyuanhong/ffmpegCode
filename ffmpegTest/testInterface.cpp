#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"
#ifdef _WIN32
#include<direct.h>
#define mkdir(A,B) _mkdir(A)
#else
#include <sys/stat.h>
#endif

//"../Vid0616000023.mp4"
int TestInterface(const char * file)
{
	avcodec_register_all();

	EvoMediaSource source;
	int ret = source.Open(file);
	if (ret != 0)
	{
		return -1;
	}

	AVStream * stream = source.GetVideoStream();

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
#ifndef USE_NEW_API
		AVCodecContext * sourceContext = stream->codec;

		if (sourceContext != NULL && sourceContext->codec != NULL)
		{
			printf("Stream DECODE:%s\n", sourceContext->codec->name);
		}

		//使用自身解码器解码
		AVCodec *codec = (AVCodec*)sourceContext->codec;
		if (codec == NULL) codec = avcodec_find_decoder(sourceContext->codec_id);

		if (avcodec_open2(sourceContext, codec, NULL) < 0)
		{
			return -1;
		}

		decoder = new VideoDecoder(sourceContext);
#endif
	}

	EvoVideoConvert convert;
	struct EvoVideoInfo info;
#ifdef USE_NEW_API
	info.Width = stream->codecpar->width;
	info.Height = stream->codecpar->height;
	info.Format = (AVPixelFormat)stream->codecpar->format;
#else
	info.Width = stream->codec->width;
	info.Height = stream->codec->height;
	info.Format = stream->codec->pix_fmt;
#endif

	struct EvoVideoInfo des = info;
	des.Format = AV_PIX_FMT_NV12;
	des.Width = 1920;
	des.Height = 960;
	convert.Initialize(info, des);

	if (decoder != NULL)
	{
		decoder->Attach(&convert);
	}

	int index = 0;
	//mkdir("./tmp", 0x777);
	while (true)
	{
		EvoFrame *out = NULL;
		ret = source.ReadFrame(&out);
		if (out != NULL)
		{
			printf("pts:%lld dts:%lld timestamp:%lld size:%d\n", out->pts, out->dts, out->timestamp, out->size);

			/*char file[64];
			sprintf(file,"./tmp/%05d.h264",index++);
			FILE * fp = fopen(file,"wb+");
			fwrite(out->data,out->size,1,fp);
			fclose(fp);*/

			AVFrame *outFrame = NULL;

			printf("Decode begin:%lld\n", av_gettime() / 1000);
			decoder->DecodeFrame(out, &outFrame);
			printf("Decode end:%lld Success:%d\n", av_gettime() / 1000, (outFrame != NULL));

			if (outFrame != NULL)
			{
				//printf("pts:%lld dts:%lld dts:%lld\n", outFrame->pts, outFrame->pkt_pts, outFrame->pkt_dts);
#ifdef _WIN32
				char file[64];
				sprintf(file,"./tmp/%05d.yuv",index++);
				FILE * fp = fopen(file,"wb+");
				fwrite(outFrame->data[0], outFrame->width*outFrame->height*3/2,1,fp);
				fclose(fp);
				//SaveAsBMP(outFrame, des.Width, des.Height, index++, 24);
#endif
				FreeAVFrame(&outFrame);
			}

			EvoFreeFrame(&out);
		}
		//source.Seek(10*1000);
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