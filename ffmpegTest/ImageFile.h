#pragma once

#include "EvHeade.h"

#ifdef _WIN32
void SaveAsBMP(AVFrame *pFrameRGB, int width, int height, int index, int bpp);
#endif