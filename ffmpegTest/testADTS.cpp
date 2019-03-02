#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"
#include "TransformADTS.h"
#include "../EvoInterface/AudioDecoder.h"
#include "AACEncoder.h"
#include "AEncode.h"

//https://blog.csdn.net/zhangrui_fslib_org/article/details/50756640

#ifdef USE_NEW_API
static AVCodecContext *CreateCodecContent(AVCodecParameters *codecpar)
{
	AVCodecContext *codecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecContext, codecpar);
	return codecContext;
}
#endif

#define LOGD printf

//"../video.264"
int testADTS(const char * infile, const char * outfile)
{
	av_register_all();

	AVFormatContext * formatContext = avOpenFile(infile);
	if (formatContext == NULL)
	{
		return -1;
	}
	int audioIndex = getStreamId(formatContext, AVMEDIA_TYPE_AUDIO);

	if (audioIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVStream * stream = formatContext->streams[audioIndex];
#ifdef USE_NEW_API
	AVCodecContext *codecContext_ = CreateCodecContent(stream->codecpar);
#else
	AVCodecContext *codecContext_ = stream->codec;
#endif
	if (stream == NULL || !(
		codecContext_ != NULL &&
		codecContext_->codec_type == AVMEDIA_TYPE_AUDIO &&
		codecContext_->codec_id == AV_CODEC_ID_AAC
		))
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	int ret = 0;

	AVPacket * packet = av_packet_alloc();

	ADTS adts = { NULL };
	AVPacket *des = av_packet_alloc();
	av_init_packet(des);

	FILE * fp = fopen(outfile, "wb");

	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == audioIndex)
			{
				TransformADTS(&adts, codecContext_, packet, &des);
				if (des != NULL)
				{
					LOGD("ADTS:%x %x %x %x %x %x %x  -  %x %x %x %x\n", des->data[0], des->data[1], des->data[2], des->data[3],
						des->data[4], des->data[5], des->data[6],
						des->data[7], des->data[8], des->data[9], des->data[10]);
					printf("block size:%d\n",packet->size);
					fwrite(des->data, des->size, 1, fp);
					av_packet_unref(des);
				}
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	avformat_close_input(&formatContext);
	return 0;
}

int decodeAAC(const char * infile, const char * outfile)
{
	av_register_all();

	AVFormatContext * formatContext = avOpenFile(infile);
	if (formatContext == NULL)
	{
		return -1;
	}
	int audioIndex = getStreamId(formatContext, AVMEDIA_TYPE_AUDIO);

	if (audioIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVStream * stream = formatContext->streams[audioIndex];

#ifdef USE_NEW_API
	AVCodecContext *codecContext_ = CreateCodecContent(stream->codecpar);
#else
	AVCodecContext *codecContext_ = stream->codec;
#endif

	if (stream == NULL || !(
		codecContext_ != NULL &&
		codecContext_->codec_type == AVMEDIA_TYPE_AUDIO &&
		codecContext_->codec_id == AV_CODEC_ID_AAC
		))
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	int ret = 0;

	AVPacket * packet = av_packet_alloc();
	
	AudioDecoder decode(codecContext_);

	FILE * fp = fopen(outfile, "wb");

	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == audioIndex)
			{
				AVFrame * frame = NULL;
				decode.DecodePacket(packet, &frame);
				if (frame != NULL)
				{
					int DataSize = av_samples_get_buffer_size(NULL, frame->channels, frame->nb_samples, (AVSampleFormat)frame->format, 1);
					fwrite(frame->buf[0],1, DataSize,fp);

					av_frame_unref(frame);
					av_frame_free(&frame);
				}
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}

	av_packet_free(&packet);

	fclose(fp);

	avformat_close_input(&formatContext);
	return 0;
}

int processAAC(const char * infile, const char * outfile)
{
	av_register_all();

	AVFormatContext * formatContext = avOpenFile(infile);
	if (formatContext == NULL)
	{
		return -1;
	}
	int audioIndex = getStreamId(formatContext, AVMEDIA_TYPE_AUDIO);

	if (audioIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVStream * stream = formatContext->streams[audioIndex];

#ifdef USE_NEW_API
	AVCodecContext *codecContext_ = CreateCodecContent(stream->codecpar);
#else
	AVCodecContext *codecContext_ = stream->codec;
#endif

	if (stream == NULL || !(
		codecContext_ != NULL &&
		codecContext_->codec_type == AVMEDIA_TYPE_AUDIO &&
		codecContext_->codec_id == AV_CODEC_ID_AAC
		))
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	int ret = 0;

	AVPacket * packet = av_packet_alloc();

	AudioDecoder decode(codecContext_);

	AVCodecContext * audioContext = avcodec_alloc_context3(NULL);
	audioContext->codec_id = AV_CODEC_ID_AAC;
	audioContext->codec_type = AVMEDIA_TYPE_AUDIO;

	audioContext->sample_fmt = codecContext_->sample_fmt;//AV_SAMPLE_FMT_S32;
	audioContext->sample_rate = codecContext_->sample_rate;
	audioContext->channels = codecContext_->channels;
	audioContext->channel_layout = codecContext_->channel_layout; //AV_CH_LAYOUT_STEREO;
	audioContext->channels = av_get_channel_layout_nb_channels(audioContext->channel_layout);

	audioContext->bit_rate = codecContext_->bit_rate;
	audioContext->frame_size = codecContext_->frame_size;

	AVDictionary* options = NULL;
	//av_dict_set(&options, "-strict", "2", 0);

	AVCodec * codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
	ret = avcodec_open2(audioContext, codec, options==NULL?NULL:&options);
	if (ret < 0) {
		char errbuf[1024] = { 0 };
		av_strerror(ret, errbuf, 1024);
		printf("error avcodec_open2:%d(%s).\n", ret, errbuf);

		return -1;
	}

	AEncode encode(audioContext);

	ADTS adts = { NULL };
	AVPacket *adts_packet = av_packet_alloc();
	av_init_packet(adts_packet);
	FILE * fp = fopen(outfile, "wb");

	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == audioIndex)
			{
				AVFrame * frame = NULL;
				decode.DecodePacket(packet, &frame);
				if (frame != NULL)
				{
					AVPacket * pkt = NULL;
					encode.Encode(frame,&pkt);
					if (pkt != NULL)
					{
						TransformADTS(&adts, audioContext, pkt, &adts_packet);
						if (adts_packet != NULL)
						{
							LOGD("ADTS:%x %x %x %x %x %x %x  -  %x %x %x %x\n",
								adts_packet->data[0], adts_packet->data[1], adts_packet->data[2], 
								adts_packet->data[3], adts_packet->data[4], adts_packet->data[5], 
								adts_packet->data[6], adts_packet->data[7], adts_packet->data[8], 
								adts_packet->data[9], adts_packet->data[10]);
							printf("block size:%d\n", packet->size);
							fwrite(adts_packet->data, adts_packet->size, 1, fp);
							av_packet_unref(adts_packet);
						}

						av_packet_unref(pkt);
						av_packet_free(&pkt);
					}

					av_frame_unref(frame);
					av_frame_free(&frame);
				}
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}


	while (true)
	{
		AVFrame * frame = NULL;
		int ret = decode.DecodePacket(&frame);
		if (frame != NULL)
		{
			AVPacket * pkt = NULL;
			encode.Encode(frame, &pkt);
			if (pkt != NULL)
			{
				TransformADTS(&adts, audioContext, pkt, &adts_packet);
				if (adts_packet != NULL)
				{
					LOGD("ADTS:%x %x %x %x %x %x %x  -  %x %x %x %x\n",
						adts_packet->data[0], adts_packet->data[1], adts_packet->data[2],
						adts_packet->data[3], adts_packet->data[4], adts_packet->data[5],
						adts_packet->data[6], adts_packet->data[7], adts_packet->data[8],
						adts_packet->data[9], adts_packet->data[10]);
					printf("block size:%d\n", packet->size);
					fwrite(adts_packet->data, adts_packet->size, 1, fp);
					av_packet_unref(adts_packet);
				}

				av_packet_unref(pkt);
				av_packet_free(&pkt);
			}

			av_frame_unref(frame);
			av_frame_free(&frame);
		}
		if (ret <= 0)
		{
			break;
		}
	}

	while (true)
	{
		AVPacket * pkt = NULL;
		int ret = encode.Encode(&pkt);
		if (pkt != NULL)
		{
			TransformADTS(&adts, audioContext, pkt, &adts_packet);
			if (adts_packet != NULL)
			{
				LOGD("ADTS:%x %x %x %x %x %x %x  -  %x %x %x %x\n",
					adts_packet->data[0], adts_packet->data[1], adts_packet->data[2],
					adts_packet->data[3], adts_packet->data[4], adts_packet->data[5],
					adts_packet->data[6], adts_packet->data[7], adts_packet->data[8],
					adts_packet->data[9], adts_packet->data[10]);
				printf("block size:%d\n", packet->size);
				fwrite(adts_packet->data, adts_packet->size, 1, fp);
				av_packet_unref(adts_packet);
			}

			av_packet_unref(pkt);
			av_packet_free(&pkt);
		}
		if (ret <= 0)
		{
			break;
		}
	}
	

	av_packet_free(&packet);

	fclose(fp);

	avformat_close_input(&formatContext);
	return 0;
}

typedef struct _adts_header
{
	unsigned int syncword : 12;//同步字0xfff，说明一个ADTS帧的开始  
	unsigned char ID : 1;//ID比较奇怪,标准文档中是这么说的”MPEG identifier, set to ‘1’. See ISO/IEC 11172-3″ MPEG Version: 0 for MPEG-4，1 for MPEG-2
	unsigned char layer : 2;//一般设置为0  
	unsigned char protection_absent : 1;//是否误码校验,需为1，关闭CRC数据段
	unsigned char profile : 2;//表示使用哪个级别的AAC，如01 Low Complexity(LC)--- AACLC  
	unsigned char sampling_frequency_index : 4;//表示使用的采样率下标0x3 48k ,0x4 44.1k, 0x5 32k  
	unsigned char private_bit : 1;//一般设置为0  
	unsigned char channel_configuration : 3;// 表示声道数  
	unsigned char original_copy : 1;//一般设置为0  
	unsigned char home : 1;//一般设置为0  

	unsigned char copyright_identification_bit : 1;//一般设置为0  
	unsigned char copyright_identification_start : 1;//一般设置为0  
	unsigned int frame_length : 13;// 一个ADTS帧的长度包括ADTS头和raw data block  
	unsigned int adts_buffer_fullness : 11;// 0x7FF 说明是码率可变的码流  
	unsigned char number_of_raw_data_blocks_in_frame : 2;//表示ADTS帧中有number_of_raw_data_blocks_in_frame + 1个AAC原始帧.  
	//unsigned int CRC;	//16位，可选，protection_absent==0，需要此段
};

void printADTS(const char * infile)
{
	FILE * fp = fopen(infile, "rb");
	if (fp == NULL) return;

	fseek(fp,0,SEEK_END);
	int size = ftell(fp);
	fseek(fp,0,SEEK_SET);

	unsigned char* btData = (unsigned char*)malloc(size);
	int dwRead = fread(btData,1,size,fp);
	fclose(fp);

	int j = 0;
	int i = 0;
	int nAdtsHeader_Size = 9;//根据AAC adts协议头可知，协议大小为9个字节  
	while (i < dwRead - nAdtsHeader_Size) {
		if (btData[i] == 0xff && (btData[i + 1] >> 4) == 0xf) {//判断是aac的adts封装格式
			unsigned char* data = &btData[i];

			LOGD("ADTS:%02x %02x %02x %02x %02x %02x %02x  -  %02x %02x %02x %02x\n", data[0], data[1], data[2], data[3],
				data[4], data[5], data[6],
				data[7], data[8], data[9], data[10]);

			printf("%03d  %5d:  %02x  %x  %x  %x", j, i,
				(data[0] << 4) | (data[1] >> 4),
				(data[1] >> 3) & 0x1,
				(data[1] >> 1) & 0x3,
				data[1] & 0x1);

			switch ((data[2] >> 6) & 0x3) {
			case 0:
				printf("  Main");
				break;
			case 1:
				printf("  LC");
				break;
			case 2:
				printf("  SSR");
				break;
			default:
				printf("  unknown");
				break;
			}

			switch ((data[2] >> 2) & 0xF) {
			case 0:
				printf("  96000Hz");
				break;
			case 1:
				printf("  88200Hz");
				break;
			case 2:
				printf("  64000Hz");
				break;
			case 3:
				printf("  48000Hz");
				break;
			case 4:
				printf("  44100Hz");
				break;
			case 5:
				printf("  32000Hz");
				break;
			case 6:
				printf("  24000Hz");
				break;
			case 7:
				printf("  22050Hz");
				break;
			case 8:
				printf("  16000Hz");
				break;
			case 9:
				printf("  12000Hz");
				break;
			case 10:
				printf("  11025Hz");
				break;
			case 11:
				printf("  8000Hz");
				break;
			default:
				printf(" unknown");
				break;
			}
			printf(" %x  %x  %x  %x  %x  %x  %d  %02x  %x\n",
				(data[2] >> 1) & 0x1,
				((data[2] & 0x1) << 2) | ((data[3] >> 6) & 0x3),
				(data[3] >> 5) & 0x1,
				(data[3] >> 4) & 0x1,

				(data[3] >> 3) & 0x1,
				(data[3] >> 2) & 0x1,
				(((data[3]) & 0x3) << 11) | (data[4]) << 3 | ((data[5] >> 5) & 0x7),
				((data[5] & 0x1F) << 6) | ((data[6] >> 2) & 0x3F),
				data[6] & 0x3);

			j++;
			int audio_size = (((data[3]) & 0x3) << 11) | (data[4]) << 3 | ((data[5] >> 5) & 0x7);//偏移一个音频帧的大小 
			printf("block size:%d\n", audio_size);

			i += audio_size;
		}
		else {
			i++;
		}
	}

	free(btData);
}