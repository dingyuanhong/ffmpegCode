//
// Created by jephy on 7/17/17.
//

#include "MediaDecode.h"
#include "../EvoInterface/sei_packet.h"
#ifndef _WIN32
#include "android/log.h"
#endif

MediaDecode::MediaDecode()
:VideoDecoder(NULL),codecContent(NULL)
{
    time_base.num = 0;
    time_base.den = 1;
    extradata = NULL;
}

MediaDecode::~MediaDecode()
{
    if(codecContent != NULL)
    {
        avcodec_close(codecContent);
        avcodec_free_context(&codecContent);
    }
    if(extradata != NULL)
    {
        av_free(extradata);
        extradata = NULL;
    }
}
char  sps[] = {0x67,0x64,0x00,0x33,0xac,0x1b,0x1a,0x80,0x2f,0x80,0xbf,0xa1,0x00,0x00,0x03,0x00,0x01,0x00,0x00,0x03,0x00,0x3c,0x8f,0x14,0x2a,0xa0};
char pps[] = {0x68,0xee,0x0b,0xcb};

AVCodec *GetBestVideoDecoder(AVCodecID id,std::string Name) {
    if (Name.length() == 0) return NULL;

    AVCodec *c_temp = av_codec_next(NULL);
    while (c_temp != NULL) {
        if (c_temp->id == id && c_temp->type == AVMEDIA_TYPE_VIDEO
            && c_temp->decode != NULL)
        {
            //__android_log_print(ANDROID_LOG_INFO,"native","Video H264 decode:%s\n",c_temp->name);
        }
        c_temp = c_temp->next;
    }

    std::string name = Name;
    std::string decoder;
    while (true) {
        size_t pos = name.find(" ");
        if (pos != -1) {
            decoder = name.substr(0, pos);
            name = name.substr(pos + 1);
        }
        else {
            decoder = name;
            name = "";
        }
        if (decoder.length() > 0) {
            AVCodec * codec = avcodec_find_decoder_by_name(decoder.c_str());
            if (codec != NULL && codec->id == id) return codec;
        }
        if (name.length() == 0) break;
    }
    return NULL;
}

uint8_t * MakeExtraData(char * sps,int sps_len,char * pps,int pps_len,int * out_len)
{
    int extraSize = 5 + 1 + 2 + sps_len + 1 + 2 + pps_len;
    uint8_t * extraBuffer = (uint8_t*)av_malloc(extraSize);
    memset(extraBuffer,0,extraSize);
    uint8_t * extra = extraBuffer;
    extra[0] = 0x01;
    extra[1] = sps[1];
    extra[2] = sps[2];
    extra[3] = sps[3];
    extra[4] = 0xFF;
    extra[5] = 0xE1;
    extra[6] = sps_len >> 8;
    extra[7] = sps_len & 0xFF;
    memcpy(extra + 8,sps,sps_len);
    extra += 8 + sps_len;
    extra[0] = 0x01;
    extra[1] = pps_len >> 8;
    extra[2] = pps_len & 0xFF;
    memcpy(extra + 3,pps,pps_len);

    if(out_len != NULL) *out_len = extraSize;
    return extraBuffer;
}

void av_log_callback(void*, int level, const char* fmt, va_list)
{
    //__android_log_print(ANDROID_LOG_INFO,"native","al_log:%s",fmt);
}

int MediaDecode::init(int thread_count)
{
    av_register_all();
    AVCodec *codec = GetBestVideoDecoder(AV_CODEC_ID_H264,"h264_mediacodec");
    if(codec == NULL) codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    else thread_count = 0;
    if(codec == NULL) return -1;
    //__android_log_print(ANDROID_LOG_INFO,"native","AVCodec:%s",codec->name);
    codecContent = avcodec_alloc_context3(codec);
    if(thread_count > 1)
    {
        codecContent->active_thread_type = 1;
        codecContent->thread_count = thread_count;
    }
    int out_size = 0;
    uint8_t * extraData = MakeExtraData(sps,sizeof(sps)/sizeof(sps[0]),pps,sizeof(pps)/sizeof(pps[0]),&out_size);
    if(extraData != NULL)
    {
        codecContent->extradata = extraData;
        codecContent->extradata_size = out_size;
    }
    if(extradata != NULL)
    {
        av_free(extradata);
        extradata = NULL;
    }
    extradata = extraData;
    codecContent->width = 3040;
    codecContent->height = 1520;
    av_log_set_callback(av_log_callback);
    int ret = 0;
    if((ret = avcodec_open2(codecContent,codec,NULL)) != 0)
    {
        char errbuf[64] = {0};
        av_strerror(ret, errbuf, 64);
        avcodec_free_context(&codecContent);
        codecContent = NULL;
        return -1;
    }
    if(VideoCodecCtx  != NULL)
    {
        avcodec_close(VideoCodecCtx);
        avcodec_free_context(&VideoCodecCtx);
        VideoCodecCtx = NULL;
    }
    VideoCodecCtx = codecContent;


    struct EvoVideoInfo info;
    info.Width = 0;
    info.Height = 0;
    info.Format = AV_PIX_FMT_NONE;
    struct EvoVideoInfo des = info;
    des.Format = AV_PIX_FMT_YUV420P;
    convert.Initialize(info,des);
    Attach(&convert);

    return 0;
}

int MediaDecode::decode(uint8_t * data, int32_t size)
{
    if (VideoCodecCtx == NULL) return -1;
    AVFrame * evoResult = NULL;
    int ret = 0;
    if(data != NULL && size > 0)
    {
        EvoPacket packet = {0};
        packet.data = data;
        packet.size = size;

        char buffer[256] = {0};
        int count = 256;
        ret = get_sei_content(data,size,buffer,&count);
//        __android_log_print(ANDROID_LOG_DEBUG,"JNI-DECODER","buffer = %s",buffer);

        if(ret > 0)
        {
            int cflags = 0;
            int64_t cpts = 0;
            int64_t cdts = 0;
            int64_t ctimestamp = 0;
            int ctime_base_num = 0;
            int ctime_base_den = 0;

            sscanf(buffer,"flags:%d pts:%llu dts:%llu timestamp:%llu time_base:num:%d den:%d",
                  &cflags,&cpts,&cdts,&ctimestamp,&ctime_base_num,&ctime_base_den);

            time_base.den = ctime_base_den;
            time_base.num = ctime_base_num;
            if(time_base.den == 0)
            {
                time_base.num = 0;
                time_base.den = 1;
            }

            packet.pts = cpts;
            packet.dts = cdts;
            packet.flags = cflags;
//            __android_log_print(ANDROID_LOG_DEBUG,"JNI-DECODER","pts:%lld dts:%lld",cpts,cdts);
        } else
        {
            packet.pts = AV_NOPTS_VALUE;
            packet.dts = AV_NOPTS_VALUE;
        }
        int64_t timeBegin = av_gettime()/1000;
        ret = DecodePacket(&packet,&evoResult);
        int64_t timeEnd = av_gettime() / 1000;
        //__android_log_print(ANDROID_LOG_INFO,"native MediaDecode","use:%lld",timeEnd - timeBegin);

    } else{
        ret = DecodePacket((EvoPacket*)NULL,&evoResult);
    }
    if(evoResult != NULL)
    {
        int64_t timestamp = (evoResult->pts != AV_NOPTS_VALUE) ? (evoResult->pts * av_q2d(time_base) * 1000) :
#ifndef USE_NEW_API
                            (evoResult->pkt_pts != AV_NOPTS_VALUE) ? (evoResult->pkt_pts * av_q2d(time_base) * 1000) :
#endif
                            (evoResult->pkt_dts != AV_NOPTS_VALUE) ? (evoResult->pkt_dts * av_q2d(time_base) * 1000) : NAN;

        evoResult->pts = timestamp;
        SendPacket(evoResult);
        FreeFrame(&evoResult);
    }
    return ret;
}