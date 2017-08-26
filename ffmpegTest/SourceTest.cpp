
#include "testffmpeg.cpp"
#include "testFFmpegIO.cpp"
#include "testEncode.cpp"
#include "testFLV.cpp"
#include "testMeidaControl.cpp"
#include "testEncode2.cpp"
#include "testConvert.cpp"

#pragma comment(lib,"EvoInterface.lib")

int main()
{
	char * path = "D:/Users/ee/Desktop/temp_frame_0724_1549/temp_frame_0724_1549";
	//char * path = "D:/Users/ee/Desktop/temp_frame_072/temp_frame_0724_1549";
	char * infile = "../output.mp4";
	char * outfile2 = "../1.mp4";
	char * infile1 = "../video.h264";
	char * infile2 = "../ds_480x272.yuv";
	char * pathNV12 = "D:/Users/ee/Desktop/temp/";
	char * pathYUV = "D:/Users/ee/Desktop/yuv_temp/";
	//return testConvert(pathNV12, pathYUV);
	return testEncode2(infile, outfile2);
	//return testEncode(infile2, outfile2);
	//return testMediaControl(outfile);
	//return testFLV(path);
	//return testFFmpegIO(path);
	return testFFmpeg(infile);
}