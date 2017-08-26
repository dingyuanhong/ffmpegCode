#include "EvoMediaSource.h"
#include "sei_packet.h"

inline bool IsAnnexb(uint8_t * packet,int size)
{
	unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
	unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

	unsigned char *data = packet;
	bool isAnnexb = false;
	if (packet == NULL) return isAnnexb;
	if ((size > 3 && memcmp(data, ANNEXB_CODE_LOW, 3) == 0) ||
		(size > 4 && memcmp(data, ANNEXB_CODE, 4) == 0)
		)
	{
		isAnnexb = true;
	}
	return isAnnexb;
}

void EvoFreeFrame(EvoFrame ** frame)
{
	if (frame != NULL)
	{
		if (*frame != NULL)
		{
			av_free(*frame);
		}
		*frame = NULL;
	}
}

#ifdef USE_NEW_API
static AVCodecContext *CreateCodecContent(AVCodecParameters *codecpar)
{
	AVCodecContext *codecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(codecContext, codecpar);
	return codecContext;
}
#endif

EvoMediaSource::EvoMediaSource()
	:context_(NULL),
	packet_(NULL),
	videoIndex_(-1),
	videoStream_(NULL),
	codecContext_(NULL),
	pps_data_(NULL),
	pps_size_(0),
	sps_data_(NULL),
	sps_size_(0)
{
}

EvoMediaSource::~EvoMediaSource()
{
	Close();
}

int EvoMediaSource::Open(const char * file, EvoMediaSourceConfig *config, enum AVMediaType codecType)
{
	av_register_all();

	context_ = avformat_alloc_context();

	AVDictionary* options = NULL;
	//����̽��ͷ��������ĵȴ�
	av_dict_set(&options, "max_analyze_duration", "100", 0);
	av_dict_set(&options, "probesize", "1024", 0);
	int ret = avformat_open_input(&context_,file,NULL, &options);
	if (ret != 0)
	{
		if (context_ != NULL)
		{
			avformat_close_input(&context_);
		}
		context_ = NULL;
		return ret;
	}

#ifndef USE_NEW_API
	for (size_t i = 0; i < context_->nb_streams; i++)
	{
		if (context_->streams[i]->codec->codec_type == codecType)
		{
			videoStream_ = context_->streams[i];
			AVCodec * codec = GetBestDecoder(VideoDecoderName, videoStream_->codec->codec_type,videoStream_->codec->codec_id);
			if (codec == NULL) codec = avcodec_find_decoder(videoStream_->codec->codec_id);
			if (codec != NULL)
			{
				videoStream_->codec->codec = codec;
			}
		}
	}
#endif

	context_->probesize = 1024;
#ifndef USE_NEW_API
	//context_->max_analyze_duration = 5 * AV_TIME_BASE;
#endif
	if (avformat_find_stream_info(context_, NULL))
	{
		if (context_ != NULL)
		{
			avformat_close_input(&context_);
		}
		context_ = NULL;
		return -1;
	}

	videoIndex_ = -1;
	for (size_t i = 0; i < context_->nb_streams; i++)
	{
#ifdef USE_NEW_API
		if (context_->streams[i]->codecpar->codec_type == codecType)
		{
			videoIndex_ = (int)i;
			videoStream_ = context_->streams[i];
			codecContext_ = CreateCodecContent(videoStream_->codecpar);
			AVCodec * codec = GetBestDecoder(VideoDecoderName, codecContext_->codec_type, codecContext_->codec_id);
			if (codec == NULL) codec = avcodec_find_decoder(codecContext_->codec_id);
			if (codec != NULL)
			{
				codecContext_->codec = codec;
			}
			if (videoStream_ != NULL && codecContext_ != NULL && codecContext_->codec != NULL)
			{
				printf("EvoMediaSource DECODE:%s\n", codecContext_->codec->name);
			}
		}
#else
		if (context_->streams[i]->codec->codec_type == codecType)
		{
			videoIndex_ = (int)i;
			videoStream_ = context_->streams[i];
			codecContext_ = videoStream_->codec;
			AVCodec * codec = GetBestDecoder(VideoDecoderName, codecContext_->codec_type, codecContext_->codec_id);
			if (codec == NULL) codec = avcodec_find_decoder(codecContext_->codec_id);
			if (codec != NULL)
			{
				codecContext_->codec = codec;
			}
			if (videoStream_ != NULL && codecContext_ != NULL && codecContext_->codec != NULL)
			{
				printf("EvoMediaSource DECODE:%s\n", codecContext_->codec->name);
			}
		}
#endif
	}
	
	if (videoIndex_ == -1 || 
		(codecType == AVMEDIA_TYPE_VIDEO && AV_CODEC_ID_H264 != codecContext_->codec_id))
	{
		if (context_ != NULL) 
		{
			avformat_close_input(&context_);
		}
		context_ = NULL;
		videoStream_ = NULL;
		return -1;
	}

	packet_ = (AVPacket*)av_malloc(sizeof(AVPacket));
	memset(packet_,0,sizeof(AVPacket));
	av_init_packet(packet_);

	if (codecContext_ != NULL && AV_CODEC_ID_H264 == codecContext_->codec_id && codecType == AVMEDIA_TYPE_VIDEO)
	{
		AnalysisVideoPPSSPS();
	}

	if (config != NULL)
	{
		Config_ = *config;
	}
	
	return 0;
}

void EvoMediaSource::Close()
{
	if (context_ != NULL)
	{
		avformat_close_input(&context_);
	}
	if (packet_ != NULL)
	{
#ifdef USE_NEW_API
		av_packet_unref(packet_);
#else
		av_free_packet(packet_);
#endif
		av_freep(packet_);
	}
	if (codecContext_ != NULL)
	{
		avcodec_close(this->codecContext_);
#ifdef USE_NEW_API
		avcodec_free_context(&codecContext_);
#endif
		this->codecContext_ = NULL;
	}
	context_ = NULL;
	packet_ = NULL;
	videoIndex_ = -1;
	videoStream_ = NULL;

	if (pps_data_ != NULL)
	{
		av_free(pps_data_);
		pps_data_ = NULL;
	}
	if (sps_data_ != NULL)
	{
		av_free(sps_data_);
		sps_data_ = NULL;
	}
	pps_size_ = 0;
	sps_size_ = 0;

	Config_.UseAnnexb = false;
	Config_.UseSei = false;
}

int EvoMediaSource::Seek(int64_t millisecond)
{
	if (context_ == NULL) return -1;
	int duration = GetDuration();
	if (duration > 0 && millisecond > duration)
	{
		millisecond = duration;
	}
	int64_t timeStamp = millisecond * 1000;
	int ret = av_seek_frame(context_,-1, timeStamp, AVSEEK_FLAG_BACKWARD);
	if (ret >= 0)
	{
		avformat_flush(this->context_);
		return 0;
	}
	return ret;
}

int EvoMediaSource::ReadFrame(EvoFrame** out)
{
	if (context_ == NULL) return -1;
	bool getPacket = false;
	do {
		int ret = av_read_frame(context_, packet_);
		if (ret == 0)
		{
			if (packet_->stream_index == videoIndex_)
			{
				if (codecContext_->codec_type == AVMEDIA_TYPE_VIDEO)
				{
					//读取成功
					if (out != NULL)
					{
						int64_t timestamp = (packet_->pts != AV_NOPTS_VALUE) ? (packet_->pts * av_q2d(videoStream_->time_base) * 1000) :
							(packet_->dts != AV_NOPTS_VALUE) ? (packet_->dts * av_q2d(videoStream_->time_base) * 1000) : NAN;

						char sei_buf[128];
						sprintf(sei_buf,"flags:%d pts:%llu dts:%llu timestamp:%llu time_base:num:%d den:%d",
							packet_->flags,packet_->pts, packet_->dts, timestamp, videoStream_->time_base.num, videoStream_->time_base.den);
						size_t len = strlen(sei_buf);
						size_t sei_len = get_sei_packet_size((const uint8_t*)sei_buf,len);

						if (!Config_.UseSei)
						{
							sei_len = 0;
						}

						EvoFrame * frame = (EvoFrame*)av_malloc(sizeof(EvoFrame) + packet_->size + sei_len);
						frame->pts = packet_->pts;
						frame->dts = packet_->dts;
						frame->timestamp = timestamp;
						frame->flags = packet_->flags;
						frame->size = packet_->size + sei_len;
						memcpy(frame->data, packet_->data, frame->size);

						bool annexb = IsAnnexb(packet_->data,packet_->size);

						if (Config_.UseSei) {
							//填充sei信息
							fill_sei_packet(frame->data + packet_->size, annexb, TIME_STAMP_UUID, (const uint8_t*)sei_buf, len);
						}
						if (!annexb && Config_.UseAnnexb)
						{
							ChangeAnnexb(frame);
						}

						*out = frame;
						getPacket = true;
					}
				}
				else if (codecContext_->codec_type == AVMEDIA_TYPE_AUDIO)
				{
					//读取成功
					if (out != NULL)
					{
						int64_t timestamp = (packet_->pts != AV_NOPTS_VALUE) ? (packet_->pts * av_q2d(videoStream_->time_base) * 1000) :
							(packet_->dts != AV_NOPTS_VALUE) ? (packet_->dts * av_q2d(videoStream_->time_base) * 1000) : NAN;

						EvoFrame * frame = (EvoFrame*)av_malloc(sizeof(EvoFrame) + packet_->size);
						frame->pts = packet_->pts;
						frame->dts = packet_->dts;
						frame->timestamp = timestamp;
						frame->flags = packet_->flags;
						frame->size = packet_->size;
						memcpy(frame->data, packet_->data, frame->size);

						*out = frame;
						getPacket = true;
					}
				}
			}
		}

		if (packet_ != NULL) 
		{
			av_packet_unref(packet_);
		}
		if (getPacket)
		{
			break;
		}
		if (ret == AVERROR_EOF)
		{
			//文件结束
			return ret;
		}
	} while (true);
	return 0;
}

void EvoMediaSource::ChangeAnnexb(EvoFrame * frame)
{
	uint8_t * data = frame->data;
	while (data < frame->data + frame->size)
	{
		int32_t nula_size = reversebytes(*(int32_t*)data);
		data[0] = 0x00;
		data[1] = 0x00;
		data[2] = 0x00;
		data[3] = 0x01;
		data += nula_size + 4;
	}
}

int EvoMediaSource::GetExtData(uint8_t * data, int size)
{
	if (codecContext_ == NULL) return 0;

	if (data == NULL)
	{
		return codecContext_->extradata_size;
	}
	if (size < codecContext_->extradata_size)
	{
		return codecContext_->extradata_size;
	}
	memcpy(data, codecContext_->extradata, codecContext_->extradata_size);
	
	return codecContext_->extradata_size;
}

//PPS
int EvoMediaSource::GetPPS(uint8_t * data, int size)
{
	if (videoStream_ == NULL) return 0;
	if (pps_size_ <= size && pps_data_ != NULL && data != NULL)
	{
		memcpy(data,pps_data_,pps_size_);
	}
	return pps_size_;
}

//SPS
int EvoMediaSource::GetSPS(uint8_t * data, int size)
{
	if (videoStream_ == NULL) return 0;
	if (sps_size_ <= size && sps_data_ != NULL && data != NULL)
	{
		memcpy(data, sps_data_, sps_size_);
	}
	return sps_size_;
}

int EvoMediaSource::GetDuration()
{
	if (context_ == NULL) return 0;
	return (int)(context_->duration/1000);
}

int EvoMediaSource::GetFrameRate()
{
	if (videoStream_ == NULL) return 0;
	return videoStream_->r_frame_rate.num;
}

int EvoMediaSource::GetFrameCount()
{
	if (videoStream_ == NULL) return 0;
	return (int)videoStream_->nb_frames;
}

//��
int EvoMediaSource::GetWidth()
{
	if (codecContext_ == NULL) return 0;
	return codecContext_->width > 0 ?
		codecContext_->width : codecContext_->coded_width;
}

//��
int EvoMediaSource::GetHeight()
{
	if (codecContext_ == NULL) return 0;
	return codecContext_->height > 0 ?
		codecContext_->height : codecContext_->coded_height;
}

AVStream * EvoMediaSource::GetVideoStream()
{
	return videoStream_;
}

AVCodecContext * EvoMediaSource::GetCodecContext()
{
	return codecContext_;
}

int EvoMediaSource::AnalysisVideoPPSSPS()
{
	if (codecContext_ == NULL) return -1;

	int extradata_size = codecContext_->extradata_size;
	uint8_t * extradata = codecContext_->extradata;
	if (IsAnnexb(extradata,extradata_size))
	{
		int sps_index = 0;
		int sps_size = 0;
		int pps_index = 0;
		int pps_size = 0;
		int cur_type = 0;  //1 sps 2 pps
		uint8_t * data = extradata;
		while (data < extradata + extradata_size - 1)
		{
			if (data[0] == 0x00 && data[1] == 0x00 )
			{
				int header_size = 2;
				if (data[2] == 0x01)
				{
					header_size = 3;
				}
				else if (data[2] == 0x00 && data[3] == 0x01)
				{
					header_size = 4;
				}
				if (header_size != 2)
				{
					if ((data[header_size] & 0x1f) == 7)
					{
						sps_index = data + header_size - extradata;
						if (cur_type == 2)
						{
							pps_size = data - extradata - pps_index;
						}
						cur_type = 1;
					}
					else if ((data[header_size] & 0x1f) == 8)
					{
						pps_index = data + header_size - extradata;
						if (cur_type == 1)
						{
							sps_size = data - extradata - sps_index;
						}
						cur_type = 2;
					}
					else
					{
						cur_type = 0;
					}
				}
				data += 2;
			}
			else
			{
				data += 1;
			}
		}
		if (cur_type == 1)
		{
			sps_size = (int)(extradata_size - sps_index);
		}
		else if (cur_type == 2)
		{
			pps_size = (int)(extradata_size - pps_index);
		}

		if (sps_data_ != NULL)
		{
			av_free(sps_data_);
			sps_data_ = NULL;
		}
		sps_size_ = 0;
		if (pps_data_ != NULL)
		{
			av_free(pps_data_);
			pps_data_ = NULL;
		}
		pps_size_ = 0;

		if (sps_size > 0)
		{
			sps_data_ = (uint8_t*)av_malloc(sps_size + 4);
			sps_data_[0] = 0x00;
			sps_data_[1] = 0x00;
			sps_data_[2] = 0x00;
			sps_data_[3] = 0x01;
			memcpy(sps_data_ + 4, extradata + sps_index, sps_size);
			sps_size_ = sps_size + 4;
		}
		
		if (pps_size > 0)
		{
			pps_data_ = (uint8_t*)av_malloc(pps_size + 4);
			pps_data_[0] = 0x00;
			pps_data_[1] = 0x00;
			pps_data_[2] = 0x00;
			pps_data_[3] = 0x01;
			memcpy(pps_data_ + 4, extradata + pps_index, pps_size);
			pps_size_ = pps_size + 4;
		}
		return 0;
	}
	/* retrieve sps and pps NAL units from extradata */
	{
		uint16_t unit_size;
		uint64_t total_size = 0;
		uint8_t unit_nb, sps_done = 0, sps_seen = 0, pps_seen = 0;
		int unit_type = 0;
		extradata = extradata + 4;  //����ǰ4���ֽ�  

		/* retrieve length coded size */
		int length_size = (*extradata++ & 0x3) + 1;    //����ָʾ��ʾ�������ݳ��������ֽ���  
		if (length_size == 3)
			return AVERROR(EINVAL);

		if (sps_data_ != NULL)
		{
			av_free(sps_data_);
			sps_data_ = NULL;
		}
		sps_size_ = 0;
		if (pps_data_ != NULL)
		{
			av_free(pps_data_);
			pps_data_ = NULL;
		}
		pps_size_ = 0;
		
		/* retrieve sps and pps unit(s) */
		unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
		if (!unit_nb) {
			goto pps;
		}
		else {
			sps_seen = 1;
		}

		while (unit_nb--) {
			unit_size = extradata[0];
			unit_size = (unit_size  << 8) + extradata[1];
			total_size += unit_size + 4;
			if (total_size > INT_MAX - FF_INPUT_BUFFER_PADDING_SIZE ||
				extradata + 2 + unit_size > extradata + extradata_size) {
				return AVERROR(EINVAL);
			}
			unit_type = *(extradata + 2) & 0x1f;
			if (unit_type == 7)
			{
				//SPS
				if (sps_data_ == NULL)
				{
					sps_data_ = (uint8_t*)av_malloc(unit_size + 4);
					sps_data_[0] = 0x00;
					sps_data_[1] = 0x00;
					sps_data_[2] = 0x00;
					sps_data_[3] = 0x01;
					memcpy(sps_data_ + 4, extradata + 2, unit_size);
					sps_size_ = unit_size + 4;
				}
				
			}
			else if (unit_type == 8)
			{
				//PPS
				if (pps_data_ == NULL)
				{
					pps_data_ = (uint8_t*)av_malloc(unit_size + 4);
					pps_data_[0] = 0x00;
					pps_data_[1] = 0x00;
					pps_data_[2] = 0x00;
					pps_data_[3] = 0x01;
					memcpy(pps_data_ + 4, extradata + 2, unit_size);
					pps_size_ = unit_size + 4;
				}
			}

			extradata += 2 + unit_size;
		pps:
			if (!unit_nb && !sps_done++) {
				unit_nb = *extradata++; /* number of pps unit(s) */
				if (unit_nb)
					pps_seen = 1;
			}
		}
	}
	return 0;
}

AVCodec *EvoMediaSource::GetBestDecoder(std::string name, AVMediaType codecType,AVCodecID id) 
{
	
	AVCodec *c_temp = av_codec_next(NULL);
	while (c_temp != NULL) {
		if (c_temp->id == id && c_temp->type == codecType
			&& c_temp->decode != NULL)
		{
			printf("Decode Name:%s\n",c_temp->name);
		}
		c_temp = c_temp->next;
	}

	std::string decoder;
	while (true) {
		size_t pos = name.find(" ");
		if (pos != -1) {
			decoder = name.substr(0, pos);
			name = name.substr(pos + 1);
		}
		else {
			decoder = name;
			name = "";
		}
		if (decoder.length() > 0) {
			AVCodec * codec = avcodec_find_decoder_by_name(decoder.c_str());
			if (codec != NULL && codec->id == id && codec->type == codecType) return codec;
		}
		if (name.length() == 0) break;
	}
	return NULL;
}

void EvoMediaSource::SetVideoCodecName(const char * codec)
{
	VideoDecoderName = codec;
}