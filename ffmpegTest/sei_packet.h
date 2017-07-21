#ifndef SEI_PACKET_H
#define SEI_PACKET_H

#include <stdint.h>

uint32_t reversebytes(uint32_t value);

uint32_t get_sei_packet_size(uint32_t size);

int fill_sei_packet(unsigned char * packet, bool isAnnexb, const char * content, uint32_t size);

int get_sei_content(unsigned char * packet, uint32_t size, char * buffer, int *count);

#endif