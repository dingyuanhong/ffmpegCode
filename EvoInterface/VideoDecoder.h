#ifndef VIDEODECODER_H
#define VIDEODECODER_H

#include "EvoHeader.h"
#include "EvoVideoConvert.h"
#include "EvoMediaSource.h"

typedef struct EvoPacket
{
	int64_t pts;
	int64_t dts;
	int64_t timestamp;
	int flags;
	int32_t size;
	uint8_t * data;
 }EvoPacket;

void FreeAVFrame(AVFrame **out);

class VideoDecoder
{
public:
	VideoDecoder(AVCodecContext	*codec);
	~VideoDecoder();

	void Attach(EvoVideoConvert *convert);
	EvoVideoConvert *Detach();

	int DecodeFrame(EvoFrame *packet, AVFrame **evoResult);
	int DecodePacket(EvoPacket *packet, AVFrame **evoResult);
	int Decode(AVPacket *packet, AVFrame **evoResult);

	void SetKeepIFrame(bool keep)
	{
		KeepIFrame = keep;
	}
protected:
	virtual int CreateFrame(AVFrame *out,int Width, int Height, AVPixelFormat Format);
	virtual void FreeFrame(AVFrame **out);
protected:
	AVCodecContext	*VideoCodecCtx;
	AVFrame         *VideoFrame;
	bool			KeepIFrame;

	EvoVideoConvert	*Convert;

	AVPacket *Packet;
};


#endif