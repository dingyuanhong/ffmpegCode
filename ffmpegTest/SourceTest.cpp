#include "EvHeade.h"
#include "sei_packet.h"
#include "json\json.h"
#pragma comment(lib,"lib_json.lib")
#include "Encode.h"

AVFormatContext * av_open_file(const char * file)
{
	AVFormatContext * formatContext = avformat_alloc_context();

	int ret = avformat_open_input(&formatContext, "../sp.mp4", NULL, NULL);
	if (ret != 0) {
		avformat_close_input(&formatContext);
		return NULL;
	}

	ret = avformat_find_stream_info(formatContext, NULL);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return NULL;
	}

	return formatContext;
}

int resetPacket(AVPacket * packet, AVPacket * pkt)
{
	//生成自定义数据
	Json::Value data;
	data.append(Json::Value(packet->pts));
	data.append(Json::Value(packet->dts));
	Json::FastWriter  writer;
	std::string str = writer.write(data);

	//获取自定义数据长度
	size_t sei_packet_size = get_sei_packet_size(str.size());

	av_new_packet(pkt, packet->size + (int)sei_packet_size);
	memset(pkt->data, 0, packet->size + sei_packet_size);
	pkt->pts = packet->pts;
	pkt->dts = packet->dts;

	//填充原始数据
	memcpy(pkt->data, packet->data, packet->size);

	//填充自定义数据
	unsigned char * sei = (unsigned char*)pkt->data + packet->size;
	fill_sei_packet(sei, false, str.data(), str.size());

	return 0;
}

int decodeVideo(AVCodecContext * context, AVPacket * packet, AVFrame * frame)
{
	int ret = avcodec_send_packet(context, packet);
	if (ret == 0)ret = avcodec_receive_frame(context, frame);
	if (ret == 0) {
		printf("%Id %d %d\n", packet->pts, frame->width, frame->height);

		av_frame_unref(frame);
		return 0;
	}
	else
	{
		char str[1024];
		av_make_error_string(str, 1024, ret);
		printf("decodeVideo Error:%d %s\n", ret, str);
	}
	return ret;
}

int testFFmpeg()
{
	av_register_all();

	AVFormatContext * formatContext = avformat_alloc_context();

	int ret = avformat_open_input(&formatContext, "../sp.mp4", NULL, NULL);
	if (ret != 0) {
		avformat_close_input(&formatContext);
		return -1;
	}

	ret = avformat_find_stream_info(formatContext, NULL);
	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		return -1;
	}

	int videoIndex = -1;

	for (size_t i = 0; i < formatContext->nb_streams; i++)
	{
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoIndex = (int)i;
			break;
		}
	}

	if (videoIndex == -1)
	{

		avformat_close_input(&formatContext);
		return -1;
	}

	AVCodecParameters * paramter = formatContext->streams[videoIndex]->codecpar;

	AVCodec * codec = avcodec_find_decoder(paramter->codec_id);

	AVCodecContext * codecContext = avcodec_alloc_context3(codec);

	avcodec_parameters_to_context(codecContext, paramter);

	ret = avcodec_open2(codecContext, codec, NULL);

	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		avcodec_close(codecContext);
		avcodec_free_context(&codecContext);
		return -1;
	}

	AVPacket * packet = av_packet_alloc();
	AVFrame * frame = av_frame_alloc();

	AVPacket * pkt = av_packet_alloc();
	while (true)
	{
		ret = av_read_frame(formatContext,packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				//修改包裹
				resetPacket(packet, pkt);
				//解码
				decodeVideo(codecContext, pkt, frame);

				//取出自定义数据
				char selfPacket[255] = {0};
				int count = 255;
				int ret = get_sei_content(pkt->data, pkt->size, selfPacket,&count);
				if (ret > 0)
				{
					Json::Reader reader;
					Json::Value value;
					reader.parse(selfPacket, selfPacket + count,value);
					int vCount = value.size();
					printf("%Id\n",value[0].asInt64());
				}
				
				av_packet_unref(pkt);
			}
		}
		else if (ret == AVERROR_EOF)
		{
			break;
		}
		av_packet_unref(packet);
	}


	av_packet_free(&packet);
	av_frame_free(&frame);
	av_packet_free(&pkt);

	avformat_close_input(&formatContext);
	avcodec_close(codecContext);
	avcodec_free_context(&codecContext);
	
	return 0;
}

int testEncode()
{
	int in_w = 480, in_h = 272;                              //Input data's width and height  


	OriginalEncode encode;
	int ret = encode.Open("./1.mp4");
	if (ret != 0)
	{
		return -1;
	}
	ret = encode.NewVideoStream(in_w, in_h, AV_PIX_FMT_YUV420P);
	if (ret != 0)
	{
		return -1;
	}
	encode.WriteHeader();

	//FILE *in_file = fopen("src01_480x272.yuv", "rb"); //Input raw YUV data   
	FILE *in_file = fopen("../ds_480x272.yuv", "rb");   //Input raw YUV data  
	
	int framenum = 100;                                   //Frames to encode  

	AVStream * stream = encode.GetVideoStream();
	AVCodecContext * codecContext = encode.GetVideoStream()->codec;

	AVFrame * frame = av_frame_alloc();
	int picture_size = avpicture_get_size(codecContext->pix_fmt, codecContext->width, codecContext->height);
	uint8_t * picture_buf = (uint8_t *)av_malloc(picture_size);
	avpicture_fill((AVPicture *)frame, picture_buf, codecContext->pix_fmt, codecContext->width, codecContext->height);
	int y_size = codecContext->width * codecContext->height;

	for (int i = 0; i < framenum; i++)
	{
		//Read raw YUV data  
		if (fread(picture_buf, 1, y_size * 3 / 2, in_file) <= 0) {
			printf("Failed to read raw data! \n");
			break;
		}
		else if (feof(in_file)) {
			break;
		}

		frame->data[0] = picture_buf;					// Y  
		frame->data[1] = picture_buf + y_size;			// U   
		frame->data[2] = picture_buf + y_size * 5 / 4;  // V  
		frame->width = codecContext->width;
		frame->height = codecContext->height;
		frame->format = codecContext->pix_fmt;
		//PTS  
		//pFrame->pts=i;
		frame->pts = i*(stream->time_base.den) / ((stream->time_base.num) * 25);

		encode.EncodeVideo(frame);
	}

	encode.FlushVideo();

	encode.WriteTrailer();
	encode.Close();


	av_free(picture_buf);
	picture_buf = NULL;
	av_frame_free(&frame);
	return  0;
}

int main()
{
	return testEncode();
	return testFFmpeg();
}