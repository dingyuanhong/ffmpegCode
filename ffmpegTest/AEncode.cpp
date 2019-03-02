#include "AEncode.h"

AEncode::AEncode(AVCodecContext *codecContext)
	:codecContext_(codecContext)
{
	encode_count_ = 0;
}

int AEncode::Encode(AVFrame *frame, AVPacket ** pkt)
{
#ifndef USE_NEW_API
	AVPacket * packet = av_packet_alloc();
	av_init_packet(packet);

	int got_packet_ptr = 0;
	int u_size = avcodec_encode_audio2(
		codecContext_,
		packet,
		frame,
		&got_packet_ptr
		);

	if ((0 == u_size) && (1 == got_packet_ptr) && 
		(NULL != packet->data) && (packet->size > 0))
	{
		*pkt = packet;
		return 1;
	}
	else
	{
		encode_count_++;
		av_packet_unref(packet);
		av_packet_free(&packet);
	}
#endif
	return 0;
	
}

int AEncode::Encode(AVPacket ** pkt)
{
#ifndef USE_NEW_API
	if (encode_count_ > 0)
	{
		AVPacket * packet = av_packet_alloc();
		av_init_packet(packet);
		int got_packet_ptr = 0;
		int u_size = avcodec_encode_audio2(
			codecContext_,
			packet,
			NULL,
			&got_packet_ptr
			);

		if ((0 == u_size) && (1 == got_packet_ptr) &&
			(NULL != packet->data) && (packet->size > 0))
		{
			*pkt = packet;
			encode_count_--;
		}
		else
		{
			av_packet_unref(packet);
			av_packet_free(&packet);
		}
	}
	return encode_count_;
#else
	return 0;
#endif
}

void AEncode::Flush() {
	if (this->codecContext_ != NULL) {
		avcodec_flush_buffers(this->codecContext_);
	}
}