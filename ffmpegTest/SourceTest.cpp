
//#include "testffmpeg.cpp"
//#include "testFFmpegIO.cpp"
//#include "testEncode.cpp"
//#include "testFLV.cpp"
//#include "testMeidaControl.cpp"
//#include "testEncode2.cpp"
//#include "testConvert.cpp"
//#include "testHead.cpp"
//#include "testDecode.cpp"
//#include "testDecodePath.cpp"
//#include "testDecodeSEI.cpp"
//#include "testEncode.cpp"
//#include "testInterface.cpp"
//#include "testMP4Encode.cpp"
//#include "testEncode3.cpp"
#include <string>

int testConvert(const char * path, const char * opath);
int testDecode(const char * file);
int testDecodePath(std::string path);
int testDecodeSEI(const char * file);
int testEncode(const char * infile, const char * outfile);
int testEncode2(const char * infile, const char * outfile);
int testFFmpeg(const char * file);
int testFFmpegIO(const char * path);
int testFLV(const char * path);
int TestHeader(const char *file);
int TestInterface(const char * file);
int testMediaControl(const char * file);
int testMP4Encode(const char * file, const char * outFile);
int testEncode3(const char * infile, const char * outfile);
int testADTS(const char * infile, const char * outfile);
int decodeAAC(const char * infile, const char * outfile);
void printADTS(const char * infile);
int processAAC(const char * infile, const char * outfile);
int getExtraData(const char * file);
//aac≤‚ ‘
int simplest_aac_parser(char *url);

bool EVOReconductanceDone(const char *url, const char * outUrl);

#ifdef _WIN32
#pragma comment(lib,"EvoInterface.lib")
#define strcasecmp _stricmp
#else 
#ifndef _LINUX
#include <strings.h>
#endif
#endif

#include <stdlib.h>
#include "EvoInterface/sei_packet.h"

int testIMU()
{
	float imu[9] = {
		0.999047,
		0.031128,
		0.030595,
		-0.031389, 
		0.999474,
		0.008097,
		-0.030327,
		-0.009049, 
		0.999499
	};

	uint32_t annexbType = 1;
	uint32_t len = get_sei_packet_size((const uint8_t*)imu,sizeof(float)*9, annexbType);
	uint8_t * buffer = (uint8_t*)malloc(len);

	fill_sei_packet(buffer, annexbType,IMU_UUID, (const uint8_t*)imu, sizeof(float) * 9);

	uint8_t * data = NULL;
	uint32_t size = 0;
	get_sei_content(buffer,len,IMU_UUID,&data,&size);
	if (data != NULL)
	{
		FILE * fp = fopen("imu.data","wb");
		fwrite(buffer,len,1,fp);
		fclose(fp);
		float * fData = (float*)data;
		printf("%f  %f  %f  %f  %f  %f  %f  %f  %f\n",fData[0], fData[1], fData[2], fData[3], fData[4], fData[5], fData[6], fData[7], fData[8]);
	}
	free_sei_content(&data);
	free(buffer);
	return 0;
}

int testSEI() {
	uint8_t source[9] = { 0};
	uint32_t source_len = 9;

	uint32_t annexbType = 1;
	uint32_t len = get_sei_packet_size((const uint8_t*)source, source_len, annexbType);
	uint8_t * buffer = (uint8_t*)malloc(len);

	fill_sei_packet(buffer, annexbType, IMU_UUID, (const uint8_t*)source, source_len);

	uint8_t * data = NULL;
	uint32_t size = 0;
	get_sei_content(buffer, len, IMU_UUID, &data, &size);
	if (data != NULL)
	{
		printf("%d",memcmp(data, source, source_len));

		free_sei_content(&data);
	}
	free(buffer);
	return 0;

}

#include <string.h>

int main(int argv ,char* argc[])
{
	char * type = NULL;
	if (argv > 1)
	{
		type = argc[1];
	}
	else {
		return -1;
	}

	char * inpath = "../temp_frame/";
	char * pathNV12 = "../temp_nv12/";
	char * pathYUV = "/temp_yuv/";

	char * infile = "../video.mp4";
	char * infile_h264 = "../video.h264";
	char * infile_yuv = "../video.yuv";
	char * infile_sei_mp4 = "../video_sei.mp4";
	char * outfile = "../1.mp4";
	char * outfile_aac = "../1.aac";
	char * outfile_pcm = "../1.pcm";

	if (strcasecmp(type, "convert") == 0)
	{
		return testConvert(pathNV12, pathYUV);
	}
	else if (strcasecmp(type, "decode") == 0)
	{
		return testDecode(infile);
	}
	else if (strcasecmp(type, "decodepath") == 0)
	{
		return testDecodePath(inpath);
	}
	else if (strcasecmp(type, "sei") == 0)
	{
		return testDecodeSEI(infile_sei_mp4);
	}
	else if (strcasecmp(type, "encode") == 0)
	{
		return testEncode(infile_yuv, outfile);
	}
	else if (strcasecmp(type, "encode2") == 0)
	{
		return testEncode2(infile, outfile);
	}
	else if (strcasecmp(type, "encode3") == 0)
	{
		return testEncode3(infile, outfile);
	}
	else if (strcasecmp(type, "encode4") == 0)
	{
		return testMP4Encode(infile, outfile);
	}
	else if (strcasecmp(type, "ffmpeg") == 0)
	{
		return testFFmpeg(infile);
	}
	else if (strcasecmp(type, "ffmpegio") == 0)
	{
		return testFFmpegIO(inpath);
	}
	else if (strcasecmp(type, "flv") == 0)
	{
		return testFLV(inpath);
	}
	else if (strcasecmp(type, "header") == 0)
	{
		return TestHeader(infile);
	}
	else if (strcasecmp(type, "interface") == 0)
	{
		return TestInterface(infile);
	}
	else if (strcasecmp(type, "control") == 0)
	{
		return testMediaControl(outfile);
	}
	else if (strcasecmp(type, "adts") == 0)
	{
		return testADTS(infile, outfile_aac);
	}
	else if (strcasecmp(type, "adtso") == 0)
	{
		return processAAC(infile, outfile_aac);
	}
	else if (strcasecmp(type, "adtsd") == 0)
	{
		return decodeAAC(outfile_aac, outfile_pcm);
	}
	else if (strcasecmp(type, "adtsp") == 0)
	{
		printADTS(outfile_aac);
	}
	else if (strcasecmp(type, "aac") == 0)
	{
		simplest_aac_parser(outfile_aac);
	}
	else if (strcasecmp(type, "extra") == 0)
	{
		getExtraData(infile);
	}
	else if (strcasecmp(type, "sei_test") == 0)
	{
		//testSEI();
		testIMU();
	}
	else if (strcasecmp(type, "transform") == 0)
	{
		EVOReconductanceDone(infile, outfile);
	}
	else
	{
		printf("not found type %s\n",type);
	}
	return 0;
}