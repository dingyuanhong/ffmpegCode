#include "ImageFile.h"

#ifdef _WIN32

#include <Windows.h>

//保存BMP文件的函数  
void SaveAsBMP(AVFrame *pFrameRGB, int width, int height, int index, int bpp)
{
	char buf[5] = { 0 };
	BITMAPFILEHEADER bmpheader;
	BITMAPINFOHEADER bmpinfo;
	FILE *fp = NULL;

	char *filename = new char[255];
	CreateDirectoryA("tmp",NULL);
	//文件存放路径，根据自己的修改  
	sprintf_s(filename, 255, "%s%05d.bmp", "./tmp/", index);
	fopen_s(&fp, filename, "wb+");
	if (fp == NULL) {
		printf("open file failed!\n");
		return;
	}

	bmpheader.bfType = 0x4d42;
	bmpheader.bfReserved1 = 0;
	bmpheader.bfReserved2 = 0;
	bmpheader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
	bmpheader.bfSize = bmpheader.bfOffBits + width*height*bpp / 8;

	bmpinfo.biSize = sizeof(BITMAPINFOHEADER);
	bmpinfo.biWidth = width;
	bmpinfo.biHeight = height;
	bmpinfo.biPlanes = 1;
	bmpinfo.biBitCount = bpp;
	bmpinfo.biCompression = BI_RGB;
	bmpinfo.biSizeImage = (width*bpp + 31) / 32 * 4 * height;
	bmpinfo.biXPelsPerMeter = 100;
	bmpinfo.biYPelsPerMeter = 100;
	bmpinfo.biClrUsed = 0;
	bmpinfo.biClrImportant = 0;

	fwrite(&bmpheader, sizeof(bmpheader), 1, fp);
	fwrite(&bmpinfo, sizeof(bmpinfo), 1, fp);
	fwrite(pFrameRGB->data[0], width*height*bpp / 8, 1, fp);

	fclose(fp);
}

#endif