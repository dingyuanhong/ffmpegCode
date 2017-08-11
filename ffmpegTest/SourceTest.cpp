
#include "testffmpeg.cpp"
#include "testFFmpegIO.cpp"
#include "testEncode.cpp"
#include "testFLV.cpp"
#include "testMeidaControl.cpp"

#pragma comment(lib,"EvoInterface.lib")

int main()
{
	char * path = "D:/Users/ee/Desktop/temp_frame_0724_1549/temp_frame_0724_1549";
	//char * path = "D:/Users/ee/Desktop/temp_frame_072/temp_frame_0724_1549";
	char * file = "../1.mp4";
	char * outfile = "../output.mp4";
	char * infile = "../video.h264";
	return testMediaControl(outfile);
	//return testFLV(path);
	return testFFmpegIO(path);
	return testEncode(file);
	return testFFmpeg(infile);
}