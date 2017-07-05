#include "Encode.h"



/**
* 最简单的基于FFmpeg的视频编码器
* Simplest FFmpeg Video Encoder
*
* 雷霄骅 Lei Xiaohua
* leixiaohua1020@126.com
* 中国传媒大学/数字电视技术
* Communication University of China / Digital TV Technology
* http://blog.csdn.net/leixiaohua1020
*
* 本程序实现了YUV像素数据编码为视频码流（H264，MPEG2，VP8等等）。
* 是最简单的FFmpeg视频编码方面的教程。
* 通过学习本例子可以了解FFmpeg的编码流程。
* This software encode YUV420P data to H.264 bitstream.
* It's the simplest video encoding software based on FFmpeg.
* Suitable for beginner of FFmpeg
*/

#include <stdio.h>  

#define __STDC_CONSTANT_MACROS  

#include "Encode.h"

Encode::Encode()
{
	formatCtx_ = NULL;
	output_ = NULL;

	videoStream_ = NULL;
	videoCodecCtx_ = NULL;
	videoCodec_ = NULL;

	audioStream_ = NULL;
	audioCodecCtx_ = NULL;
	audioCodec_ = NULL;
}

int Encode::Open(const char * file)
{
	av_register_all();
	//Method1.  
	formatCtx_ = avformat_alloc_context();
	//Guess Format  
	output_ = av_guess_format(NULL, file, NULL);
	formatCtx_->oformat = output_;

	//Method 2.
	//avformat_alloc_output_context2(&formatCtx_, NULL, NULL, file);
	//fmt = formatCtx_->oformat;

	//Open output URL  
	if (avio_open(&formatCtx_->pb, file, AVIO_FLAG_READ_WRITE) < 0) {
		printf("Failed to open output file! \n");
		return -1;
	}
	return 0;
}

int Encode::NewVideoStream(int width,int height, AVPixelFormat format)
{
	videoStream_ = avformat_new_stream(formatCtx_, 0);
	//video_st->time_base.num = 1;   
	//video_st->time_base.den = 25;    

	if (videoStream_ == NULL) {
		return -1;
	}
	//Param that must set  
	videoCodecCtx_ = videoStream_->codec;

	//videoCodecCtx_->codec_id =AV_CODEC_ID_HEVC;  
	videoCodecCtx_->codec_id = output_->video_codec;
	videoCodecCtx_->codec_type = AVMEDIA_TYPE_VIDEO;
	videoCodecCtx_->pix_fmt = format;
	videoCodecCtx_->width = width;
	videoCodecCtx_->height = height;
	videoCodecCtx_->bit_rate = 400000;
	videoCodecCtx_->gop_size = 250;

	videoCodecCtx_->time_base.num = 1;
	videoCodecCtx_->time_base.den = 25;

	//H264  
	//pCodecCtx->me_range = 16;  
	//pCodecCtx->max_qdiff = 4;  
	//pCodecCtx->qcompress = 0.6;  
	videoCodecCtx_->qmin = 10;
	videoCodecCtx_->qmax = 51;

	//Optional Param  
	videoCodecCtx_->max_b_frames = 3;
	//videoCodecCtx_->thread_count = 1;

	// Set Option  
	AVDictionary *param = 0;
	//H.264  
	if (videoCodecCtx_->codec_id == AV_CODEC_ID_H264) {
		av_dict_set(&param, "preset", "slow", 0);
		av_dict_set(&param, "tune", "zerolatency", 0);
		//av_dict_set(¶m, "profile", "main", 0);  
	}
	//H.265  
	if (videoCodecCtx_->codec_id == AV_CODEC_ID_H265) {
		av_dict_set(&param, "preset", "ultrafast", 0);
		av_dict_set(&param, "tune", "zero-latency", 0);
	}

	videoCodec_ = avcodec_find_encoder(videoCodecCtx_->codec_id);
	if (!videoCodec_) {
		printf("Can not find encoder! \n");
		return -1;
	}
	if (avcodec_open2(videoCodecCtx_, videoCodec_, &param) < 0) {
		printf("Failed to open encoder! \n");
		return -1;
	}

	return 0;
}

int Encode::NewAudioStream()
{
	return 0;
}

int Encode::WriteHeader()
{
	//Write File Header  
	return avformat_write_header(formatCtx_, NULL);
}

int Encode::WriteVideo(AVPacket *packet)
{
	packet->stream_index = videoStream_->index;
	int ret = av_write_frame(formatCtx_, packet);
	return ret;
}

int Encode::WriteAudio(AVPacket *packet)
{
	packet->stream_index = audioStream_->index;
	int ret = av_write_frame(formatCtx_, packet);
	return ret;
}

int Encode::WriteTrailer()
{
	av_write_trailer(formatCtx_);
	return 0;
}

void Encode::Close()
{
	if (videoStream_) {
		avcodec_close(videoStream_->codec);
		//avcodec_free_context(&videoStream_->codec);
	}
	if (audioStream_) {
		avcodec_close(audioStream_->codec);
		//avcodec_free_context(&audioStream_->codec);
	}
	if (formatCtx_) {
		avio_closep(&formatCtx_->pb);
		avformat_free_context(formatCtx_);
	}

	videoStream_ = NULL;
	videoCodecCtx_ = NULL;
	videoCodec_ = NULL;

	audioStream_ = NULL;
	audioCodecCtx_ = NULL;
	audioCodec_ = NULL;

	output_ = NULL;
	formatCtx_ = NULL;
}



int OriginalEncode::EncodeAudio(AVFrame* frame)
{
	int got_picture = 0;
	av_init_packet(&audioPacket_);
	audioPacket_.data = NULL;
	audioPacket_.size = 0;
	int ret = avcodec_encode_audio2(audioStream_->codec, &audioPacket_,
		frame, &got_picture);

	if (ret < 0) {
		printf("Failed to encode! \n");
		return -1;
	}
	if (got_picture == 1) {
		printf("Succeed to encode frame size:%5d\n", audioPacket_.size);
		ret = WriteAudio(&audioPacket_);
		av_free_packet(&audioPacket_);
		return 0;
	}
	return -1;
}

int OriginalEncode::EncodeVideo(AVFrame* frame)
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
		printf("Succeed to encode frame size:%5d\n", videoPacket_.size);
		ret = WriteVideo(&videoPacket_);
		av_free_packet(&videoPacket_);
		return 1;
	}
	else
	{
		return 0;
	}

	return -1;
}

int OriginalEncode::FlushAudio()
{
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(audioStream_->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_audio2(audioStream_->codec, &enc_pkt,
			NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame) {
			ret = 0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		/* mux encoded frame */
		ret = WriteAudio(&enc_pkt);
		if (ret < 0)
			break;
	}
	return ret;
}

int OriginalEncode::FlushVideo()
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
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		/* mux encoded frame */
		ret = WriteVideo(&enc_pkt);
		if (ret < 0)
			break;
	}
	return ret;
}

AVStream* OriginalEncode::GetVideoStream()
{
	return videoStream_;
}

AVStream* OriginalEncode::GetAudioStream()
{
	return audioStream_;
}