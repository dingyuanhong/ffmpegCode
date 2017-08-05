#ifndef AUDIODECODER_H
#define AUDIODECODER_H

#include "EvoHeader.h"
#include "EvoMediaSource.h"

typedef struct EvoAudioInfo {
	int SampleRate;			//������
	int Channels;			//������
	int nb_samples;			//������
	int BitSize;			//�����ֽ���
	enum AVSampleFormat format;
}EvoAudioInfo;

class AudioDecoder
{
public:
	AudioDecoder(AVCodecContext	*codec);
	~AudioDecoder();
	EvoAudioInfo GetTargetInfo();
	void SetTargetInfo(const EvoAudioInfo info);
	EvoAudioInfo GetCorrectTargetInfo();
	void Flush();
	int DecodeFrame(EvoFrame *packet, AVFrame **evoResult);
	int DecodePacket(AVPacket *packet, AVFrame **evoResult);
private:
	virtual AVFrame* CreateAVFrame(EvoAudioInfo	audioInfo, AVFrame *Frame, int align);
	virtual void FreeAVFrame(AVFrame** frame);
	AVFrame* Decode(AVPacket *packet);
	int AudioResampling(AVFrame * audioDecodeFrame,uint8_t * audioBuffer, int bufferLen);
	int64_t GetAudioTimeStamp(AVRational time_base, int64_t pts, int nb_samples, int SampleRate);
private:
	AVCodecContext	*AudioCodecCtx;
	struct SwrContext *AudioConvertCtx;

	AVFrame         *AudioFrame;
	EvoAudioInfo	AudioInfo;
	AVPacket *Packet;
	int64_t audio_frame_next_pts;
};


#endif