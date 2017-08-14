#pragma once

#include "EvHeade.h"

class Encode
{
public:
	Encode();
	int Open(const char * file);
	void Close();

	int NewVideoStream(int width, int height, AVPixelFormat format,int frameRate = 30);
	int NewVideoStream(AVStream * stream);
	//format : 样本格式 ,AV_SAMPLE_FMT_S16
	//smapleRate:采样率 , 44100
	//channel_layout:通道类型 ,AV_CH_LAYOUT_STEREO
	int NewAudioStream(enum AVSampleFormat format, int smapleRate, uint64_t channel_layout);
	int NewAudioStream(AVStream* stream);

	int WriteHeader();
	int WriteVideo(AVPacket *packet);
	int WriteAudio(AVPacket *packet);
	int WriteTrailer();
protected:
	AVFormatContext* formatCtx_ = NULL;
	AVOutputFormat* output_ = NULL;

	AVStream* videoStream_ = NULL;
	AVCodecContext* videoCodecCtx_ = NULL;
	AVCodec* videoCodec_ = NULL;

	AVStream* audioStream_ = NULL;
	AVCodecContext* audioCodecCtx_ = NULL;
	AVCodec* audioCodec_ = NULL;
};

class OriginalEncode
	:public Encode
{
public:
	virtual int EncodeAudio(AVFrame* frame);
	virtual int EncodeVideo(AVFrame* frame);
	virtual int FlushAudio();
	virtual int FlushVideo();

	AVStream* GetVideoStream();
	AVStream* GetAudioStream();

	AVCodecContext* GetVideoContext();
	AVCodecContext* GetAudioContext();
protected:
	AVPacket videoPacket_;
	AVPacket audioPacket_;
};