#ifdef __cplusplus
extern "C"
{
#endif

#include "libavcodec\avcodec.h"
#include "libavformat\avformat.h"
#include "libavutil\avutil.h"

#ifdef __cplusplus
}
#endif


#ifdef _WIN32

#pragma comment(lib,"avcodec.lib")
#pragma comment(lib,"avdevice.lib")
#pragma comment(lib,"avfilter.lib")
#pragma comment(lib,"avformat.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib,"postproc.lib")
#pragma comment(lib,"swresample.lib")
#pragma comment(lib,"swscale.lib")

#endif