#ifndef EVOMEDIASOURCE_H
#define EVOMEDIASOURCE_H

#include "EvoHeader.h"
#include <string>


struct EvoFrame {
	int64_t pts;
	int64_t dts;
	int64_t timestamp;
	int flags;
	int size;
	uint8_t data[0];
};

void EvoFreeFrame(EvoFrame ** frame);

typedef struct EvoMediaSourceConfig
{
	bool UseAnnexb;
	bool UseSei;
}EvoMediaSourceConfig;

class EvoMediaSource
{
public:
	EvoMediaSource();
	~EvoMediaSource();
	//���ļ�
	//����ֵ:0:�ɹ� !0:ʧ��
	int Open(const char * file, EvoMediaSourceConfig *config = NULL, enum AVMediaType codecType = AVMEDIA_TYPE_VIDEO);
	//�����ر�
	void Close();
	//��ת,��λ:����
	//����ֵ:0:�ɹ� !0:ʧ��
	virtual int Seek(int64_t millisecond);
	//��ȡ֡
	//����ֵ:0:�ɹ� AVERROR_EOF:�ļ����� !0:ʧ��
	virtual int ReadFrame(EvoFrame** out);
	//��ȡ��չ����
	//����ֵ:��չ���ݴ�С
	int GetExtData(uint8_t * data, int size);
	//PPS
	int GetPPS(uint8_t * data, int size);
	//SPS
	int GetSPS(uint8_t * data, int size);
	//ʱ��
	//����ֵ:��λ:����
	int GetDuration();
	//֡��
	int GetFrameRate();
	//֡��
	int GetFrameCount();
	//��
	int GetWidth();
	//��
	int GetHeight();
	AVStream * GetVideoStream();
	AVCodecContext * GetCodecContext();
	void SetVideoCodecName(const char * codec);
	AVCodec *GetBestDecoder(std::string name, AVMediaType codecType, AVCodecID id);
private:
	void ChangeAnnexb(EvoFrame * frame);
	int AnalysisVideoPPSSPS();
private:
	AVFormatContext * context_;
	AVPacket * packet_;
	int videoIndex_;
	AVStream *videoStream_;
	AVCodecContext *codecContext_;
	uint8_t * pps_data_;
	int pps_size_;
	uint8_t * sps_data_;
	int sps_size_;
	std::string VideoDecoderName;

	EvoMediaSourceConfig Config_;
};


#endif