#include "flv.h"
#ifdef _WIN32
	#include <Winsock2.h>
	#pragma comment(lib,"ws2_32.lib")
#else
	#include <arpa/inet.h>
#endif

void FLVHeader(FILE * fp, bool hasVideo, bool hasAudio,uint8_t *data,uint32_t len)
{
	fwrite("FLV",3,1,fp);
	uint8_t version = 0x01;
	fwrite(&version, 1, 1, fp);
	uint8_t typeFlagsAudio = hasAudio ? 0x4 : 0;
	uint8_t typeFlagsVideo = hasVideo ? 0x1 : 0;
	uint8_t typeFlags = typeFlagsAudio | typeFlagsVideo;
	fwrite(&typeFlags, 1, 1, fp);
	uint32_t DataOffset = 9;
	if (data != NULL && len > 0)
	{
		DataOffset += len;
	}
	DataOffset = htonl(DataOffset);
	fwrite(&DataOffset,4,1,fp);

	if (data != NULL && len > 0)
	{
		fwrite(data, len, 1, fp);
	}
}

void FLVTagBody(FILE * fp, uint32_t previousTagSize)
{
	previousTagSize = htonl(previousTagSize);
	fwrite(&previousTagSize, 4, 1, fp);
}

/*
type: 8 :Audio 9: Video 18 : script
*/
void FLVTagHeader(FILE * fp, uint8_t type,uint32_t dataSize,uint32_t timestamp,uint32_t streamID)
{
	int sizeAndType = (dataSize & 0x00FFFFFF) | ((type & 0x1F) << 24);
	sizeAndType = htonl(sizeAndType);
	fwrite(&sizeAndType,4,1,fp);
	//uint8_t TimestampExtended = ((timestamp >> 24) & 0x000000FF);
	//uint32_t time = ((timeStamp << 8) & 0xFFFFFF00);
	int time = ((timestamp << 8) & 0xFFFFFF00) | ((timestamp >> 24) & 0x000000FF);
	time = htonl(time);
	fwrite(&time, 4, 1, fp);
	//fwrite(&time, 3, 1, fp);
	//fwrite(&TimestampExtended,1,1,fp);
	fwrite(&streamID, 3, 1, fp);
}
