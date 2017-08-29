
#include "SEIEncode.h"
#include "EvoInterface/sei_packet.h"

static int resetPacket(AVPacket * packet, AVPacket * pkt)
{
	//生成自定义数据
	char buffer[256];
	sprintf(buffer, "pts:%lld dts:%lld", packet->pts, packet->dts);

	size_t len = strlen(buffer);
	//获取自定义数据长度
	size_t sei_packet_size = get_sei_packet_size((const uint8_t*)buffer,len);

	av_new_packet(pkt, packet->size + (int)sei_packet_size);
	memset(pkt->data, 0, packet->size + sei_packet_size);
	pkt->pts = packet->pts;
	pkt->dts = packet->dts;

	unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
	unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

	unsigned char *data = packet->data;
	int size = packet->size;
	bool isAnnexb = false;
	if ((size > 3 && memcmp(data, ANNEXB_CODE_LOW, 3) == 0) ||
		(size > 4 && memcmp(data, ANNEXB_CODE, 4) == 0)
		)
	{
		isAnnexb = true;
	}

	bool addInHead = false;
	if (addInHead)
	{
		//填充原始数据
		memcpy(pkt->data + sei_packet_size, packet->data, packet->size);
		//填充自定义数据
		unsigned char * sei = (unsigned char*)pkt->data;
		fill_sei_packet(sei, isAnnexb, TIME_STAMP_UUID, (const uint8_t*)buffer, len);
	}
	else
	{
		//填充原始数据
		memcpy(pkt->data, packet->data, packet->size);
		//填充自定义数据
		unsigned char * sei = (unsigned char*)pkt->data + packet->size;
		fill_sei_packet(sei, isAnnexb, TIME_STAMP_UUID,(const uint8_t*)buffer, len);
	}
	

	return 0;
}

int SEIEncode::EncodeVideo(AVFrame* frame)
{
	int got_picture = 0;
	av_init_packet(&videoPacket_);
	videoPacket_.data = NULL;
	videoPacket_.size = 0;
#ifdef USE_NEW_API
	int ret = avcodec_send_frame(videoCodecCtx_, frame);
	if(ret == 0) ret = avcodec_receive_packet(videoCodecCtx_,&videoPacket_);
	if (ret == 0)
	{
		got_picture = 1;
	}
#else
	int ret = avcodec_encode_video2(videoCodecCtx_, &videoPacket_,
		frame, &got_picture);
#endif
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
		//ret = WriteVideo(&videoPacket_);
#ifdef USE_NEW_API
		av_packet_unref(pkt);
#else
		av_free_packet(pkt);
#endif
		av_free(pkt);
#ifdef USE_NEW_API
		av_packet_unref(&videoPacket_);
#else
		av_free_packet(&videoPacket_);
#endif
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
	if (!(videoCodecCtx_->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
#ifdef USE_NEW_API
		ret = avcodec_receive_packet(videoCodecCtx_, &enc_pkt);
		if (ret == 0)
		{
			got_frame = 1;
		}
#else
		ret = avcodec_encode_video2(videoCodecCtx_, &enc_pkt,
			NULL, &got_frame);
#endif
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
#ifdef USE_NEW_API
		av_packet_unref(pkt);
#else
		av_free_packet(pkt);
#endif
		av_free(pkt);

		if (ret < 0)
			break;
	}
	return ret;
}