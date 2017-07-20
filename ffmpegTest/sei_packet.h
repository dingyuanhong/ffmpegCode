#pragma once

unsigned int reversebytes(unsigned int value);

size_t get_sei_packet_size(size_t size);

int fill_sei_packet(unsigned char * packet, bool isAnnexb, const char * content, size_t size);

int get_sei_content(unsigned char * packet, size_t size, char * buffer, int *count);