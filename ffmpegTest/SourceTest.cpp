
#include "testffmpeg.cpp"
#include "testFFmpegIO.cpp"
#include "testEncode.cpp"
#include "testFLV.cpp"
#include "testMeidaControl.cpp"
#include "testEncode2.cpp"
#include "testConvert.cpp"
#include "testHead.cpp"
#include "testDecode.cpp"
#include "testDecodePath.cpp"
#include "testDecodeSEI.cpp"
#include "testEncode.cpp"
#include "testInterface.cpp"
#include "testMP4Encode.cpp"


#pragma comment(lib,"EvoInterface.lib")

int main()
{
	char * inpath = "../temp_frame/";
	char * pathNV12 = "../nv_temp/";
	char * pathYUV = "/yuv_temp/";

	char * infile = "../Vid0616000023.mp4";
	char * infile_h264 = "../video.h264";
	char * infile_yuv = "../ds_480x272.yuv";
	char * infile_mp4 = "../Vid0616000023.mp4";
	char * outfile = "../1.mp4";
	//return testConvert(pathNV12, pathYUV);
	//return testDecode(infile);
	//return testDecodePath(inpath);
	//return testDecodeSEI(infile_mp4);
	return testEncode(infile_yuv, outfile);
	return testEncode2(infile, outfile);
	return testFFmpeg(infile);
	return testFFmpegIO(inpath);
	return testFLV(inpath);
	return TestHeader(infile);
	return TestInterface(infile);
	return testMediaControl(outfile);
	return testMP4Encode(infile,outfile);
}