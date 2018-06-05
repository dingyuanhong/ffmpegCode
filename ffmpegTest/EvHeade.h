#pragma once
#ifdef __cplusplus

#include "../EvoInterface/EvoHeader.h"
#include "../EvoInterface/EvoMediaSource.h"
#include "../EvoInterface/VideoDecoder.h"
#include "../EvoInterface/sei_packet.h"

#ifdef _WIN32

#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#endif

#endif

#ifndef USE_NEW_API
static inline AVPacket * av_packet_alloc()
{
	AVPacket * packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);
	return packet;
}

static inline void av_packet_free(AVPacket **ppacket)
{
	if (ppacket == NULL) return;
	AVPacket *packet = *ppacket;
	if (packet == NULL) return;
	av_free_packet(packet);
	av_free(packet);
	*ppacket = NULL;
}
#endif