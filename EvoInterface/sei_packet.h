#pragma once
#ifndef SEI_PACKET_H
#define SEI_PACKET_H

#include <stdint.h>

//FFMPEG的UUID
//static unsigned char FFMPEG_UUID[] = { 0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20, 0xd9, 0x23, 0xee, 0xef };
//时间戳UUID
static uint8_t TIME_STAMP_UUID[] = { 0x54, 0x80, 0x83, 0x97, 0xf0, 0x23, 0x47, 0x4b, 0xb7, 0xf7, 0x4f, 0x32, 0xb5, 0x4e, 0x06, 0xac };
//IMU数据UUID
static uint8_t IMU_UUID[] = { 0x94,0x51,0xef,0x8f, 0xd2,0x41, 0x49,0x6a, 0x80, 0xba, 0x68, 0x18, 0xe2, 0x4d, 0xc0, 0x4e };

#define UUID_SIZE 16

//大小端转换
uint32_t reversebytes(uint32_t value);

//检查是否为标准H264
uint32_t check_is_annexb(uint8_t * packet, uint32_t size);

//获取H264类型
uint32_t get_annexb_type(uint8_t * packet, uint32_t size);

uint32_t get_annexb_size(uint8_t * packet, uint32_t size);

//获取sei包长度
uint32_t get_sei_packet_size(const uint8_t *data, uint32_t size, uint32_t annexbType);

//填充sei数据
int32_t fill_sei_packet(uint8_t * packet, uint32_t annexbType, const uint8_t *uuid, const uint8_t * content, uint32_t size);

//获取标准H264 sei内容
int32_t get_annexb_sei_content(uint8_t * packet, uint32_t size, const uint8_t *uuid, uint8_t ** pdata, uint32_t *psize);

//获取非标准H264 sei内容
int32_t get_mp4_sei_content(uint8_t * packet, uint32_t size, const uint8_t *uuid, uint8_t ** pdata, uint32_t *psize);

//获取sei内容
int32_t get_sei_content(uint8_t * packet, uint32_t size, const uint8_t *uuid, uint8_t ** pdata, uint32_t *psize);

typedef struct NALU {
	uint8_t * data;		//内存起始
	uint32_t index;		//偏移
	uint32_t size;		//大小
	uint32_t codeSize;  //标志头大小
	uint8_t type;		//类型
}NALU;

//获取sei位置及大小
int32_t get_content_nalu(uint8_t * packet, uint32_t size, NALU **nalu, int32_t * count);

int32_t find_nalu_sei(NALU * nalu, int count, const uint8_t *uuid);

//释放sei内容缓冲区
void free_sei_content(uint8_t**pdata);

int32_t find_annexb(uint8_t * packet, uint32_t size);

//调整packet内存内容
uint32_t adjust_content_imu(uint8_t * packet, uint32_t size);

#endif
