#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"

//"../Vid0616000023.mp4"
//"../1.mp4"
int testMP4Encode(const char * file,const char * outFile)
{
	/****************************************************************/
	EvoMediaSource source;
	//file += "gopro.mp4";
	int ret = source.Open(file);
	if (ret != 0)
	{
		return -1;
	}

	AVStream * stream = source.GetVideoStream();
	
#ifdef USE_NEW_API
	int width = stream->codecpar->width;
	int height = stream->codecpar->height;
	AVPixelFormat pixelFormat = (AVPixelFormat)stream->codecpar->format;
#else
	int width = stream->codec->width;
	int height = stream->codec->height;
	AVPixelFormat pixelFormat = stream->codec->pix_fmt;
#endif

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
		//ʹ���������������
		AVCodec *codec = (AVCodec*)sourceContext->codec;
		if (codec == NULL) codec = avcodec_find_decoder(sourceContext->codec_id);

		if (avcodec_open2(sourceContext, codec, NULL) < 0)
		{
			return -1;
		}

		decoder = new VideoDecoder(sourceContext);
#endif
	}
	/****************************************************************/

	/****************************************************************/
	OriginalEncode encode;
	ret = encode.Open(outFile);
	if (ret != 0)
	{
		return -1;
	}
	ret = encode.NewVideoStream(width, height, pixelFormat);
	if (ret != 0)
	{
		return -1;
	}
	encode.WriteHeader();

	AVStream * encodeStream = encode.GetVideoStream();
	AVCodecContext * encodeCodecContext = encode.GetVideoContext();

	AVFrame * frame = av_frame_alloc();
#ifdef USE_NEW_API
	int picture_size = av_image_get_buffer_size(encodeCodecContext->pix_fmt, encodeCodecContext->width, encodeCodecContext->height,1);
	uint8_t * picture_buf = (uint8_t *)av_malloc(picture_size);
	av_image_fill_arrays(frame->data, frame->linesize, picture_buf, encodeCodecContext->pix_fmt, encodeCodecContext->width, encodeCodecContext->height,1);
#else
	int picture_size = avpicture_get_size(encodeCodecContext->pix_fmt, encodeCodecContext->width, encodeCodecContext->height);
	uint8_t * picture_buf = (uint8_t *)av_malloc(picture_size);
	avpicture_fill((AVPicture *)frame, picture_buf, encodeCodecContext->pix_fmt, encodeCodecContext->width, encodeCodecContext->height);
#endif
	/****************************************************************/
	int i = 0;
	while (true)
	{
		EvoFrame *out = NULL;
		ret = source.ReadFrame(&out);
		if (out != NULL)
		{
			AVFrame *outFrame = NULL;
			decoder->DecodeFrame(out, &outFrame);

			if (outFrame != NULL)
			{
				frame->width = encodeCodecContext->width;
				frame->height = encodeCodecContext->height;
				frame->format = encodeCodecContext->pix_fmt;
				frame->pts = (i++)*(encodeStream->time_base.den) / ((encodeStream->time_base.num) * 25);

				ret = encode.EncodeVideo(frame);

				FreeAVFrame(&outFrame);
				if (ret < 0)
				{
					printf("д�ļ�ʧ��!");
					break;
				}
			}

			EvoFreeFrame(&out);
		}

		if (ret == AVERROR_EOF)
		{
			break;
		}
	}

	/****************************************************************/
	encode.FlushVideo();

	encode.WriteTrailer();
	encode.Close();
	/****************************************************************/

	/****************************************************************/
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
	/****************************************************************/

	av_free(picture_buf);
	picture_buf = NULL;
	av_frame_free(&frame);
	return  0;
}