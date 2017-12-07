#pragma once

#include "EvoInterface/EvoVideoConvert.h"

inline uint64_t GetFileSize(FILE * fp)
{
	if (fp == NULL) return 0;
	uint64_t curr = ftell(fp);
	fseek(fp, 0, SEEK_END);
	uint64_t size = ftell(fp);
	fseek(fp,curr,SEEK_SET);
	return size;
}

inline uint64_t GetFileSize(const char * file)
{
	FILE *fp = NULL;
	fp = fopen(file, "rb");
	if (fp == NULL) return 0;
	fseek(fp, 0, SEEK_END);
	uint64_t size = ftell(fp);
	fclose(fp);
	return size;
}

int testConvert(const char * path,const char * opath)
{
	EvoVideoConvert convert;
	EvoVideoInfo info;
	info.Width = 3040;
	info.Height = 1520;
	info.Format = AV_PIX_FMT_NV12;
	EvoVideoInfo infoOut = info;
	infoOut.Format = AV_PIX_FMT_YUV420P;
	convert.Initialize(info, infoOut);

	AVFrame * SrcFrame = av_frame_alloc();
	uint32_t inSize = EvoVideoConvert::GetSize(info);
	uint8_t * nv12_data = (uint8_t*)malloc(inSize);
	av_image_fill_arrays(
		SrcFrame->data,
		SrcFrame->linesize,
		nv12_data,
		info.Format,
		info.Width,
		info.Height,
		1
		);

	uint32_t outSize = EvoVideoConvert::GetSize(infoOut);
	uint8_t * yuv_data = (uint8_t*)malloc(outSize);
	AVFrame * DesFrame = av_frame_alloc();
	av_image_fill_arrays(
		DesFrame->data,
		DesFrame->linesize,
		yuv_data,
		infoOut.Format,
		infoOut.Width,
		infoOut.Height,
		1
		);

	int index = 0;
	char buffer[255];
	char outBuffer[255];
	while (true)
	{
		sprintf(buffer, "%s/%d_frame.txt", path, index);
		sprintf(outBuffer, "%s/%d_frame.txt", opath, index);
		index++;
		FILE * fp = fopen(buffer,"rb");
		if (fp == NULL) break;
		uint64_t size = GetFileSize(fp);
		if (size != inSize) continue;
		fread(nv12_data,1,size,fp);
		fclose(fp);

		memset(yuv_data, 0, outSize);
		convert.Convert(SrcFrame, DesFrame);

		FILE * ofp = fopen(outBuffer, "wb");
		if (ofp == NULL) continue;
		fwrite(yuv_data,outSize,1,ofp);
		fclose(ofp);
	}
	av_frame_free(&SrcFrame);
	av_frame_free(&DesFrame);
	free(nv12_data);
	free(yuv_data);
	return 0;
}