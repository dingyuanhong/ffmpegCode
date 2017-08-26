#ifndef EVOHEADER_H
#define EVOHEADER_H

//#define USE_NEW_API

#ifdef __cplusplus
extern "C" {
#endif

#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"

#ifdef USE_NEW_API
#include "libavcodec/jni.h"
#endif

#ifdef __cplusplus
}
#endif



#endif
