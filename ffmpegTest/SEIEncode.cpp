
#include "SEIEncode.h"
#include "sei_packet.h"

static int resetPacket(AVPacket * packet, AVPacket * pkt)
{
	//生成自定义数据
	char buffer[256];
	sprintf(buffer, "pts:%lld dts:%lld", packet->pts, packet->dts);

	size_t len = strlen(buffer);
	//获取自定义数据长度
	size_t sei_packet_size = get_sei_packet_size(len);

	av_new_packet(pkt, packet->size + (int)sei_packet_size);
	memset(pkt->data, 0, packet->size + sei_packet_size);
	pkt->pts = packet->pts;
	pkt->dts = packet->dts;

	bool addInHead = true;
	if (addInHead)
	{
		//填充原始数据
		memcpy(pkt->data + sei_packet_size, packet->data, packet->size);
		//填充自定义数据
		unsigned char * sei = (unsigned char*)pkt->data;
		fill_sei_packet(sei, false, buffer, len);
	}
	else
	{
		//填充原始数据
		memcpy(pkt->data, packet->data, packet->size);
		//填充自定义数据
		unsigned char * sei = (unsigned char*)pkt->data + packet->size;
		fill_sei_packet(sei, false, buffer, len);
	}
	

	return 0;
}

int SEIEncode::EncodeVideo(AVFrame* frame)
{
	int got_picture = 0;
	av_init_packet(&videoPacket_);
	videoPacket_.data = NULL;
	videoPacket_.size = 0;
	int ret = avcodec_encode_video2(videoStream_->codec, &videoPacket_,
		frame, &got_picture);

	if (ret < 0) {
		printf("Failed to encode! \n");
		return -1;
	}
	if (got_picture == 1) {
		printf("Succeed to encode frame size:%5d %lld\n", videoPacket_.size, videoPacket_.pts);
		AVPacket * pkt = (AVPacket*)av_malloc(sizeof(AVPacket));//av_packet_alloc();
		av_init_packet(pkt);

		resetPacket(&videoPacket_, pkt);
		ret = WriteVideo(pkt);
		av_free_packet(pkt);
		av_free(pkt);

		av_free_packet(&videoPacket_);
		return 1;
	}
	else
	{
		return 0;
	}

	return -1;
}

int SEIEncode::FlushVideo()
{
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(videoStream_->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2(videoStream_->codec, &enc_pkt,
			NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame) {
			ret = 0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d %lld\n", enc_pkt.size, enc_pkt.pts);
		/* mux encoded frame */
		AVPacket * pkt = (AVPacket*)av_malloc(sizeof(AVPacket));//av_packet_alloc();
		av_init_packet(pkt);

		resetPacket(&enc_pkt, pkt);

		ret = WriteVideo(pkt);

		av_free_packet(pkt);
		av_free(pkt);

		if (ret < 0)
			break;
	}
	return ret;
}