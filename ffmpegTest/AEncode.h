#pragma once

#include "EvHeade.h"

class AEncode {
public:
	AEncode(AVCodecContext *codecContext);
	int Encode(AVFrame *frame,AVPacket ** pkt);
	int Encode(AVPacket ** pkt);
	void Flush();
private:
	AVCodecContext *codecContext_;
	int encode_count_;
};