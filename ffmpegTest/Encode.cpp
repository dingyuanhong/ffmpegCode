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

#ifdef USE_NEW_API
static AVCodecContext *CreateCodecContent(AVCodecParameters *codecpar)
{
	AVCodecContext *codecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecContext, codecpar);
	return codecContext;
}
#endif

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

int Encode::NewVideoStream(int width,int height, AVPixelFormat format,int frameRate)
{
	videoStream_ = avformat_new_stream(formatCtx_, NULL);

	if (videoStream_ == NULL) {
		return -1;
	}
	videoStream_->time_base.num = 1;
	videoStream_->time_base.den = frameRate;

	if (videoCodecCtx_) {
		avcodec_close(videoCodecCtx_);
#ifdef USE_NEW_API
		avcodec_free_context(&videoCodecCtx_);
#endif
		videoCodecCtx_ = NULL;
	}
#ifdef USE_NEW_API
	videoCodecCtx_ = CreateCodecContent(videoStream_->codecpar);
#else
	//Param that must set  
	videoCodecCtx_ = videoStream_->codec;
#endif
	AVCodecContext* codecCtx = videoCodecCtx_;

	//codecCtx->codec_id =AV_CODEC_ID_HEVC;  
	codecCtx->codec_id = output_->video_codec;
	codecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
	codecCtx->pix_fmt = format;
	codecCtx->width = width;
	codecCtx->height = height;

	//设置码率
	codecCtx->bit_rate = 40000000;
	codecCtx->rc_max_rate = 40000000;
	codecCtx->rc_min_rate = 400000;
	//codecCtx->gop_size = 250;

	//设置时间戳时间基准
	codecCtx->time_base.num = 1;
	codecCtx->time_base.den = frameRate;

	//H264  
	//codecCtx->me_range = 16;  
	//codecCtx->max_qdiff = 4;  
	//codecCtx->qcompress = 0.6;  
	codecCtx->qmin = 10;
	codecCtx->qmax = 51;

	//Optional Param  
	codecCtx->max_b_frames = 3;
	//codecCtx->thread_count = 1;

	// Set Option  
	AVDictionary *param = NULL;

	if(codecCtx->codec_id == AV_CODEC_ID_H264) {
		av_dict_set(&param, "preset", "slow", 0);
		av_dict_set(&param, "tune", "zerolatency", 0);
		av_dict_set(&param, "profile", "main", 0);
	}
	//H.265  
	if (codecCtx->codec_id == AV_CODEC_ID_H265) {
		av_dict_set(&param, "preset", "ultrafast", 0);
		av_dict_set(&param, "tune", "zero-latency", 0);
	}

	videoCodec_ = avcodec_find_encoder(codecCtx->codec_id);
	if (!videoCodec_) {
		printf("Can not find encoder! \n");
		return -1;
	}
	if (avcodec_open2(codecCtx, videoCodec_, param == NULL ? NULL : &param) != 0) {
		printf("Failed to open encoder! \n");
		return -1;
	}

	return 0;
}

int Encode::NewVideoStream(AVStream * stream)
{
	if (stream == NULL)
	{
		return -1;
	}

	videoStream_ = avformat_new_stream(formatCtx_, NULL);

	if (videoStream_ == NULL) {
		return -1;
	}
	videoStream_->time_base = stream->time_base;

	if (videoCodecCtx_) {
		avcodec_close(videoCodecCtx_);
#ifdef USE_NEW_API
		avcodec_free_context(&videoCodecCtx_);
#endif
		videoCodecCtx_ = NULL;
	}
#ifdef USE_NEW_API
	videoCodecCtx_ = CreateCodecContent(videoStream_->codecpar);
#else
	//Param that must set  
	videoCodecCtx_ = videoStream_->codec;
#endif
	AVCodecContext* codecCtx = videoCodecCtx_;

#ifdef USE_NEW_API
	avcodec_parameters_to_context(codecCtx, stream->codecpar);
#else
	avcodec_copy_context(codecCtx, stream->codec);
#endif
	codecCtx->time_base = stream->r_frame_rate;
	codecCtx->codec_id = output_->video_codec;
	
	// Set Option  
	AVDictionary *param = NULL;

	if (codecCtx->codec_id == AV_CODEC_ID_H264) {
		av_dict_set(&param, "preset", "slow", 0);
		av_dict_set(&param, "tune", "zerolatency", 0);
		av_dict_set(&param, "profile", "baseline", 0);
		av_dict_set(&param, "qp", "0", 0);
	}
	//H.265  
	if (codecCtx->codec_id == AV_CODEC_ID_H265) {
		av_dict_set(&param, "preset", "ultrafast", 0);
		av_dict_set(&param, "tune", "zero-latency", 0);
	}

	videoCodec_ = avcodec_find_encoder(codecCtx->codec_id);
	if (!videoCodec_) {
		printf("Can not find encoder! \n");
		return -1;
	}
	if (avcodec_open2(codecCtx, videoCodec_, param == NULL ? NULL : &param) != 0) {
		printf("Failed to open encoder! \n");
		return -1;
	}
	return 0;
}

int Encode::NewAudioStream(enum AVSampleFormat format,int smapleRate, uint64_t channel_layout)
{
	audioStream_ = avformat_new_stream(formatCtx_, NULL);

	if (audioStream_ == NULL) {
		return -1;
	}
	audioStream_->time_base.num = 1;
	audioStream_->time_base.den = 0;

	if (audioCodecCtx_) {
		avcodec_close(audioCodecCtx_);
#ifdef USE_NEW_API
		avcodec_free_context(&audioCodecCtx_);
#endif
		audioCodecCtx_ = NULL;
	}
#ifdef USE_NEW_API
	audioCodecCtx_ = CreateCodecContent(audioStream_->codecpar);
#else
	//Param that must set  
	audioCodecCtx_ = audioStream_->codec;
#endif
	AVCodecContext* codecCtx = audioCodecCtx_;

	//codecCtx->codec_id =AV_CODEC_ID_HEVC;  
	codecCtx->codec_id = output_->audio_codec;
	codecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
	
	codecCtx->sample_fmt = format ;
	codecCtx->sample_rate = 44100;
	codecCtx->channel_layout = channel_layout;
	codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);

	codecCtx->bit_rate = 64000;
	codecCtx->gop_size = 250;

	codecCtx->time_base.num = 1;
	codecCtx->time_base.den = 1000;

	codecCtx->qmin = 10;
	codecCtx->qmax = 51;

	//Optional Param  
	codecCtx->max_b_frames = 3;
	//videoCodecCtx_->thread_count = 1;

	// Set Option  
	AVDictionary *param = NULL;
	audioCodec_ = avcodec_find_encoder(codecCtx->codec_id);
	if (!audioCodec_) {
		printf("Can not find encoder! \n");
		return -1;
	}
	if (avcodec_open2(codecCtx, audioCodec_, param ==NULL? NULL : &param) != 0) {
		printf("Failed to open encoder! \n");
		return -1;
	}
	return 0;
}

int Encode::NewAudioStream(AVStream * stream)
{
	if (stream == NULL) return -1;

	audioStream_ = avformat_new_stream(formatCtx_, NULL);

	if (audioStream_ == NULL) {
		return -1;
	}
	audioStream_->time_base = stream->time_base;

	if (audioCodecCtx_) {
		avcodec_close(audioCodecCtx_);
#ifdef USE_NEW_API
		avcodec_free_context(&audioCodecCtx_);
#endif
		audioCodecCtx_ = NULL;
	}
#ifdef USE_NEW_API
	audioCodecCtx_ = CreateCodecContent(audioStream_->codecpar);
#else
	//Param that must set  
	audioCodecCtx_ = audioStream_->codec;
#endif
	AVCodecContext* codecCtx = audioCodecCtx_;

#ifdef USE_NEW_API
	avcodec_parameters_to_context(codecCtx, stream->codecpar);
#else
	avcodec_copy_context(codecCtx, stream->codec);
#endif
	codecCtx->time_base = stream->r_frame_rate;
	codecCtx->codec_id = output_->audio_codec;
	
	// Set Option  
	AVDictionary *param = NULL;
	audioCodec_ = avcodec_find_encoder(codecCtx->codec_id);
	if (!audioCodec_) {
		printf("Can not find encoder! \n");
		return -1;
	}
	if (avcodec_open2(codecCtx, audioCodec_, param == NULL ? NULL : &param) != 0) {
		printf("Failed to open encoder! \n");
		return -1;
	}
	return 0;
}

int Encode::WriteHeader()
{
	if (formatCtx_ == NULL) return -1;
	//Write File Header  
	return avformat_write_header(formatCtx_, NULL);
}

int Encode::WriteVideo(AVPacket *packet)
{
	if (formatCtx_ == NULL) return -1;
	if (videoStream_ == NULL) return -1;
	packet->stream_index = videoStream_->index;
	int ret = av_write_frame(formatCtx_, packet);
	return ret;
}

int Encode::WriteAudio(AVPacket *packet)
{
	if (formatCtx_ == NULL) return -1;
	if (audioStream_ == NULL) return -1;
	packet->stream_index = audioStream_->index;
	int ret = av_write_frame(formatCtx_, packet);
	return ret;
}

int Encode::WriteTrailer()
{
	if (formatCtx_ == NULL) return -1;
	av_write_trailer(formatCtx_);
	return 0;
}

void Encode::Close()
{
	if (videoCodecCtx_) {
		avcodec_close(videoCodecCtx_);
#ifdef USE_NEW_API
		avcodec_free_context(&videoCodecCtx_);
#endif
		videoCodecCtx_ = NULL;
	}
	if (audioCodecCtx_) {
		avcodec_close(audioCodecCtx_);
#ifdef USE_NEW_API
		avcodec_free_context(&audioCodecCtx_);
#endif
		audioCodecCtx_ = NULL;
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
#ifdef USE_NEW_API
	int ret = avcodec_send_frame(audioCodecCtx_, frame);
	if (ret == 0) ret = avcodec_receive_packet(audioCodecCtx_, &audioPacket_);
	if (ret == 0)
	{
		got_picture = 1;
	}
#else
	int ret = avcodec_encode_audio2(audioCodecCtx_, &audioPacket_,
		frame, &got_picture);
#endif
	if (ret < 0) {
		printf("Failed to encode! \n");
		return -1;
	}
	if (got_picture == 1) {
		printf("Succeed to encode frame size:%5d\n", audioPacket_.size);
		ret = WriteAudio(&audioPacket_);
#ifdef USE_NEW_API
		av_packet_unref(&audioPacket_);
#else
		av_free_packet(&audioPacket_);
#endif
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
#ifdef USE_NEW_API
	int ret = avcodec_send_frame(videoCodecCtx_, frame);
	if (ret == 0) ret = avcodec_receive_packet(videoCodecCtx_, &videoPacket_);
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
		printf("Succeed to encode frame size:%5d\n", videoPacket_.size);
		ret = WriteVideo(&videoPacket_);
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

int OriginalEncode::FlushAudio()
{
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(audioCodecCtx_->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
#ifdef USE_NEW_API
		ret = avcodec_receive_packet(audioCodecCtx_, &enc_pkt);
		if (ret == 0)
		{
			got_frame = 1;
		}
#else
		ret = avcodec_encode_audio2(audioCodecCtx_, &enc_pkt,
			NULL, &got_frame);
#endif
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
	int ret = 0;
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

AVCodecContext* OriginalEncode::GetVideoContext()
{
	return videoCodecCtx_;
}

AVStream* OriginalEncode::GetAudioStream()
{
	return audioStream_;
}

AVCodecContext* OriginalEncode::GetAudioContext()
{
	return audioCodecCtx_;
}