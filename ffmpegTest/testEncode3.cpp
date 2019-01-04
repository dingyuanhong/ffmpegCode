#pragma once

#include "EvHeade.h"
#include "Encode.h"
#include "SEIEncode.h"
#include "exterlFunction.h"
#include "../EvoInterface/sei_packet.h"

#ifdef USE_NEW_API
static AVCodecContext *CreateCodecContent(AVCodecParameters *codecpar)
{
	AVCodecContext *codecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecContext, codecpar);
	return codecContext;
}
#endif

//通过NALU生成包
AVPacket *NALUToPacket(NALU * nalu,int count,bool mp4 = true) {
	//如果开始码长度不一致则需要调整内存长度
	bool bMemoryAdjust = false;
	int lastCodeSize = -1;
	uint32_t size = 0;
	for (int i = 0; i < count; i++) {
		size += nalu[i].size;
		if (nalu[i].codeSize < 4) {
			size += 1;
		}
		if (lastCodeSize == -1) {
			lastCodeSize = nalu[i].codeSize;
		}
		else if (lastCodeSize != nalu[i].codeSize) {
			bMemoryAdjust = true;
		}
	}

	//需调整内存长度
	if (bMemoryAdjust) {
		//重申请内存复制数据
		AVPacket * packet = av_packet_alloc();
		av_new_packet(packet, size);
		uint8_t * buffer = packet->data;
		for (int i = 0; i < count; i++) {
			*(uint32_t*)buffer = reversebytes(nalu[i].size - nalu[i].codeSize);
			buffer += 4;
			memcpy(buffer, nalu[i].data + nalu[i].index + nalu[i].codeSize, nalu[i].size - nalu[i].codeSize);
			buffer += nalu[i].size - nalu[i].codeSize;
		}
		return packet;
	}
	else {
		//直接调整NALU头数据
		if (lastCodeSize == 4) {
			for (int i = 0; i < count; i++) {
				if (mp4) {
					*(uint32_t*)(nalu[i].data + nalu[i].index) = reversebytes(nalu[i].size - nalu[i].codeSize);
				}
				else {
					uint8_t * buffer = nalu[i].data + nalu[i].index;
					buffer[0] = 0x00;
					buffer[1] = 0x00;
					buffer[2] = 0x00;
					buffer[3] = 0x01;
				}	
			}
		}
		else if (lastCodeSize == 3) {
			for (int i = 0; i < count; i++) {
				uint8_t * buffer = nalu[i].data + nalu[i].index;
				buffer[0] = 0x00;
				buffer[1] = 0x00;
				buffer[2] = 0x01;
			}
		}

		return NULL;
	}
}

int testEncode3(const char * infile, const char * outfile)
{
	av_register_all();

	AVFormatContext * formatContext = avOpenFile(infile);
	if (formatContext == NULL)
	{
		return -1;
	}
	int videoIndex = getStreamId(formatContext, AVMEDIA_TYPE_VIDEO);
	int audioIndex = getStreamId(formatContext, AVMEDIA_TYPE_AUDIO);

	if (videoIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}
	if (audioIndex == -1)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	Encode encode;
	int ret = encode.Open(outfile);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	AVStream * stream = formatContext->streams[videoIndex];

	if (videoIndex != -1)
	{
		encode.NewVideoStream(formatContext->streams[videoIndex]);
		//encode.NewVideoStream(stream->codec->width, stream->codec->height, stream->codec->pix_fmt,30);
	}
	if (audioIndex != -1)
	{
		encode.NewAudioStream(formatContext->streams[audioIndex]);
	}

	ret = encode.WriteHeader();
	if (ret != 0)
	{
		//写头失败
		return -1;
	}

	bool used = false;

	AVPacket * packet = av_packet_alloc();
	while (true)
	{
		ret = av_read_frame(formatContext, packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				uint8_t * imu = NULL;
				uint32_t size = 0;
				int imu_size = get_sei_content(packet->data,packet->size, IMU_UUID,&imu,&size);
				if (imu_size == -1) {
					uint8_t tmp[4];
					tmp[0] = packet->data[0];
					tmp[1] = packet->data[1];
					tmp[2] = packet->data[2];
					tmp[3] = packet->data[3];

					packet->data[0] = 0x00;
					packet->data[1] = 0x00;
					packet->data[2] = 0x00;
					packet->data[3] = 0x01;
					imu_size = get_sei_content(packet->data, packet->size, IMU_UUID, &imu, &size);
				}

				if (imu_size > 0) {
					NALU * nalu = NULL;
					int nalu_count = 0;
					//获取NALU单元
					get_content_nalu(packet->data, packet->size, &nalu, &nalu_count);
					if (nalu_count > 0) {
						//搜索IMU数据
						int sei_index = find_nalu_sei(nalu, nalu_count, IMU_UUID);
						if (sei_index != -1) {
							if (sei_index + 1 == nalu_count) {
								//切掉数据
								//memset(nalu[sei_index].data + nalu[sei_index].index,0, nalu[sei_index].size);
								//packet->size -= nalu[sei_index].size;
							}
						}

						//NALU单元生成数据
						AVPacket * pp = NALUToPacket(nalu, nalu_count);
						if (pp != NULL) {
							av_copy_packet_side_data(pp, packet);
							av_packet_copy_props(pp, packet);
							av_packet_free(&packet);
							packet = pp;
						}
						free(nalu);
					}
				}

				packet->pts = packet->dts;

				//packet->dts = packet->pts;
				encode.WriteVideo(packet);

				if (!used) {
					FILE * fp = fopen("../tmp.h264", "wb+");
					if (fp != NULL) {
						fwrite(packet->data, packet->size, 1, fp);
						fclose(fp);
					}
					used = true;
				}
			}
			else if (packet->stream_index == audioIndex)
			{
				encode.WriteAudio(packet);
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}

	encode.WriteTrailer();
	encode.Close();

	av_packet_free(&packet);

	avformat_close_input(&formatContext);
	return 0;
}