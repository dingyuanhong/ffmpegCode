#pragma once

#include "EvHeade.h"
#include "sei_packet.h"
#include "Encode.h"
#include "SEIEncode.h"

//file = "../1.mp4"
inline int testEncode(const char * file)
{
	int in_w = 480, in_h = 272;                              //Input data's width and height  


	SEIEncode encode;
	int ret = encode.Open(file);
	if (ret != 0)
	{
		return -1;
	}
	ret = encode.NewVideoStream(in_w, in_h, AV_PIX_FMT_YUV420P, 10);
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