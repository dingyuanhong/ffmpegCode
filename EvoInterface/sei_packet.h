#pragma once
#ifndef SEI_PACKET_H
#define SEI_PACKET_H

#include <stdint.h>

//FFMPEG��UUID
//static unsigned char FFMPEG_UUID[] = { 0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48, 0xb7, 0x96, 0x2c, 0xd8, 0x20, 0xd9, 0x23, 0xee, 0xef };
//ʱ���UUID
static uint8_t TIME_STAMP_UUID[] = { 0x54, 0x80, 0x83, 0x97, 0xf0, 0x23, 0x47, 0x4b, 0xb7, 0xf7, 0x4f, 0x32, 0xb5, 0x4e, 0x06, 0xac };
//IMU����UUID
static uint8_t IMU_UUID[] = { 0x94,0x51,0xef,0x8f, 0xd2,0x41, 0x49,0x6a, 0x80, 0xba, 0x68, 0x18, 0xe2, 0x4d, 0xc0, 0x4e };

#define UUID_SIZE 16

//��С��ת��
uint32_t reversebytes(uint32_t value);

//����Ƿ�Ϊ��׼H264
uint32_t check_is_annexb(uint8_t * packet, uint32_t size);

//��ȡH264����
uint32_t get_annexb_type(uint8_t * packet, uint32_t size);

uint32_t get_annexb_size(uint8_t * packet, uint32_t size);

//��ȡsei������
uint32_t get_sei_packet_size(const uint8_t *data, uint32_t size, uint32_t annexbType);

//���sei����
int32_t fill_sei_packet(uint8_t * packet, uint32_t annexbType, const uint8_t *uuid, const uint8_t * content, uint32_t size);

//��ȡ��׼H264 sei����
int32_t get_annexb_sei_content(uint8_t * packet, uint32_t size, const uint8_t *uuid, uint8_t ** pdata, uint32_t *psize);

//��ȡ�Ǳ�׼H264 sei����
int32_t get_mp4_sei_content(uint8_t * packet, uint32_t size, const uint8_t *uuid, uint8_t ** pdata, uint32_t *psize);

//��ȡsei����
int32_t get_sei_content(uint8_t * packet, uint32_t size, const uint8_t *uuid, uint8_t ** pdata, uint32_t *psize);

//�ͷ�sei���ݻ�����
void free_sei_content(uint8_t**pdata);

int32_t find_annexb(uint8_t * packet, uint32_t size);

#endif
