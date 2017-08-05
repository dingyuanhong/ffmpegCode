#pragma once
#include "EvoHeader.h"

#ifdef __cplusplus
extern "C"
{
#endif

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/time.h"
//#include "libavutil/timestamp.h"
#include "libavutil/intreadwrite.h"

#include "libavformat/avio.h"
#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

#include "internal.h"

#include "libavutil/ffversion.h"

#ifdef __cplusplus
}
#endif


int self_avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options);