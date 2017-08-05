#include "EvHeade.h"
#include "sei_packet.h"
//#include "json\json.h"
//#pragma comment(lib,"lib_json.lib")
#include "Encode.h"
#include "SEIEncode.h"

AVFormatContext * av_open_file(const char * file)
{
	AVFormatContext * formatContext = avformat_alloc_context();

	int ret = avformat_open_input(&formatContext, "../sp.mp4", NULL, NULL);
	if (ret != 0) {
		avformat_close_input(&formatContext);
		return NULL;
	}

	ret = avformat_find_stream_info(formatContext, NULL);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return NULL;
	}

	return formatContext;
}

int resetPacket(AVPacket * packet, AVPacket * pkt)
{
	//生成自定义数据
	char buffer[128];
	sprintf(buffer,"pts:%lld dts:%lld",packet->pts,packet->dts);
	int size = strlen(buffer);
	char * data = buffer;
	//获取自定义数据长度
	size_t sei_packet_size = get_sei_packet_size(size);

	av_new_packet(pkt, packet->size + (int)sei_packet_size);
	memset(pkt->data, 0, packet->size + sei_packet_size);
	pkt->pts = packet->pts;
	pkt->dts = packet->dts;

	bool addInHead = true;
	if (addInHead)
	{
		//填充原始数据
		memcpy(pkt->data + sei_packet_size, packet->data, packet->size);
		//填充自定义数据
		unsigned char * sei = (unsigned char*)pkt->data;
		fill_sei_packet(sei, false, data, size);
	}
	else
	{
		//填充原始数据
		memcpy(pkt->data, packet->data, packet->size);

		//填充自定义数据
		unsigned char * sei = (unsigned char*)pkt->data + packet->size;
		fill_sei_packet(sei, false, data, size);
	}
	
	return 0;
}

//#define NEW_API

int decodeVideo(AVCodecContext * context, AVPacket * packet, AVFrame * frame)
{
#ifdef NEW_API
	int ret = avcodec_send_packet(context, packet);
	if (ret == 0)ret = avcodec_receive_frame(context, frame);
#else
	int got_picture_ptr = 0;
	int ret = avcodec_decode_video2(context,frame,&got_picture_ptr,packet);
#endif
	if (got_picture_ptr > 0) {
		printf("decode:%lld %d %d\n", packet->pts, frame->width, frame->height);
		return 1;
	}
	else
	{
		if (ret < 0)
		{
			char str[1024];
			av_make_error_string(str, 1024, ret);
			printf("decodeVideo Error:%d %s\n", ret, str);
		}
		else
		{
			return 0;
		}
	}
	return ret;
}

int getVideoId(AVFormatContext * formatContext)
{
	int videoIndex = -1;
	for (size_t i = 0; i < formatContext->nb_streams; i++)
	{
#ifdef NEW_API
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
#else
		if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#endif
		{
			videoIndex = (int)i;
			break;
		}
	}
	return videoIndex;
}

AVCodecContext * openCodec(AVStream * stream)
{
#ifdef NEW_API
	AVCodecParameters * paramter = formatContext->streams[videoIndex]->codecpar;

	AVCodec * codec = avcodec_find_decoder(paramter->codec_id);

	AVCodecContext * codecContext = avcodec_alloc_context3(codec);

	avcodec_parameters_to_context(codecContext, paramter);
#else
	AVCodec * codec = avcodec_find_decoder(stream->codec->codec_id);
	AVCodecContext * codecContext = stream->codec;
#endif
	int ret = avcodec_open2(codecContext, codec, NULL);
	if (ret != 0) {
		avcodec_close(codecContext);
		avcodec_free_context(&codecContext);
		return NULL;
	}

	return codecContext;
}

#ifndef NEW_API
inline AVPacket * av_packet_alloc()
{
	AVPacket * packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);
	return packet;
}

inline void av_packet_free(AVPacket **ppacket)
{
	if (ppacket == NULL) return;
	AVPacket *packet = *ppacket;
	if (packet == NULL) return;
	av_free_packet(packet);
	av_free(packet);
	*ppacket = NULL;
}
#endif

int testFFmpeg()
{
	av_register_all();

	AVFormatContext * formatContext = avformat_alloc_context();

	int ret = avformat_open_input(&formatContext, "../1.mp4", NULL, NULL);
	if (ret != 0) {
		avformat_close_input(&formatContext);
		return -1;
	}

	ret = avformat_find_stream_info(formatContext, NULL);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	int videoIndex = getVideoId(formatContext);
	
	if (videoIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVCodecContext * codecContext = openCodec(formatContext->streams[videoIndex]);
	if (codecContext == NULL)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVPacket * packet = av_packet_alloc();
	AVPacket * pkt = av_packet_alloc();
	AVFrame * frame = av_frame_alloc();

	while (true)
	{
		ret = av_read_frame(formatContext,packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				//修改包裹
				//resetPacket(packet, pkt);
				//解码
				ret = decodeVideo(codecContext, packet, frame);
				if (ret == 1)
				{
					av_frame_unref(frame);
				}
				//取出自定义数据
				char selfPacket[255] = {0};
				int count = 255;
				ret = get_sei_content(packet->data, packet->size, selfPacket,&count);
				if (ret > 0)
				{
					printf("sei: %s\n", selfPacket);
				}
				
				av_packet_unref(pkt);
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}


	av_frame_free(&frame);
	av_packet_free(&packet);
	av_packet_free(&pkt);

	avcodec_close(codecContext);
#ifdef NEW_API
	avcodec_free_context(&codecContext);
#endif
	avformat_close_input(&formatContext);
	return 0;
}

int testEncode()
{
	int in_w = 480, in_h = 272;                              //Input data's width and height  


	SEIEncode encode;
	int ret = encode.Open("../1.mp4");
	if (ret != 0)
	{
		return -1;
	}
	ret = encode.NewVideoStream(in_w, in_h, AV_PIX_FMT_YUV420P,10);
	if (ret != 0)
	{
		return -1;
	}
	encode.WriteHeader();

	//FILE *in_file = fopen("src01_480x272.yuv", "rb"); //Input raw YUV data   
	FILE *in_file = fopen("../ds_480x272.yuv", "rb");   //Input raw YUV data  
	
	int framenum = 100;                                   //Frames to encode  

	AVStream * stream = encode.GetVideoStream();
	AVCodecContext * codecContext = encode.GetVideoStream()->codec;

	AVFrame * frame = av_frame_alloc();
	int picture_size = avpicture_get_size(codecContext->pix_fmt, codecContext->width, codecContext->height);
	uint8_t * picture_buf = (uint8_t *)av_malloc(picture_size);
	avpicture_fill((AVPicture *)frame, picture_buf, codecContext->pix_fmt, codecContext->width, codecContext->height);
	int y_size = codecContext->width * codecContext->height;

	for (int i = 0; i < framenum; i++)
	{
		//Read raw YUV data  
		if (fread(picture_buf, 1, y_size * 3 / 2, in_file) <= 0) {
			printf("Failed to read raw data! \n");
			break;
		}
		else if (feof(in_file)) {
			break;
		}

		frame->data[0] = picture_buf;					// Y  
		frame->data[1] = picture_buf + y_size;			// U   
		frame->data[2] = picture_buf + y_size * 5 / 4;  // V  
		frame->width = codecContext->width;
		frame->height = codecContext->height;
		frame->format = codecContext->pix_fmt;
		//PTS  
		//pFrame->pts=i;
		frame->pts = i*(stream->time_base.den) / ((stream->time_base.num) * codecContext->time_base.den);

		encode.EncodeVideo(frame);
	}

	encode.FlushVideo();

	encode.WriteTrailer();
	encode.Close();


	av_free(picture_buf);
	picture_buf = NULL;
	av_frame_free(&frame);
	return  0;
}

#include <string>

int i = 0;
int laifeng_read_cb(void *opaque, uint8_t *buf, int buf_size)
{
	char * path = (char*)opaque;
	char strBuffer[256];
	int ret = 0;
	while (true)
	{
		sprintf(strBuffer,"%s/%d_frame.txt", path,i++);
		FILE * fp = fopen(strBuffer,"rb");
		if (fp == NULL) return 0;
		fseek(fp,0,SEEK_END);
		int size = ftell(fp);
		fseek(fp,0,SEEK_SET);
		unsigned char * mem = (unsigned char*)malloc(size);

		ret = fread(mem, 1, size, fp);
		if ((mem[0] & 0xF) != 0x7)
		{
			free(mem);
			fclose(fp);
			continue;
		}

		int index = 0;
		/*
		FrameType : 4B
		CodecID   : 4B
		*/
		unsigned char * buffer = (unsigned char*)mem;
		int FrameType = (buffer[0] & 0xF0)>4;
		int CodecID = buffer[0] & 0x0F;
		buffer++;
		/*
		AVCPacketType :8B    0:sequence header 1:NALU 2:NALU end
		CompositionTime :24
		*/
		int AVCPacketType = buffer[0];
		buffer++;
		buffer += 3;

		if (AVCPacketType == 0)
		{
			int configurationVersion = buffer[0]; buffer++;
			unsigned char sps[3];
			sps[0] = buffer[0]; buffer++;
			sps[1] = buffer[0]; buffer++;
			sps[2] = buffer[0]; buffer++;
			int lengthSizeMinusOne = buffer[0] & 0x3; buffer++;
			int numSPS = buffer[0] & 0x1F; buffer++;
			unsigned int spsSize = (buffer[0] << 8) + buffer[1];
			buffer += 2;

			buf[index++] = 0x00;
			buf[index++] = 0x00;
			buf[index++] = 0x00;
			buf[index++] = 0x01;
			//sps
			memcpy(buf + index, buffer, spsSize);
			index += spsSize;
			buffer += spsSize;
			//pps
			int numPPS = buffer[0]; buffer++;
			unsigned int ppsSize = (buffer[0] << 8) + buffer[1];
			buffer += 2;

			buf[index++] = 0x00;
			buf[index++] = 0x00;
			buf[index++] = 0x00;
			buf[index++] = 0x01;
			memcpy(buf + index, buffer, ppsSize);
			index += ppsSize;
			buffer += ppsSize;
		}
		else if(AVCPacketType == 1)
		{
			while (buffer < mem + size)
			{
				uint32_t len = buffer[0]; buffer++;
				len = (len << 8) + buffer[0]; buffer++;
				len = (len << 8) + buffer[0]; buffer++;
				len = (len << 8) + buffer[0]; buffer++;

				if (buffer + len > mem + size)
				{
					break;
				}

				buf[index++] = 0x00;
				buf[index++] = 0x00;
				buf[index++] = 0x00;
				buf[index++] = 0x01;
				memcpy(buf + index, buffer, len);
				index += len;
				buffer += len;
			}
			ret = index;
		}
		else
		{
			free(mem);
			fclose(fp);

			break;
		}

		free(mem);
		fclose(fp);
		break;
	}
	return ret;
}

char * YUVFrame(AVFrame * frame)
{
	if (frame->format != 0) return NULL;

	char* buf = new char[frame->height * frame->width * 3 / 2];
	memset(buf, 0, frame->height * frame->width * 3 / 2);

	int height = frame->height;
	int width = frame->width;

	int index = 0, i;
	for (i = 0; i<height; i++)
	{
		memcpy(buf + index, frame->data[0] + i * frame->linesize[0], width);
		index += width;
	}
	for (i = 0; i<height / 2; i++)
	{
		memcpy(buf + index, frame->data[1] + i * frame->linesize[1], width / 2);
		index += width / 2;
	}
	for (i = 0; i<height / 2; i++)
	{
		memcpy(buf + index, frame->data[2] + i * frame->linesize[2], width / 2);
		index += width / 2;
	}
	
	return buf;
}

void WriteYUV420(FILE * fp, AVFrame * frame)
{
	if (frame->format != 0) return;
	//Y
	uint8_t * buffer = frame->data[0];
	for (int i = 0; i < frame->height; i++)
	{
		fwrite(buffer,1, frame->width, fp);
		buffer += frame->linesize[0];
	}
	
	//U
	buffer = frame->data[1];
	for (int i = 0; i < frame->height/2; i++)
	{
		fwrite(buffer, 1, frame->width/2, fp);
		buffer += frame->linesize[1];
	}
	//V
	buffer = frame->data[2];
	for (int i = 0; i < frame->height / 2; i++)
	{
		fwrite(buffer, 1, frame->width / 2, fp);
		buffer += frame->linesize[2];
	}
}

int testFFmpegIO()
{
	av_register_all();
	//char * path = "D:/Users/ee/Desktop/temp_frame_0724_1549/temp_frame_0724_1549";
	char * path = "D:/Users/ee/Desktop/temp_frame_072/temp_frame_0724_1549";
	int io_size = 0x1FFFF;
	unsigned char * IOBuffer = (unsigned char *)av_malloc(io_size);
	AVIOContext *avioCtx = avio_alloc_context(IOBuffer, io_size, 0, path, laifeng_read_cb, NULL, NULL);
	if (NULL == avioCtx)
	{
		printf("create avio_alloc_context error !!\n");
		return -1;
	}

	AVFormatContext * formatContext = avformat_alloc_context();
	formatContext->pb = avioCtx;
	formatContext->flags = AVFMT_FLAG_CUSTOM_IO;

	int ret = avformat_open_input(&formatContext, NULL, NULL, NULL);
	if (ret != 0) {
		char err[256];
		av_make_error_string(err, 256, ret);
		avformat_close_input(&formatContext);
		return -1;
	}

	ret = avformat_find_stream_info(formatContext, NULL);
	if (ret != 0)
	{
		char err[256];
		av_make_error_string(err,256,ret);
		avformat_close_input(&formatContext);
		return -1;
	}

	int videoIndex = getVideoId(formatContext);
	if (videoIndex == -1)
	{

		avformat_close_input(&formatContext);
		return -1;
	}
	AVCodecContext * codecContext = openCodec(formatContext->streams[videoIndex]);
	if (codecContext == NULL)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVPacket * packet = av_packet_alloc();
	AVPacket * pkt = av_packet_alloc();
	AVFrame * frame = av_frame_alloc();
	int index = 0;
	char strBuffer[255];
	FILE * fp = NULL;

	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				//修改包裹
				//resetPacket(packet, pkt);
				//解码
				ret = decodeVideo(codecContext, packet, frame);
				if (ret == 1)
				{
					if (fp == NULL)
					{
						sprintf(strBuffer, "../yuv/yuv%d__%d_%d.yuv",index++, frame->width, frame->height);
						fp = fopen(strBuffer, "wb");
					}
					char * buff = YUVFrame(frame);
					int len = frame->width * frame->height * 3 / 2;
					if (buff != NULL)
					{
						fwrite(buff, len, 1, fp);
						delete[] buff;
					}
					//单独写回存在数据丢失问题
					//WriteYUV420(fp,frame);
					fclose(fp);
					fp = NULL;
					av_frame_unref(frame);
				}
				av_packet_unref(pkt);
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}
	if(fp != NULL) fclose(fp);

	av_frame_free(&frame);
	av_packet_free(&packet);
	av_packet_free(&pkt);

	avcodec_close(codecContext);
#ifdef NEW_API
	avcodec_free_context(&codecContext);
#endif
	avformat_close_input(&formatContext);
	return 0;
}

#include "flv.h"

long fileSize(FILE * in)
{
	fseek(in, 0, SEEK_END);
	long size = ftell(in);
	fseek(in, 0, SEEK_SET);
	return size;
}

#include "libavutil/time.h"

int testFLV()
{
	char * path = "D:/Users/ee/Desktop/temp_frame_0724_1549/temp_frame_0724_1549";
	
	char strBuffer[256];
	sprintf(strBuffer, "%s/flv_frame.flv", path);

	FILE * fp = fopen(strBuffer, "wb");
	FLVHeader(fp,true,false);
	FLVTagBody(fp,0);

	int timeStamp = 0;
	int index = 0;
	int ret = 0;
	while (true)
	{
		sprintf(strBuffer, "%s/%d_frame.txt", path,index++);
		FILE * in = fopen(strBuffer, "rb");
		if (in == NULL) break;
		long size = fileSize(in);
		unsigned char * mem = (unsigned char*)malloc(size);
		ret = fread(mem, 1, size, in);
		if ((mem[0] & 0xF) != 0x7)
		{
			printf("invalit index:%d\n",index - 1);
			free(mem);
			fclose(in);
			continue;
		}
		timeStamp += 33;
		FLVTagHeader(fp, 9, size, timeStamp);
		fwrite(mem,size,1,fp);
		FLVTagBody(fp, size + 11);

		fclose(in);
	}
	fclose(fp);
	return ret;
}

int main()
{
	//return testFLV();
	return testFFmpegIO();
	//return testEncode();
	return testFFmpeg();
}