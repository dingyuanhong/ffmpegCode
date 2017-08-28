#ifdef __cplusplus

#include "../EvoInterface/EvoHeader.h"
#include "../EvoInterface/EvoMediaSource.h"
#include "../EvoInterface/VideoDecoder.h"
#include "../EvoInterface/sei_packet.h"

#ifdef _WIN32

#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#endif

#endif