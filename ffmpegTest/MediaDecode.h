//
// Created by jephy on 7/17/17.
//

#ifndef UVCCAMERACTRLDEMO_MEDIADECODE_H
#define UVCCAMERACTRLDEMO_MEDIADECODE_H

#include "EvoInterface/VideoDecoder.h"
#include "EvoInterface/EvoVideoConvert.h"

class MediaDecode
    :protected VideoDecoder
{
public:
    MediaDecode();
    ~MediaDecode();
    int init(int thread_count);
    void SetWidth(int width);
    void SetHeight(int height);
    void SetSPS(uint8_t * sps,int sps_len);
    void SetPPS(uint8_t * pps,int pps_len);
    int decode(uint8_t * data, int32_t size);
    void UseAVCC();
    virtual void SendPacket(AVFrame * frame) = 0;
private:
    EvoVideoConvert convert;
    AVCodecContext	*codecContent;
    AVRational time_base;
    uint8_t * extradata_;
    int width_;
    int height_;
    uint8_t * sps_;
    int sps_len_;
    uint8_t * pps_;
    int pps_len_;
    bool isUseAVCC;
};


#endif //UVCCAMERACTRLDEMO_MEDIADECODE_H
