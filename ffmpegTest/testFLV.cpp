#include "EvHeade.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "flv.h"

inline long fileSize(FILE * in)
{
	fseek(in, 0, SEEK_END);
	long size = ftell(in);
	fseek(in, 0, SEEK_SET);
	return size;
}

//char * path = "D:/Users/ee/Desktop/temp_frame_0724_1549/temp_frame_0724_1549";
inline int testFLV(const char * path)
{
	char strBuffer[256];
	sprintf(strBuffer, "%s/flv_frame.flv", path);

	FILE * fp = fopen(strBuffer, "wb");
	FLVHeader(fp, true, false);
	FLVTagBody(fp, 0);

	int timeStamp = 0;
	int index = 0;
	int ret = 0;
	while (true)
	{
		sprintf(strBuffer, "%s/%d_frame.txt", path, index++);
		FILE * in = fopen(strBuffer, "rb");
		if (in == NULL) break;
		long size = fileSize(in);
		unsigned char * mem = (unsigned char*)malloc(size);
		ret = fread(mem, 1, size, in);
		if ((mem[0] & 0xF) != 0x7)
		{
			printf("invalit index:%d\n", index - 1);
			free(mem);
			fclose(in);
			continue;
		}
		timeStamp += 33;
		FLVTagHeader(fp, 9, size, timeStamp);
		fwrite(mem, size, 1, fp);
		FLVTagBody(fp, size + 11);

		fclose(in);
	}
	fclose(fp);
	return ret;
}