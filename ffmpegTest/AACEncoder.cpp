#include "AACEncoder.h"
#include "EvHeade.h"

//±àÂëÆ÷
struct AACEncoderStruct
{
    unsigned int SampleRate;
    unsigned int SampleBit;
    unsigned int BitRate;
    unsigned int SoundChannel;
    unsigned int BufferLen;
    AVCodec * AACCodec;
    AVCodecContext *AACCodecCtx;
    AVFrame *AACFrame;
    unsigned char * FrameBuffer;
    AVPacket AACPacket;
};

//×¢²á±àÂëÆ÷
AACEncoderObj * AACEncoderInit(struct AACEncoderParams * params)
{
    struct AACEncoderStruct * encoderObj = (struct AACEncoderStruct*)malloc(sizeof(struct AACEncoderStruct));
    memset(encoderObj, 0, sizeof(struct AACEncoderStruct));
    encoderObj->SampleBit = params->SampleBit;
    encoderObj->BitRate = params->BitRate;
    encoderObj->SampleRate = params->SampleRate;
    encoderObj->SoundChannel = params->SoundChannel;

    avcodec_register_all();

    int initRet = 0;
    do{
        //²éÕÒ±àÂëÆ÷
        encoderObj->AACCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (NULL == encoderObj->AACCodec){
            break;
        }

        encoderObj->AACCodecCtx = avcodec_alloc_context3(encoderObj->AACCodec);
        if (NULL == encoderObj->AACCodecCtx){
            break;
        }

        encoderObj->AACCodecCtx->codec_id = AV_CODEC_ID_AAC;
        if (8 == encoderObj->SampleBit){
            encoderObj->AACCodecCtx->sample_fmt = AV_SAMPLE_FMT_U8;
        }
        else if (16 == encoderObj->SampleBit){
            encoderObj->AACCodecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
        }
        else if (32 == encoderObj->SampleBit){
            encoderObj->AACCodecCtx->sample_fmt = AV_SAMPLE_FMT_S32;
        }
        encoderObj->AACCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
        encoderObj->AACCodecCtx->sample_rate = encoderObj->SampleRate;
        encoderObj->AACCodecCtx->channels = encoderObj->SoundChannel;
        encoderObj->AACCodecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
        encoderObj->AACCodecCtx->bit_rate = encoderObj->BitRate;
        encoderObj->AACCodecCtx->channels = av_get_channel_layout_nb_channels(encoderObj->AACCodecCtx->channel_layout);
        
        if (avcodec_open2(encoderObj->AACCodecCtx, encoderObj->AACCodec, NULL) < 0){
            break;
        }

        encoderObj->AACFrame = av_frame_alloc();
        if (NULL == encoderObj->AACFrame){
            break;
        }

        encoderObj->BufferLen = av_samples_get_buffer_size(NULL, encoderObj->AACCodecCtx->channels, encoderObj->AACCodecCtx->frame_size, encoderObj->AACCodecCtx->sample_fmt, 1);
        encoderObj->AACFrame->nb_samples = encoderObj->AACCodecCtx->frame_size;
        encoderObj->AACFrame->format = encoderObj->AACCodecCtx->sample_fmt;

        encoderObj->FrameBuffer = (unsigned char *)malloc(encoderObj->BufferLen);
        avcodec_fill_audio_frame(
            encoderObj->AACFrame,
            encoderObj->AACCodecCtx->channels,
            encoderObj->AACCodecCtx->sample_fmt,
            (const uint8_t*)(encoderObj->FrameBuffer),
            encoderObj->BufferLen,
            1
            );
        initRet = 1;
    } while(0);
    
    if (!initRet){
        AACEncoderDestroy(encoderObj);
        return NULL;
    }

    return encoderObj;
}

//Ïú»Ù±àÂëÆ÷
void AACEncoderDestroy(AACEncoderObj * obj)
{
    struct AACEncoderStruct * encoderObj = (struct AACEncoderStruct*)obj;
    if (NULL != encoderObj->AACCodec){
        av_free(encoderObj->AACCodec);
        encoderObj->AACCodec = NULL;
    }

    if (NULL != encoderObj->AACCodecCtx){
        avcodec_close(encoderObj->AACCodecCtx);
        avcodec_free_context(&(encoderObj->AACCodecCtx));
        encoderObj->AACCodecCtx = NULL;
    }

    if (NULL != encoderObj->AACFrame){
        av_free(encoderObj->AACFrame);
        encoderObj->AACFrame = NULL;
    }
    if (NULL != encoderObj->FrameBuffer){
        free(encoderObj->FrameBuffer);
    }

    free(encoderObj);
}

int AACEncoderGetBufferLen(AACEncoderObj * obj)
{
    struct AACEncoderStruct * encoderObj = (struct AACEncoderStruct*)obj;
    return encoderObj->BufferLen;
}

//ÉèÖÃbuffer
int AACEncoderSetBuffer(AACEncoderObj * obj, unsigned char *rawBuffer, unsigned int bufferLen)
{
    struct AACEncoderStruct * encoderObj = (struct AACEncoderStruct*)obj;
    if (bufferLen != encoderObj->BufferLen){
        return 0;
    }
    memcpy(encoderObj->FrameBuffer, rawBuffer, bufferLen);
    return 1;
}


//±àÂë£¬·µ»Øaacbuffer³¤¶È
int AACEncode(AACEncoderObj * obj, unsigned char * aacBuffer)
{
    struct AACEncoderStruct * encoderObj = (struct AACEncoderStruct*)obj;
    int aacBufferLen = 0;
    int got_packet_ptr = 0;

    av_init_packet(&(encoderObj->AACPacket));
    encoderObj->AACPacket.data = NULL;    // packet data will be allocated by the encoder
    encoderObj->AACPacket.size = 0;
#ifndef USE_NEW_API
    int u_size = avcodec_encode_audio2(
        encoderObj->AACCodecCtx,
        &(encoderObj->AACPacket),
        encoderObj->AACFrame,
        &got_packet_ptr
        );
    if ((0 == u_size) && (1 == got_packet_ptr) && (NULL != encoderObj->AACPacket.data) && (encoderObj->AACPacket.size > 0))
    {
        aacBufferLen = encoderObj->AACPacket.size;
        memcpy(aacBuffer, encoderObj->AACPacket.data, aacBufferLen);
    }
    av_free_packet(&(encoderObj->AACPacket));
#endif
    return aacBufferLen;
}