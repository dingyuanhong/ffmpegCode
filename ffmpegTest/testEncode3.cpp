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

int testEncode3(const char * infile, const char * outfile)
{
	av_register_all();

	AVFormatContext * formatContext = avOpenFile(infile);
	if (formatContext == NULL)
	{
		return -1;
	}
	int videoIndex = getStreamId(formatContext, AVMEDIA_TYPE_VIDEO);
	int audioIndex = getStreamId(formatContext, AVMEDIA_TYPE_AUDIO);

	if (videoIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}
	if (audioIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	Encode encode;
	int ret = encode.Open(outfile);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVStream * stream = formatContext->streams[videoIndex];

	if (videoIndex != -1)
	{
		encode.NewVideoStream(formatContext->streams[videoIndex]);
		//encode.NewVideoStream(stream->codec->width, stream->codec->height, stream->codec->pix_fmt,30);
	}
	if (audioIndex != -1)
	{
		encode.NewAudioStream(formatContext->streams[audioIndex]);
	}

	ret = encode.WriteHeader();
	if (ret != 0)
	{
		//Ð´Í·Ê§°Ü
		return -1;
	}

	AVPacket * packet = av_packet_alloc();
	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				packet->pts = packet->dts;
				encode.WriteVideo(packet);
			}
			else if (packet->stream_index == audioIndex)
			{
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