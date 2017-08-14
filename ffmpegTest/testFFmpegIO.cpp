#include "EvHeade.h"
#include "exterlFunction.h"
#include <string>

static int i = 0;
static int laifeng_read_cb(void *opaque, uint8_t *buf, int buf_size)
{
	char * path = (char*)opaque;
	char strBuffer[256];
	int ret = 0;
	while (true)
	{
		sprintf(strBuffer, "%s/%d_frame.txt", path, i++);
		FILE * fp = fopen(strBuffer, "rb");
		if (fp == NULL) return 0;
		fseek(fp, 0, SEEK_END);
		int size = ftell(fp);
		fseek(fp, 0, SEEK_SET);
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
		else if (AVCPacketType == 1)
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

inline char * YUVFrame(AVFrame * frame)
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

inline void WriteYUV420(FILE * fp, AVFrame * frame)
{
	if (frame->format != 0) return;
	//Y
	uint8_t * buffer = frame->data[0];
	for (int i = 0; i < frame->height; i++)
	{
		fwrite(buffer, 1, frame->width, fp);
		buffer += frame->linesize[0];
	}

	//U
	buffer = frame->data[1];
	for (int i = 0; i < frame->height / 2; i++)
	{
		fwrite(buffer, 1, frame->width / 2, fp);
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

//char * path = "D:/Users/ee/Desktop/temp_frame_0724_1549/temp_frame_0724_1549";
//char * path = "D:/Users/ee/Desktop/temp_frame_072/temp_frame_0724_1549";
inline int testFFmpegIO(const char * path)
{
	av_register_all();
	
	int io_size = 0x1FFFF;
	unsigned char * IOBuffer = (unsigned char *)av_malloc(io_size);
	AVIOContext *avioCtx = avio_alloc_context(IOBuffer, io_size, 0, (void*)path, laifeng_read_cb, NULL, NULL);
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
		av_make_error_string(err, 256, ret);
		avformat_close_input(&formatContext);
		return -1;
	}

	int videoIndex = getStreamId(formatContext);
	if (videoIndex == -1)
	{

		avformat_close_input(&formatContext);
		return -1;
	}
	AVCodecContext * codecContext = openCodecContext(formatContext->streams[videoIndex]);
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
						sprintf(strBuffer, "../yuv/yuv%d__%d_%d.yuv", index++, frame->width, frame->height);
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
	if (fp != NULL) fclose(fp);

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