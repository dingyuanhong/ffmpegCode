#pragma once

#include <stdio.h>
#include <stdint.h>

void FLVHeader(FILE * fp, bool hasVideo, bool hasAudio, uint8_t *data = NULL, uint32_t len = 0);

/*
previousTagSize = dataSize + 11
*/
void FLVTagBody(FILE * fp, uint32_t previousTagSize);

/*
type: 8 :Audio 9: Video 18 : script
*/
void FLVTagHeader(FILE * fp, uint8_t type, uint32_t dataSize, uint32_t timestamp = 0, uint32_t streamID = 0);