#pragma once

#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"

#ifdef USE_NEW_API
static AVCodecContext *CreateCodecContent(AVCodecParameters *codecpar)
{
	AVCodecContext *codecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecContext, codecpar);
	return codecContext;
}
#endif

inline int testEncode2(const char * infile,const char * outfile)
{
	av_register_all();

	AVFormatContext * formatContext = avOpenFile(infile);
	if (formatContext == NULL)
	{
		return -1;
	}
	int videoIndex = getStreamId(formatContext);
	int audioIndex = getStreamId(formatContext, AVMEDIA_TYPE_AUDIO);

	if (videoIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	OriginalEncode encode;
	int ret = encode.Open(outfile);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	if (videoIndex != -1)
	{
		encode.NewVideoStream(formatContext->streams[videoIndex]);
	}
	if (audioIndex != -1)
	{
		//encode.NewAudioStream(formatContext->streams[audioIndex]);
	}

	int frameRate = 30;
	encode.WriteHeader();
	int videoPtsIndex = 0;
	int audioPtsIndex = 0;
	AVPacket * packet = av_packet_alloc();
	while (true)
	{
		int ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				if (packet->pts == AV_NOPTS_VALUE)
				{
					double timestamp = (1000 / frameRate)*videoPtsIndex * 1000;
					videoPtsIndex++;
					AVCodecContext * codecCtx = encode.GetVideoContext();
					int64_t ptsStamp = timestamp/(av_q2d(codecCtx->time_base)*1000);
					packet->pts = ptsStamp;
				}
				encode.WriteVideo(packet);
			}
			else if (packet->stream_index == audioIndex)
			{
				if (packet->pts == AV_NOPTS_VALUE)
				{
					packet->pts = audioPtsIndex++;
				}
				encode.WriteAudio(packet);
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}

	encode.WriteTrailer();
	encode.Close();

	av_packet_free(&packet);

	avformat_close_input(&formatContext);
	return 0;
}