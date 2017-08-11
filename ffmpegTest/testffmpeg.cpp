#include "EvHeade.h"
#include "exterlFunction.h"

//file="../1.mp4"
inline int testFFmpeg(const char * file)
{
	av_register_all();

	AVFormatContext * formatContext = avformat_alloc_context();

	int ret = avformat_open_input(&formatContext, file, NULL, NULL);
	if (ret != 0) {
		avformat_close_input(&formatContext);
		return -1;
	}

	ret = avformat_find_stream_info(formatContext, NULL);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	int videoIndex = getVideoId(formatContext);

	if (videoIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVCodecContext * codecContext = openCodec(formatContext->streams[videoIndex]);
	if (codecContext == NULL)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVPacket * packet = av_packet_alloc();
	AVPacket * pkt = av_packet_alloc();
	AVFrame * frame = av_frame_alloc();

	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				//修改包裹
				//resetPacket(packet, pkt);
				//解码
				ret = decodeVideo(codecContext, packet, frame);
				if (ret == 1)
				{
					av_frame_unref(frame);
				}
				//取出自定义数据
				char selfPacket[255] = { 0 };
				int count = 255;
				ret = get_sei_content(packet->data, packet->size, selfPacket, &count);
				if (ret > 0)
				{
					printf("sei: %s\n", selfPacket);
				}

				av_packet_unref(pkt);
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}


	av_frame_free(&frame);
	av_packet_free(&packet);
	av_packet_free(&pkt);

	avcodec_close(codecContext);
#ifdef USE_NEW_API
	avcodec_free_context(&codecContext);
#endif
	avformat_close_input(&formatContext);
	return 0;
}