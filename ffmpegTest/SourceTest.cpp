#include "EvHeade.h"
#include "sei_packet.h"
#include "json\json.h"
#pragma comment(lib,"lib_json.lib")
#include "Encode.h"
#include "SEIEncode.h"

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

//#define NEW_API

int decodeVideo(AVCodecContext * context, AVPacket * packet, AVFrame * frame)
{
#ifdef NEW_API
	int ret = avcodec_send_packet(context, packet);
	if (ret == 0)ret = avcodec_receive_frame(context, frame);
#else
	int got_picture_ptr = 0;
	int ret = avcodec_decode_video2(context,frame,&got_picture_ptr,packet);
#endif
	if (got_picture_ptr > 0) {
		printf("decode:%lld %d %d\n", packet->pts, frame->width, frame->height);
		return 1;
	}
	else
	{
		if (ret < 0)
		{
			char str[1024];
			av_make_error_string(str, 1024, ret);
			printf("decodeVideo Error:%d %s\n", ret, str);
		}
		else
		{
			return 0;
		}
	}
	return ret;
}

int testFFmpeg()
{
	av_register_all();

	AVFormatContext * formatContext = avformat_alloc_context();

	int ret = avformat_open_input(&formatContext, "../1.mp4", NULL, NULL);
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
#ifdef NEW_API
		if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
#else
		if (formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#endif
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
#ifdef NEW_API
	AVCodecParameters * paramter = formatContext->streams[videoIndex]->codecpar;

	AVCodec * codec = avcodec_find_decoder(paramter->codec_id);

	AVCodecContext * codecContext = avcodec_alloc_context3(codec);

	avcodec_parameters_to_context(codecContext, paramter);
#else

	AVCodec * codec = avcodec_find_decoder(formatContext->streams[videoIndex]->codec->codec_id);
	AVCodecContext * codecContext = formatContext->streams[videoIndex]->codec;
#endif
	ret = avcodec_open2(codecContext, codec, NULL);

	if (ret != 0)
	{
		avformat_close_input(&formatContext);
		avcodec_close(codecContext);
		avcodec_free_context(&codecContext);
		return -1;
	}

#ifdef NEW_API
	AVPacket * packet = av_packet_alloc();
	AVPacket * pkt = av_packet_alloc();
#else
	AVPacket * packet = (AVPacket*)av_malloc(sizeof(AVPacket));
	av_init_packet(packet);
	AVPacket * pkt = (AVPacket*)av_malloc(sizeof(AVPacket));//av_packet_alloc();
	av_init_packet(pkt);
#endif
	AVFrame * frame = av_frame_alloc();

	while (true)
	{
		ret = av_read_frame(formatContext,packet);
		if (ret == 0)
		{
			if (packet->stream_index == videoIndex)
			{
				//修改包裹
				//resetPacket(packet, pkt);
				//解码
				ret = decodeVideo(codecContext, packet, frame);
				if (ret == 1)
				{
					av_frame_unref(frame);
				}
				//取出自定义数据
				char selfPacket[255] = {0};
				int count = 255;
				ret = get_sei_content(packet->data, packet->size, selfPacket,&count);
				if (ret > 0)
				{
					printf("sei: %s\n", selfPacket);
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


	av_frame_free(&frame);
#ifdef NEW_API
	av_packet_free(&packet);
	av_packet_free(&pkt);
#else
	av_free_packet(packet);
	av_free(packet);
	av_free_packet(pkt);
	av_free(pkt);
#endif

	avcodec_close(codecContext);
#ifdef NEW_API
	avcodec_free_context(&codecContext);
#endif
	avformat_close_input(&formatContext);
	return 0;
}

int testEncode()
{
	int in_w = 480, in_h = 272;                              //Input data's width and height  


	SEIEncode encode;
	int ret = encode.Open("../1.mp4");
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
	//return testEncode();
	return testFFmpeg();
}