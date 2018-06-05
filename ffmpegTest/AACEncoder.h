#ifndef _AACENCODER_H
#define _AACENCODER_H
#include <stdlib.h>

typedef void AACEncoderObj;

struct AACEncoderParams
{
    unsigned int SampleRate;
    unsigned int SampleBit;
    unsigned int BitRate;
    unsigned int SoundChannel;
};

AACEncoderObj * AACEncoderInit(struct AACEncoderParams * params);
int AACEncoderSetBuffer(AACEncoderObj * obj, unsigned char *rawBuffer, unsigned int bufferLen);
int AACEncoderGetBufferLen(AACEncoderObj * obj);
int AACEncode(AACEncoderObj * obj, unsigned char * aacBuffer);
void AACEncoderDestroy(AACEncoderObj * obj);

#endif

