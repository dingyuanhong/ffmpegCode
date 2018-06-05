#ifndef TRANSFORMADTS_H
#define TRANSFORMADTS_H

#include "EvHeade.h"
#include "exterlFunction.h"

#define ADTS_HEADER_SIZE 7

static const int avpriv_mpeg4audio_sample_rates[16] = {
	96000, 88200, 64000, 48000, 44100, 32000,
	24000, 22050, 16000, 12000, 11025, 8000, 
	7350 ,0 ,0 ,0
};

static int GetSampleIndex(int sample_rate)
{
	for (int i = 0; i < 16; i++)
	{
		if (sample_rate == avpriv_mpeg4audio_sample_rates[i])
		{
			return i;
		}
	}
	return -1;
}

static const uint8_t ff_mpeg4audio_channels[8] = {
	0, 1, 2, 3, 4, 5, 6, 8
};

//https://wiki.multimedia.cx/index.php?title=MPEG-4_Audio

#define PREFILE_LIST \
A(0,"Null") \
A(1,"AAC Main") \
A(2,"AAC LC(Low Complexity)") \
A(3,"AAC SSR(Scalable Sample Rate)") \
A(4,"AAC LTP(Long Term Prediction)") \
A(5,"SBR(Spectral Band Replication)") \
A(6,"AAC Scalable") \
A(7,"TwinVQ") \
A(8,"CELP(Code Excited Linear Prediction)") \
A(9,"HXVC(Harmonic Vector eXcitation Coding)") \
A(10,"Reserved") \
A(11,"Reserved") \
A(12,"TTSI(Text - To - Speech Interface)") \
A(13,"Main Synthesis") \
A(14,"Wavetable Synthesis") \
A(15,"General MIDI") \
A(16,"Algorithmic Synthesis and Audio Effects") \
A(17,"ER(Error Resilient) AAC LC") \
A(18,"Reserved") \
A(19,"ER AAC LTP") \
A(20,"ER AAC Scalable") \
A(21,"ER TwinVQ") \
A(22,"ER BSAC(Bit - Sliced Arithmetic Coding)") \
A(23,"ER AAC LD(Low Delay)") \
A(24,"ER CELP") \
A(25,"ER HVXC") \
A(26,"ER HILN(Harmonic and Individual Lines plus Noise)") \
A(27,"ER Parametric") \
A(28,"SSC(SinuSoidal Coding)") \
A(29,"PS(Parametric Stereo)") \
A(30,"MPEG Surround") \
A(31,"(Escape value)") \
A(32,"Layer - 1") \
A(33,"Layer - 2") \
A(34,"Layer - 3") \
A(35,"DST(Direct Stream Transfer)") \
A(36,"ALS(Audio Lossless)") \
A(37,"SLS(Scalable LosslesS)") \
A(38,"SLS non - core") \
A(39,"ER AAC ELD(Enhanced Low Delay)") \
A(40,"SMR(Symbolic Music Representation) Simple") \
A(41,"SMR Main") \
A(42,"USAC(Unified Speech and Audio Coding) (no SBR)") \
A(43,"SAOC(Spatial Audio Object Coding)") \
A(44,"LD MPEG Surround") \
A(45,"USAC") 

int getProfile(int profile)
{
	switch (profile)
	{
	case FF_PROFILE_AAC_MAIN:
	case FF_PROFILE_AAC_LOW:
	case FF_PROFILE_AAC_SSR:
	case FF_PROFILE_AAC_LTP:
	case FF_PROFILE_AAC_HE:
		return profile;
	case FF_PROFILE_AAC_LD:
		return 23;
	case FF_PROFILE_AAC_HE_V2:
		return 0;
	case FF_PROFILE_AAC_ELD:
		return 39;
	case FF_PROFILE_MPEG2_AAC_LOW:
		return 44;
	case FF_PROFILE_MPEG2_AAC_HE:
		return 30;
	}
	return 0;
}

static const char* ff_mpeg4audio_prefix[] = {
#define A(A,B) B,
	PREFILE_LIST
#undef A
};

typedef struct ADTS{
    char * ADTSHeader;
}ADTS;

static inline void WriteADTSHeader(ADTS * adts,int Size, int sample_rate,int channels,int profile)
{
	if (adts->ADTSHeader == NULL)
	{
		adts->ADTSHeader = (char*)av_malloc(ADTS_HEADER_SIZE);
		memset(adts->ADTSHeader,0, ADTS_HEADER_SIZE);
	}
	profile = getProfile(profile);

	int length = ADTS_HEADER_SIZE + Size;
	length &= 0x1FFF;

	int sample_index = GetSampleIndex(sample_rate);
	int channel = 0;

	if (channels < FF_ARRAY_ELEMS(ff_mpeg4audio_channels))
		channel = ff_mpeg4audio_channels[channels];
	
	int num_blocks = 0;

	char * ADTSHeader = adts->ADTSHeader;
	ADTSHeader[0] = (char)0xFF;
	//MPEG4
	ADTSHeader[1] = (char)0xF1;
	//MPEG2
	//ADTSHeader[1] = (char)0xF9;
	//2
	ADTSHeader[2] = (char)((profile & 0x03) << 6 );
	//4
	ADTSHeader[2] |= (char)((sample_index & 0x0F) << 2);
	//1:0
	//1
	ADTSHeader[2] |= (char)((channel & 0x04) >> 2);
	//2
	ADTSHeader[3] = (char)((channel & 0x03) << 6);
	//4:0
	//2
	ADTSHeader[3] |= (char)(length >> 11);
	//8
	ADTSHeader[4] = (char)((length >> 3) & 0xFF);
	//3
	ADTSHeader[5] = (char)( ((char)(length & 0x07)) << 5);
	//5
	ADTSHeader[5] |= (char)0x1F;
	//6
	ADTSHeader[6] = (char)0xFC;
	//2:0
	ADTSHeader[6] |= (char)(num_blocks & 0x03);
}

static inline int TransformADTS(ADTS * adts,AVCodecContext* context,AVPacket *src, AVPacket **des)
{
	if (src == NULL) return -1;
	if (des == NULL) return -1;

	AVPacket *adtsPacket = av_packet_alloc();
	av_init_packet(adtsPacket);
	av_new_packet(adtsPacket, src->size + ADTS_HEADER_SIZE);

	//printf("ADTS:%d %d\n",context->sample_rate, context->channels);

	WriteADTSHeader(adts,src->size, context->sample_rate, context->channels, context->profile);

	memcpy(adtsPacket->data, adts->ADTSHeader, ADTS_HEADER_SIZE);
	memcpy(adtsPacket->data + ADTS_HEADER_SIZE, src->data, src->size);

	adtsPacket->pts = src->pts;
	adtsPacket->dts = src->dts;
	adtsPacket->duration = src->duration;
	adtsPacket->flags = src->flags;
	adtsPacket->stream_index = src->stream_index;
	adtsPacket->pos = src->pos;

	if (*des == src)
	{
		av_packet_unref(src);
		av_packet_move_ref(*des, adtsPacket);
	}
	else if (*des != NULL)
	{
		av_packet_move_ref(*des, adtsPacket);
	}
	else
	{
		*des = adtsPacket;
	}

	return 0;
}



#endif