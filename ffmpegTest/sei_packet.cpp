#include "sei_packet.h"
#include <stdio.h>
#include <string.h>

#define min(X,Y) ((X) < (Y) ? (X) : (Y))

#define UUID_SIZE 16

//FFMPEG uuid
//static unsigned char uuid[] = { 0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20, 0xd9, 0x23, 0xee, 0xef };
//self UUID
static unsigned char uuid[] = { 0x54, 0x80, 0x83, 0x97, 0xf0, 0x23, 0x47, 0x4b, 0xb7, 0xf7, 0x4f, 0x32, 0xb5, 0x4e, 0x06, 0xac };

//开始码
static unsigned char start_code[] = {0x00,0x00,0x00,0x01};

uint32_t reversebytes(uint32_t value) {
	return (value & 0x000000FFU) << 24 | (value & 0x0000FF00U) << 8 |
		(value & 0x00FF0000U) >> 8 | (value & 0xFF000000U) >> 24;
}

uint32_t get_sei_nalu_size(uint32_t content)
{
	//SEI payload size
	uint32_t sei_payload_size = content + UUID_SIZE;
	//NALU + payload类型 + 数据长度 + 数据
	uint32_t sei_size = 1 + 1 + (sei_payload_size / 0xFF + (sei_payload_size % 0xFF != 0 ? 1 : 0)) + sei_payload_size;
	//截止码
	uint32_t tail_size = 2;
	if (sei_size % 2 == 1)
	{
		tail_size -= 1;
	}
	sei_size += tail_size;

	return sei_size;
}

uint32_t get_sei_packet_size(uint32_t size)
{
	return get_sei_nalu_size(size) + 4;
}

int fill_sei_packet(unsigned char * packet,bool isAnnexb, const char * content, uint32_t size)
{
	unsigned char * data = (unsigned char*)packet;
	unsigned int nalu_size = (unsigned int)get_sei_nalu_size(size);
	uint32_t sei_size = nalu_size;
	//大端转小端
	nalu_size = reversebytes(nalu_size);

	//NALU开始码
	unsigned int * size_ptr = &nalu_size;
	if (isAnnexb)
	{
		memcpy(data, start_code, sizeof(unsigned int));
	}
	else
	{
		memcpy(data, size_ptr, sizeof(unsigned int));
	}
	data += sizeof(unsigned int);

	unsigned char * sei = data;
	//NAL header
	*data++ = 6; //SEI
	//sei payload type
	*data++ = 5; //unregister
	size_t sei_payload_size = size + UUID_SIZE;
	//数据长度
	while (true)
	{
		*data++ = (sei_payload_size >= 0xFF ? 0xFF : (char)sei_payload_size);
		if (sei_payload_size < 0xFF) break;
		sei_payload_size -= 0xFF;
	}

	//UUID
	memcpy(data, uuid, UUID_SIZE);
	data += UUID_SIZE;
	//数据
	memcpy(data, content, size);
	data += size;

	//tail 截止对齐码
	if (sei + sei_size - data == 1)
	{
		*data = 0x80;
	}
	else if (sei + sei_size - data == 2)
	{
		*data++ = 0x00;
		*data++ = 0x80;
	}

	return true;
}

int get_sei_buffer(unsigned char * data, uint32_t size, char * buffer, int *count)
{
	unsigned char * sei = data;
	int sei_type = 0;
	unsigned sei_size = 0;
	//payload type
	do {
		sei_type += *sei;
	} while (*sei++ == 255);
	//数据长度
	do {
		sei_size += *sei;
	} while (*sei++ == 255);

	//检查UUID
	if (sei_size >= UUID_SIZE && sei_size <= (data + size - sei) &&
		sei_type == 5 && memcmp(sei, uuid, UUID_SIZE) == 0)
	{
		sei += UUID_SIZE;
		sei_size -= UUID_SIZE;

		if (buffer != NULL && count != NULL)
		{
			if (*count > (int)sei_size)
			{
				memcpy(buffer, sei, sei_size);
			}
		}
		if (count != NULL)
		{
			*count = sei_size;
		}
		return sei_size;
	}
	return -1;
}

int get_sei_content(unsigned char * packet, uint32_t size,char * buffer,int *count)
{
	unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
	unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

	unsigned char *data = packet;
	bool isAnnexb = false;
	if ((size > 3 && memcmp(data, ANNEXB_CODE_LOW,3) == 0) ||
		(size > 4 && memcmp(data, ANNEXB_CODE,4) == 0)
		)
	{
		isAnnexb = true;
	}
	//暂时只处理MP4封装,annexb暂为处理
	if (isAnnexb)
	{
		while (data < packet + size) {
			if ((packet + size - data) > 4 && data[0] == 0x00 && data[1] == 0x00)
			{
				int startCodeSize = 2;
				if (data[2] == 0x01)
				{
					startCodeSize = 3;
				}
				else if(data[2] == 0x00 && data[3] == 0x01)
				{
					startCodeSize = 4;
				}
				if (startCodeSize == 3 || startCodeSize == 4)
				{
					if ((packet + size - data) > (startCodeSize + 1) && 
						(data[startCodeSize] & 0x1F) == 6)
					{
						//SEI
						unsigned char * sei = data + startCodeSize + 1;

						int ret = get_sei_buffer(sei, (packet + size - sei), buffer, count);
						if (ret != -1)
						{
							return ret;
						}
					}
					data += startCodeSize + 1;
				}
				else
				{
					data += startCodeSize + 1;
				}
			}
			else
			{
				data++;
			}
		}
	}
	else
	{
		//当前NALU
		while (data < packet + size) {
			//MP4格式起始码/长度
			unsigned int *length = (unsigned int *)data;
			int nalu_size = (int)reversebytes(*length);
			//NALU header
			if ((*(data + 4) & 0x1F) == 6)
			{
				//SEI
				unsigned char * sei = data + 4 + 1;

				int ret = get_sei_buffer(sei, min(nalu_size,(packet + size - sei)),buffer,count);
				if (ret != -1)
				{
					return ret;
				}
			}
			data += 4 + nalu_size;
		}
	}
	return -1;
}
