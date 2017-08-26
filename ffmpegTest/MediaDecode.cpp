//
// Created by jephy on 7/17/17.
//

#include "MediaDecode.h"
#include "EvoInterface/sei_packet.h"
#ifndef _WIN32
#include "android/log.h"
#endif

inline int GetAnnexbLength(uint8_t * nalu, int nalu_size)
{
    unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
    unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

    unsigned char *data = nalu;
    int size = nalu_size;
    if(data == NULL)
    {
        return 0;
    }
    if ((size > 3 && memcmp(data, ANNEXB_CODE_LOW,3) == 0))
    {
        return 3;
    }
    else if((size > 4 && memcmp(data, ANNEXB_CODE,4) == 0))
    {
        return 4;
    }
    return 0;
}

//MP4模式扩展数据
inline uint8_t * MakeExtraData(uint8_t * sps, int sps_len, uint8_t * pps, int pps_len, int * out_len)
{
    int sps_header = GetAnnexbLength(sps,sps_len);
    int pps_header = GetAnnexbLength(pps,pps_len);
    sps += sps_header;
    sps_len -= sps_header;
    pps += pps_header;
    pps_len -= pps_header;

    int extraSize = 5 + 1 + 2 + sps_len + 1 + 2 + pps_len;
    uint8_t * extraBuffer = (uint8_t*)av_malloc(extraSize);
    memset(extraBuffer, 0, extraSize);
    uint8_t * extra = extraBuffer;
    extra[0] = 0x01;
    extra[1] = sps[1];
    extra[2] = sps[2];
    extra[3] = sps[3];
    extra[4] = 0xFF;
    extra[5] = 0xE1;
    extra[6] = sps_len >> 8;
    extra[7] = sps_len & 0xFF;
    memcpy(extra + 8, sps, sps_len);
    extra += 8 + sps_len;
    extra[0] = 0x01;
    extra[1] = pps_len >> 8;
    extra[2] = pps_len & 0xFF;
    memcpy(extra + 3, pps, pps_len);

    if (out_len != NULL) *out_len = extraSize;
    return extraBuffer;
}

//标准H264扩展数据
inline uint8_t * MakeH264ExtraData(uint8_t * sps, int sps_len, uint8_t * pps, int pps_len, int * out_len)
{
    int sps_header = GetAnnexbLength(sps,sps_len);
    int pps_header = GetAnnexbLength(pps,pps_len);
    sps += sps_header;
    sps_len -= sps_header;
    pps += pps_header;
    pps_len -= pps_header;

    int extraSize = 4 + sps_len + 4 + pps_len;
    uint8_t * extraBuffer = (uint8_t*)av_malloc(extraSize);
    memset(extraBuffer, 0, extraSize);
    uint8_t * extra = extraBuffer;
    extra[3] = 0x01;
    memcpy(extra + 4, sps, sps_len);
    extra += 4 + sps_len;
    extra[3] = 0x01;
    memcpy(extra + 4, pps, pps_len);

    if (out_len != NULL) *out_len = extraSize;
    return extraBuffer;
}

inline AVCodec *GetBestVideoDecoder(AVCodecID id,const char* n) {
    if(n == NULL) return NULL;
    std::string Name = n;
    if (Name.length() == 0) return NULL;

    AVCodec *c_temp = av_codec_next(NULL);
    while (c_temp != NULL) {
        if (c_temp->id == id && c_temp->type == AVMEDIA_TYPE_VIDEO
            && c_temp->decode != NULL)
        {
#ifndef _WIN32
            __android_log_print(ANDROID_LOG_INFO,"native","Video H264 decode:%s\n",c_temp->name);
#endif
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

MediaDecode::MediaDecode()
:VideoDecoder(NULL),codecContent(NULL)
{
    time_base.num = 0;
    time_base.den = 1;
    extradata_ = NULL;

    width_ = 0;
    height_ = 0;
    sps_ = NULL;
    sps_len_ = 0;
    pps_ = NULL;
    pps_len_ = 0;

    isUseAVCC  = false;
}

MediaDecode::~MediaDecode()
{
    if(codecContent != NULL)
    {
        avcodec_close(codecContent);
        codecContent->extradata = NULL;
        codecContent->extradata_size = 0;
        avcodec_free_context(&codecContent);
        codecContent = NULL;
    }
    if(extradata_ != NULL)
    {
        av_free(extradata_);
        extradata_ = NULL;
    }

    if(sps_ != NULL)
    {
        av_free(sps_);
        sps_ = NULL;
        sps_len_ = 0;
    }

    if(pps_ != NULL)
    {
        av_free(pps_);
        pps_ = NULL;
        pps_len_ = 0;
    }
}

//static uint8_t  static_sps[] = {0x67,0x64,0x00,0x33,0xac,0x1b,0x1a,0x80,0x2f,0x80,0xbf,0xa1,0x00,0x00,0x03,0x00,0x01,0x00,0x00,0x03,0x00,0x3c,0x8f,0x14,0x2a,0xa0};
//static uint8_t static_pps[] = {0x68,0xee,0x0b,0xcb};

int MediaDecode::init(const char * name,int thread_count)
{
    av_register_all();
    if(width_ <= 0 || height_ <= 0) return AVERROR_EXTERNAL;
    if(sps_ == NULL || sps_len_ <= 0) return AVERROR_EXTERNAL;
    if(pps_ == NULL || pps_len_ <= 0) return AVERROR_EXTERNAL;
    //"h264_mediacodec"
    AVCodec *codec = GetBestVideoDecoder(AV_CODEC_ID_H264,name);
    if(codec == NULL) codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if(codec == NULL) return AVERROR_EXTERNAL;
#ifndef _WIN32
    __android_log_print(ANDROID_LOG_INFO,"native","AVCodec:%s",codec->name);
#endif
    codecContent = avcodec_alloc_context3(codec);
    if(thread_count > 1)
    {
        codecContent->active_thread_type = 1;
        codecContent->thread_count = thread_count;
    }
    int out_size = 0;
    uint8_t * extraData = NULL;
    if(isUseAVCC)
    {
        //extraData = MakeExtraData(static_sps,sizeof(static_sps)/sizeof(static_sps[0]),static_pps,sizeof(static_pps)/sizeof(static_pps[0]),&out_size);
        extraData = MakeExtraData(sps_,sps_len_,pps_,pps_len_,&out_size);
    }
    else
    {
//        extraData = MakeH264ExtraData(static_sps,sizeof(static_sps)/sizeof(static_sps[0]),static_pps,sizeof(static_pps)/sizeof(static_pps[0]),&out_size);
        extraData = MakeH264ExtraData(sps_,sps_len_,pps_,pps_len_,&out_size);
    }

    if(extraData != NULL)
    {
        codecContent->extradata = extraData;
        codecContent->extradata_size = out_size;
    }
    if(extradata_ != NULL)
    {
        av_free(extradata_);
        extradata_ = NULL;
    }
    extradata_ = extraData;

    codecContent->width = width_;
    codecContent->height = height_;
    codecContent->coded_width = width_;
    codecContent->coded_height = height_;
    int ret = 0;
    if((ret = avcodec_open2(codecContent,codec,NULL)) != 0)
    {
        char errbuf[64] = {0};
        av_strerror(ret, errbuf, 64);
        codecContent->extradata_size = 0;
        codecContent->extradata = NULL;
        avcodec_free_context(&codecContent);
        codecContent = NULL;
        return ret;
    }
    if(VideoCodecCtx  != NULL)
    {
        avcodec_close(VideoCodecCtx);
        codecContent->extradata_size = 0;
        codecContent->extradata = NULL;
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

void MediaDecode::SetWidth(int width)
{
    width_ = width;
}

void MediaDecode::SetHeight(int height)
{
    height_ = height;
}

void MediaDecode::SetSPS(uint8_t * sps,int sps_len)
{
    if(sps_ != NULL)
    {
        av_free(sps_);
        sps_ = NULL;
        sps_len_ = 0;
    }
    if(sps == NULL || sps_len == 0) return;
    sps_ = (uint8_t*)av_malloc(sps_len);
    memcpy(sps_,sps,sps_len);
    sps_len_ = sps_len;
}

void MediaDecode::SetPPS(uint8_t * pps,int pps_len)
{
    if(pps_ != NULL)
    {
        av_free(pps_);
        pps_ = NULL;
        pps_len_ = 0;
    }
    if(pps == NULL || pps_len == 0) return;
    pps_ = (uint8_t*)av_malloc(pps_len);
    memcpy(pps_,pps,pps_len);
    pps_len_ = pps_len;
}

void MediaDecode::UseAVCC()
{
    isUseAVCC = true;
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

        uint8_t *buffer = NULL;
        int count = 256;
        ret = get_sei_content(data,size, TIME_STAMP_UUID,&buffer,&count);
#ifndef _WIN32
//        __android_log_print(ANDROID_LOG_DEBUG,"JNI-DECODER","buffer = %s",buffer);
#endif
        if(buffer != NULL)
        {
            int cflags = 0;
            int64_t cpts = 0;
            int64_t cdts = 0;
            int64_t ctimestamp = 0;
            int ctime_base_num = 0;
            int ctime_base_den = 0;

            sscanf((const char*)buffer,"flags:%d pts:%llu dts:%llu timestamp:%llu time_base:num:%d den:%d",
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

			free_sei_content(&buffer);
#ifndef _WIN32
//            __android_log_print(ANDROID_LOG_DEBUG,"JNI-DECODER","pts:%lld dts:%lld",cpts,cdts);
#endif
        } else
        {
            packet.pts = AV_NOPTS_VALUE;
            packet.dts = AV_NOPTS_VALUE;
        }
        int64_t timeBegin = av_gettime()/1000;
        ret = DecodePacket(&packet,&evoResult);
        int64_t timeEnd = av_gettime() / 1000;
#ifndef _WIN32
        __android_log_print(ANDROID_LOG_INFO,"native MediaDecode","use:%lld",timeEnd - timeBegin);
#endif
    } else{
        int64_t timeBegin = av_gettime()/1000;
        ret = DecodePacket((EvoPacket*)NULL,&evoResult);
        int64_t timeEnd = av_gettime() / 1000;
#ifndef _WIN32
        __android_log_print(ANDROID_LOG_INFO,"native MediaDecode","use:%lld",timeEnd - timeBegin);
#endif
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