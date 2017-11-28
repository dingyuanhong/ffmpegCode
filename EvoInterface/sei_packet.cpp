#include "sei_packet.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>

#define min(X,Y) ((X) < (Y) ? (X) : (Y))

//开始码
static unsigned char START_CODE[] = { 0x00,0x00,0x00,0x01 };
static unsigned char START_CODE_LOW[] = { 0x00,0x00,0x01 };

//大小端转换
uint32_t reversebytes(uint32_t value) {
	return (value & 0x000000FFU) << 24 | (value & 0x0000FF00U) << 8 |
		(value & 0x00FF0000U) >> 8 | (value & 0xFF000000U) >> 24;
}

//检查是否为标准H264
bool check_is_annexb(uint8_t * packet, int32_t size)
{
	unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
	unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

	unsigned char *data = packet;
	if (data == NULL) return false;
	bool isAnnexb = false;
	if ((size > 3 && memcmp(data, ANNEXB_CODE_LOW, 3) == 0) ||
		(size > 4 && memcmp(data, ANNEXB_CODE, 4) == 0)
		)
	{
		isAnnexb = true;
	}
	return isAnnexb;
}

//获取标准H264头类型
int get_annexb_size(uint8_t * packet, int32_t size)
{
	unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
	unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

	unsigned char *data = packet;
	if (data == NULL) return 0;
	if (size > 3 && memcmp(data, ANNEXB_CODE_LOW, 3) == 0)
	{
		return 3;
	}
	else if (size > 4 && memcmp(data, ANNEXB_CODE, 4) == 0)
	{
		return 4;
	}
	return 0;
}

//获取H264类型
int get_annexb_type(uint8_t * packet, int32_t size)
{
	unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
	unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

	unsigned char *data = packet;
	if (data == NULL) return 0;
	if (size > 3 && memcmp(data, ANNEXB_CODE_LOW, 3) == 0)
	{
		return 2;
	}
	else if (size > 4 && memcmp(data, ANNEXB_CODE, 4) == 0)
	{
		return 1;
	}
	return 0;
}

//搜索标准头
int32_t find_annexb(uint8_t * packet, int32_t size)
{
	uint8_t * data = packet;
	if (size <= 0) return -1;
	int32_t index = 0;
	while (size - index > 0)
	{
		if ((size - index > 3) && data[index] == 0x00 && data[index + 1] == 0x00)
		{
			if (data[index + 2] == 0x01)
			{
				return index;
			}
			else if ((size - index > 4) && data[index + 2] == 0x00 && data[index + 3] == 0x01)
			{
				return index;
			}
		}
		index += 1;
	}

	return -1;
}

//检测竞争字段
int32_t get_content_compete_size(const uint8_t *data, int32_t size)
{
	if (data == NULL) return 0;

	int zero_count = 0;
	int zero_prevention = 0;
	for (int32_t i = 0; i < size; i++)
	{
		if (zero_count >= 2)
		{
			zero_prevention++;
			zero_count = 0;
			continue;
		}
		if (data[i] == 0x00)
		{
			zero_count++;
		}
	}
	return size + zero_prevention;
}

//检测非竞争长度
int32_t get_content_uncompete_size(const uint8_t *data, int32_t size)
{
	if (data == NULL) return 0;

	int zero_count = 0;
	int uncompete_size = size;
	for (int32_t i = 0; i < size; i++)
	{
		if (zero_count >= 2)
		{
			if (data[i] == 0x03)
			{
				uncompete_size -= 1;
			}
			zero_count = 0;
		}
		else if (data[i] == 0x00)
		{
			zero_count++;
		}
	}
	return uncompete_size;
}

//获取SEI长度
int32_t get_sei_nalu_size(const uint8_t *data, int32_t size)
{
	int32_t content_size = get_content_compete_size(data, size);
	//计算空间的
	int32_t payload_size = size + UUID_SIZE;
	//SEI payload size
	int32_t sei_payload_size = content_size + UUID_SIZE;
	//NALU + payload类型 + 数据长度 + 数据
	int32_t sei_size = 1 + 1 + (payload_size / 0xFF + (payload_size % 0xFF != 0 ? 1 : 0)) + sei_payload_size;
	//截止码
	int32_t tail_size = 2;
	if (sei_size % 2 == 1)
	{
		tail_size -= 1;
	}
	sei_size += tail_size;

	return sei_size;
}

//获取sei包长度
int32_t get_sei_packet_size(const uint8_t *data, int32_t size, int annexbType)
{
	if (annexbType == 2)
	{
		return get_sei_nalu_size(data, size) + 3;
	}
	return get_sei_nalu_size(data, size) + 4;
}

//填充sei数据
int32_t fill_sei_packet(uint8_t * packet, int annexbType, const uint8_t *uuid, const uint8_t * data, int32_t size)
{
	if (packet == NULL)
	{
		return -1;
	}
	if (uuid == NULL)
	{
		return -1;
	}

	uint8_t * nalu_data = packet;
	int32_t nalu_size = get_sei_nalu_size(data, size);
	int32_t sei_size = nalu_size;
	//大端转小端
	nalu_size = reversebytes(nalu_size);

	//NALU开始码
	int32_t * size_ptr = &nalu_size;
	if (annexbType == 2)
	{
		memcpy(nalu_data, START_CODE_LOW, sizeof(unsigned char)*3);
		nalu_data += sizeof(unsigned char)*3;
	}
	else if (annexbType == 1)
	{
		memcpy(nalu_data, START_CODE, sizeof(unsigned int));
		nalu_data += sizeof(unsigned int);
	}
	else
	{
		memcpy(nalu_data, size_ptr, sizeof(unsigned int));
		nalu_data += sizeof(unsigned int);
	}

	uint8_t * sei_nalu = nalu_data;
	//NAL
	*nalu_data++ = 6; //SEI
					  //sei payload
	*nalu_data++ = 5; //unregister

	size_t sei_payload_size = size + UUID_SIZE;
	//数据长度
	while (true)
	{
		*nalu_data++ = (sei_payload_size >= 0xFF ? 0xFF : (char)sei_payload_size);
		if (sei_payload_size < 0xFF) break;
		sei_payload_size -= 0xFF;
	}

	//UUID
	memcpy(nalu_data, uuid, UUID_SIZE);
	nalu_data += UUID_SIZE;
	int32_t content_size = get_content_compete_size(data, size);
	//数据
	if (data != NULL)
	{
		if (content_size == size)
		{
			memcpy(nalu_data,data,size);
			nalu_data += size;
		}
		else
		{
			int zero_count = 0;
			for (uint32_t i = 0; i < size; i++)
			{
				if (zero_count >= 2)
				{
					*nalu_data++ = 0x03;
					*nalu_data++ = data[i];
					zero_count = 0;
				}
				else
				{
					*nalu_data++ = data[i];
					if (data[i] == 0x00)
					{
						zero_count++;
					}
				}
			}
		}
	}

	//tail 截止对齐码
	if (sei_nalu + sei_size - nalu_data == 1)
	{
		*nalu_data = 0x80;
	}
	else if (sei_nalu + sei_size - nalu_data == 2)
	{
		*nalu_data++ = 0x00;
		*nalu_data++ = 0x80;
	}

	return 0;
}

typedef struct sei_content {
	uint8_t * uuid;
	uint8_t * data;
	int32_t size;
	int32_t payload_size;
}sei_content;

//获取SEI内容
int get_sei_buffer(uint8_t * packet, int32_t size, sei_content &content)
{
	if (size <= 0) return -1;
	if (packet == NULL) return -1;

	unsigned char * sei = packet;
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
	if (sei_size >= UUID_SIZE && sei_size <= (packet + size - sei) &&
		sei_type == 5)
	{
		content.uuid = sei;

		sei += UUID_SIZE;
		sei_size -= UUID_SIZE;

		content.data = sei;
		content.size = packet + size - sei;
		content.payload_size = sei_size;

		return sei_size;
	}
	return -1;
}

//反解数据
void uncompete_content(const sei_content & content, uint8_t * data, int32_t size)
{
	if (size == content.size)
	{
		memcpy(data, content.data, size);
	}
	else
	{
		int zero_count = 0;
		int uncompete_size = size;
		for (int32_t i = 0; i < content.size; i++)
		{
			if (zero_count >= 2)
			{
				if (content.data[i] != 0x03)
				{
					*data++ = content.data[i];
					size--;
				}
				zero_count = 0;
			}
			else if (content.data[i] == 0x00)
			{
				zero_count++;
				*data++ = content.data[i];
				size--;
			}
			else
			{
				zero_count = 0;
				*data++ = content.data[i];
				size--;
			}
			if (size <= 0) break;
		}
	}
}

int get_annexb_sei_content(uint8_t * packet, int32_t size, const uint8_t *uuid, uint8_t ** pdata, int32_t *psize)
{
	uint8_t * data = packet;
	uint32_t data_size = size;

	uint8_t * nalu_element = NULL;
	int32_t nalu_element_size = 0;
	while (data < packet + size) {
		int32_t index = find_annexb(data, data_size);
		int32_t second_index = 0;
		if (index != -1)
		{
			int startCodeSize = get_annexb_size(data + index, data_size - index);
			second_index = find_annexb(data + index + startCodeSize, data_size - index - startCodeSize);
			if (second_index >= 0)
			{
				second_index += startCodeSize;
			}
		}

		if (index != -1)
		{
			if (second_index == -1)
			{
				second_index = data_size;
			}
			nalu_element = data + index;
			nalu_element_size = second_index;
			data += second_index;
			data_size -= second_index;
		}
		else
		{
			return -1;
		}
		if (nalu_element != NULL && nalu_element_size != 0)
		{
			if ((int32_t)(packet + size - nalu_element)  < nalu_element_size)
			{
				nalu_element_size = (int32_t)(packet + size - nalu_element);
			}

			int startCodeSize = get_annexb_size(nalu_element, nalu_element_size);
			if (startCodeSize == 0) continue;
			//SEI
			if ((nalu_element[startCodeSize] & 0x1F) == 6)
			{
				uint8_t * sei_data = nalu_element + startCodeSize + 1;
				int32_t sei_data_length = nalu_element_size - startCodeSize - 1;
				sei_content content = { 0 };
				int ret = get_sei_buffer(sei_data, sei_data_length, content);
				if (ret != -1)
				{
					if (memcmp(uuid, content.uuid, UUID_SIZE) == 0)
					{
						int32_t uncompete_size = get_content_uncompete_size(content.data, content.size);
						if (uncompete_size > 0 && pdata != NULL)
						{
							uncompete_size = min(content.payload_size, uncompete_size);
							uint8_t * outData = (uint8_t*)malloc(uncompete_size + 1);
							memset(outData, 0, uncompete_size + 1);
							uncompete_content(content, outData, uncompete_size);
							if (pdata != NULL)
							{
								*pdata = outData;
							}
							else
							{
								free(outData);
							}
						}
						if (psize != NULL) *psize = uncompete_size;
						return uncompete_size;
					}
				}
			}
		}
	}
	return -1;
}


int get_mp4_sei_content(uint8_t * packet, int32_t size, const uint8_t *uuid, uint8_t ** pdata, int32_t *psize)
{
	uint8_t * data = packet;
	//当前NALU
	while (data < packet + size) {
		//MP4格式起始码/长度
		unsigned int *nalu_length = (unsigned int *)data;
		int nalu_size = (int)reversebytes(*nalu_length);
		//NALU header
		if ((*(data + 4) & 0x1F) == 6)
		{
			//SEI
			uint8_t * sei_data = data + 4 + 1;
			int32_t sei_data_length = min(nalu_size, (packet + size - sei_data));
			sei_content content = { 0 };
			int ret = get_sei_buffer(sei_data, sei_data_length, content);
			if (ret != -1)
			{
				if (memcmp(uuid, content.uuid, UUID_SIZE) == 0)
				{
					int32_t uncompete_size = get_content_uncompete_size(content.data, content.size);
					if (uncompete_size > 0 && pdata != NULL)
					{
						uncompete_size = min(content.payload_size, uncompete_size);
						uint8_t * outData = (uint8_t*)malloc(uncompete_size + 1);
						memset(outData, 0, uncompete_size + 1);
						uncompete_content(content, outData, uncompete_size);
						if (pdata != NULL)
						{
							*pdata = outData;
						}
						else
						{
							free(outData);
						}
					}
					if (psize != NULL) *psize = uncompete_size;
					return uncompete_size;
				}
			}
		}
		data += 4 + nalu_size;
	}
	return -1;
}

int get_sei_content(uint8_t * packet, int32_t size, const uint8_t *uuid, uint8_t ** pdata, int32_t *psize)
{
	if (uuid == NULL) return -1;
	bool isAnnexb = check_is_annexb(packet, size);
	//暂时只处理MP4封装,annexb暂为处理
	if (isAnnexb)
	{
		return get_annexb_sei_content(packet,size,uuid,pdata,psize);
	}
	else
	{
		return get_mp4_sei_content(packet, size, uuid, pdata, psize);
	}
	return -1;
}

void free_sei_content(uint8_t**pdata)
{
	if (pdata == NULL) return;
	if (*pdata != NULL)
	{
		free(*pdata);
		*pdata = NULL;
	}
}