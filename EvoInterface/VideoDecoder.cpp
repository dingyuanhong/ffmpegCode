#include "VideoDecoder.h"

#define LOGA printf

void FreeAVFrame(AVFrame **out)
{
	if (out == NULL) return;
	AVFrame *frame = *out;
	if (frame != NULL)
	{
		if (frame->data[0] != NULL)
		{
			av_freep(&frame->data[0]);
		}
	}
	av_frame_free(out);
	*out = NULL;
}

VideoDecoder::VideoDecoder(AVCodecContext	*codec)
	:VideoCodecCtx(codec)
	, Convert(NULL)
	, KeepIFrame(false)
{
	this->VideoFrame = av_frame_alloc();
	this->Packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	memset(this->Packet,0,sizeof(AVPacket));
	av_init_packet(this->Packet);
}

VideoDecoder::~VideoDecoder()
{
	if (this->VideoFrame != NULL) {
		av_frame_free(&this->VideoFrame);
		this->VideoFrame = NULL;
	}
	if (this->Packet != NULL)
	{
		av_free_packet(this->Packet);
		av_free(this->Packet);
		this->Packet = NULL;
	}
}

void VideoDecoder::Attach(EvoVideoConvert *convert)
{
	Convert = convert;
}

EvoVideoConvert *VideoDecoder::Detach()
{
	EvoVideoConvert *old = Convert;
	Convert = NULL;
	return old;
}

int VideoDecoder::DecodeFrame(EvoFrame *packet, AVFrame **evoResult)
{
	if (packet == NULL || packet->size == 0)
	{
		return Decode(NULL,evoResult);
	}
	else 
	{
		int ret = av_new_packet(this->Packet, packet->size);

		if (ret == 0) {
			memcpy(this->Packet->data, packet->data, packet->size);
			Packet->pts = packet->pts;
			Packet->dts = packet->dts;
			Packet->pos = packet->timestamp;
			Packet->flags = packet->flags;
			ret = Decode(Packet, evoResult);
		}

		av_packet_unref(this->Packet);
		return ret;
	}
}

int VideoDecoder::DecodePacket(EvoPacket *packet, AVFrame **evoResult)
{
	if (packet == NULL || packet->size == 0)
	{
		return Decode(NULL, evoResult);
	}
	else
	{
		int ret = av_new_packet(this->Packet, packet->size);

		if (ret == 0) {
			memcpy(this->Packet->data, packet->data, packet->size);
			Packet->pts = packet->pts;
			Packet->dts = packet->dts;
			Packet->pos = packet->timestamp;
			Packet->flags = packet->flags;
			ret = Decode(Packet, evoResult);
		}
		av_packet_unref(this->Packet);
		return ret;
	}
}

int  VideoDecoder::Decode(AVPacket *packet, AVFrame **evoResult)
{
	if (evoResult != NULL)
	{
		*evoResult = NULL;
	}

	if (packet == NULL) {
		return 0;
	}

	int gotFrame = 0;
	int decoded = avcodec_decode_video2(this->VideoCodecCtx, VideoFrame, &gotFrame, packet);
	if (decoded < 0) {
		if (decoded == AVERROR(EAGAIN)) return 0;
		char errbuf[1024] = { 0 };
		av_strerror(decoded, errbuf, 1024);
		LOGA("VideoDecoder::DecodePacket:avcodec_decode_video2:%d(%s).\n", decoded, errbuf);
		if (decoded == AVERROR_INVALIDDATA) return 0;
		if (decoded == AVERROR_EOF) return -1;
		if (decoded == AVERROR(EINVAL)) return -1;
		if (AVERROR(ENOMEM)) return -1;
		return -1;
	}

	if (gotFrame) {

		if (KeepIFrame)
		{
			if (VideoFrame->pict_type == AV_PICTURE_TYPE_P)
			{
				av_frame_unref(this->VideoFrame);
				return 0;
			}
			else if (VideoFrame->pict_type == AV_PICTURE_TYPE_B)
			{
				av_frame_unref(this->VideoFrame);
				return 0;
			}else if (VideoFrame->pict_type == AV_PICTURE_TYPE_I)
			{
				//保留
				KeepIFrame = false;
			}
			else 
			{
				//直接处理掉
				KeepIFrame = false;
			}
		}

		struct EvoVideoInfo info = { 0,0,AV_PIX_FMT_NONE };
		EvoVideoConvert *tmpConvert = Convert;
		if (tmpConvert != NULL)
		{
			info = tmpConvert->GetCorrectTargetInfo(this->VideoCodecCtx->width, this->VideoCodecCtx->height);
		}
		else
		{
			if (this->VideoCodecCtx != NULL)
			{
				info.Width = this->VideoCodecCtx->width;
				info.Height = this->VideoCodecCtx->height;
				info.Format = this->VideoCodecCtx->pix_fmt;
			}
		}

		AVFrame * retData = av_frame_alloc();
		int retSize = av_image_alloc(retData->data, retData->linesize, info.Width, info.Height, info.Format, 1);

		if (retSize <= 0)
		{
			LOGA("VideoDecoder::DecodePacket:EvoPacketAllocator::CreateAVFrame(%d,%d,%d)==NULL.\n"
				, info.Width, info.Height, info.Format);

			FreeAVFrame(&retData);
			av_frame_unref(this->VideoFrame);
			return -1;
		}
		else 
		{
			AVFrame* desFrame = retData;
			if (tmpConvert != NULL)
			{
				tmpConvert->Convert(VideoFrame, desFrame);
			}
			else
			{
				//存放视频信息
				desFrame->width = info.Width;
				desFrame->height = info.Height;
				desFrame->format = info.Format;
				//拷贝数据
				int ret = av_frame_copy(desFrame, VideoFrame);
				
				if (ret < 0)
				{

					LOGA("VideoDecoder::DecodePacket:EvoPacketAllocator::av_frame_copy==(%d).\n"
						, ret);
					FreeAVFrame(&retData);
					av_frame_unref(this->VideoFrame);
					return -1;
				}
			}
			
			desFrame->width = info.Width;
			desFrame->height = info.Height;
			desFrame->format = info.Format;

			desFrame->pkt_pts = VideoFrame->pkt_pts;
			desFrame->pkt_dts = VideoFrame->pkt_dts;
			desFrame->pts = VideoFrame->pts;

			if (evoResult != NULL)
			{
				*evoResult = retData;
			}
			else
			{
				FreeAVFrame(&retData);
			}

			av_frame_unref(this->VideoFrame);

			return 1;
		}
	}

	return 0;
}