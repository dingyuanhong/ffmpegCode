#pragma once
#ifndef SEIENCODE_H
#define SEIENCODE_H

#include "Encode.h"

class SEIEncode
	:public OriginalEncode
{
public:
	SEIEncode();
	virtual int EncodeVideo(AVFrame* frame);
	virtual int FlushVideo();
};

#endif