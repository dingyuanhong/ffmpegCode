//
// Created by jephy on 7/13/17.
//

#include "MediaControl.h"
#ifdef _ANDROID
#include <android/log.h>
#endif

inline int GetAnnexbLength(uint8_t * nalu, int nalu_size)
{
    unsigned char ANNEXB_CODE_LOW[] = { 0x00,0x00,0x01 };
    unsigned char ANNEXB_CODE[] = { 0x00,0x00,0x00,0x01 };

    unsigned char *data = nalu;
    int size = nalu_size;
    if(data == NULL)
    {
        return 0;
    }
    if ((size > 3 && memcmp(data, ANNEXB_CODE_LOW,3) == 0))
    {
        return 3;
    }
    else if((size > 4 && memcmp(data, ANNEXB_CODE,4) == 0))
    {
        return 4;
    }
    return 0;
}

//MP4模式扩展数据
inline uint8_t * MakeExtraData(uint8_t * sps, int sps_len, uint8_t * pps, int pps_len, int * out_len)
{
    int sps_header = GetAnnexbLength(sps,sps_len);
    int pps_header = GetAnnexbLength(pps,pps_len);
    sps += sps_header;
    sps_len -= sps_header;
    pps += pps_header;
    pps_len -= pps_header;

    int extraSize = 5 + 1 + 2 + sps_len + 1 + 2 + pps_len;
    uint8_t * extraBuffer = (uint8_t*)av_malloc(extraSize);
    memset(extraBuffer, 0, extraSize);
    uint8_t * extra = extraBuffer;
    extra[0] = 0x01;
    extra[1] = sps[1];
    extra[2] = sps[2];
    extra[3] = sps[3];
    extra[4] = 0xFF;
    extra[5] = 0xE1;
    extra[6] = sps_len >> 8;
    extra[7] = sps_len & 0xFF;
    memcpy(extra + 8, sps, sps_len);
    extra += 8 + sps_len;
    extra[0] = 0x01;
    extra[1] = pps_len >> 8;
    extra[2] = pps_len & 0xFF;
    memcpy(extra + 3, pps, pps_len);

    if (out_len != NULL) *out_len = extraSize;
    return extraBuffer;
}

//标准H264扩展数据
inline uint8_t * MakeH264ExtraData(uint8_t * sps, int sps_len, uint8_t * pps, int pps_len, int * out_len)
{
    int sps_header = GetAnnexbLength(sps,sps_len);
    int pps_header = GetAnnexbLength(pps,pps_len);
    sps += sps_header;
    sps_len -= sps_header;
    pps += pps_header;
    pps_len -= pps_header;

    int extraSize = 4 + sps_len + 4 + pps_len;
    uint8_t * extraBuffer = (uint8_t*)av_malloc(extraSize);
    memset(extraBuffer, 0, extraSize);
    uint8_t * extra = extraBuffer;
    extra[3] = 0x01;
    memcpy(extra + 4, sps, sps_len);
    extra += 4 + sps_len;
    extra[3] = 0x01;
    memcpy(extra + 4, pps, pps_len);

    if (out_len != NULL) *out_len = extraSize;
    return extraBuffer;
}

#ifdef _WIN32
DWORD WINAPI MediaControl_Thread(void* param)
#else
void*  MediaControl_Thread(void* param)
#endif
{
    MediaControl * thiz = (MediaControl*)param;
    thiz->Run();
    return 0;
}

MediaControl::MediaControl()
    :bStop(false),bPause(false)
    ,timestamp_last(0),timestamp_now(0),
     time_last(0),thread(0),
     decoder(NULL),frame_last(NULL),
	codecContext(NULL),extraData_(NULL)
{
#ifndef _WIN32
    pthread_mutex_init(&lock,NULL);
#endif
	IsEnd = true;
}

MediaControl::~MediaControl()
{
    Close();
#ifndef _WIN32
    pthread_mutex_destroy(&lock);
#endif
}

inline AVCodec *GetBestDecoder(AVCodecID id,enum AVMediaType type,std::string Name) {
    if (Name.length() == 0) return NULL;

    AVCodec *c_temp = av_codec_next(NULL);
    while (c_temp != NULL) {
        if (c_temp->id == id && c_temp->type == type
            && c_temp->decode != NULL)
        {
            //__android_log_print(ANDROID_LOG_INFO,"native","Video H264 decode:%s\n",c_temp->name);
        }
        c_temp = c_temp->next;
    }

    std::string name = Name;
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
            if (codec != NULL && codec->id == id && codec->type == type) return codec;
        }
        if (name.length() == 0) break;
    }
    return NULL;
}

int MediaControl::Open(const char * file)
{
    int ret = source.Open(file);
    if(ret == 0)
    {
		bool newContext = true;
		if (newContext) {
            AVCodec *codec = GetBestDecoder(AV_CODEC_ID_H264,AVMEDIA_TYPE_VIDEO,"h264_mediacodec");//h264_mediacodec
            if (codec == NULL) codec = avcodec_find_decoder(AV_CODEC_ID_H264);
			if (!codec) {
				source.Close();
				return -1;
			}
			codecContext = avcodec_alloc_context3(NULL);
			uint8_t extData[128];
            int size = source.GetExtData(extData, 128);
            uint8_t sps[128];
            int sps_size = source.GetSPS(sps,128);
            uint8_t pps[128];
            int pps_size = source.GetPPS(pps,128);
            int out_size = 0;
            uint8_t *  extraData = MakeExtraData(sps,sps_size,pps,pps_size,&out_size);

			codecContext->extradata = extraData;
			codecContext->extradata_size = out_size;
            if(extraData_ != NULL)
            {
                av_free(extraData_);
                extraData_ = NULL;
            }
            extraData_ = extraData;

            codecContext->width = source.GetWidth();
            codecContext->height = source.GetHeight();
            codecContext->coded_width = source.GetWidth();
            codecContext->coded_height = source.GetHeight();
			//设置解码器数量用以加快解码速度
//			codecContext->thread_count = 5;
//			codecContext->active_thread_type = FF_THREAD_FRAME;
            ret = avcodec_open2(codecContext, codec, NULL);
			if (ret != 0)
			{
                codecContext->extradata = NULL;
                codecContext->extradata_size = 0;
				avcodec_free_context(&codecContext);
				codecContext = NULL;
				source.Close();
				return ret;
			}
            codecContext->codec = codec;
			decoder = new VideoDecoder(codecContext);
		}
		else {
			AVStream * stream = source.GetVideoStream();
			AVCodecContext *Context = source.GetCodecContext();
			if (Context == NULL) return -1;
			//AVCodec *codec = (AVCodec*)Context->codec;
            AVCodec *codec = GetBestDecoder(Context->codec_id,AVMEDIA_TYPE_VIDEO,"h264_mediacodec");
			if (codec == NULL) codec = avcodec_find_decoder(Context->codec_id);
			if (codec == NULL) {
				source.Close();
				return -1;
			}
            Context->codec = codec;
            ret = avcodec_open2(Context, codec, NULL);
			if (ret != 0)
			{
				source.Close();
				return ret;
			}
			decoder = new VideoDecoder(Context);
		}
    }
    if(decoder != NULL)
    {
        EvoVideoInfo info ;
        info.Width = 0;
        info.Height = 0;
        info.Format = AV_PIX_FMT_NONE;
        EvoVideoInfo oInfo = info;
        oInfo.Format = AV_PIX_FMT_YUV420P;
        convert.Initialize(info,oInfo);
        decoder->Attach(&convert);
    }
    return ret;
}

int MediaControl::Close()
{
    bStop = true;
    if(thread != 0)
    {
#ifdef _WIN32
		WaitForSingleObject(thread,INFINITE);
		CloseHandle(thread);
#else
        pthread_join(thread,NULL);
#endif
        thread = 0;
    }
    if(decoder != NULL)
    {
        delete decoder;
        decoder = NULL;
    }
	if (codecContext != NULL)
	{
		avcodec_close(codecContext);
        codecContext->extradata = NULL;
        codecContext->extradata_size = 0;
        avcodec_free_context(&codecContext);
		codecContext = NULL;
	}
    if(extraData_ != NULL)
    {
        av_free(extraData_);
        extraData_ = NULL;
    }
    source.Close();
    return 0;
}

int MediaControl::Seek(int second)
{
    int millSecond = second * 1000;
#ifdef _WIN32
	lock.lock();
	int ret = source.Seek(millSecond);
	lock.unlock();
#else
    pthread_mutex_lock(&lock);
    int ret = source.Seek(millSecond);
    pthread_mutex_unlock(&lock);
#endif
    return ret;
}

int MediaControl::Play()
{
    if(source.GetVideoStream() == NULL)
    {
        return -1;
    }
    if(decoder == NULL)
    {
        return -1;
    }

    int ret = 0;
    if(thread == 0)
    {
        bStop = false;
        bPause = true;
		IsEnd = false;
#ifdef _WIN32
		thread = CreateThread(NULL,0, MediaControl_Thread,this,0,NULL);
#else
        ret = pthread_create(&thread,NULL,&MediaControl_Thread,(void*)this);
        if(ret != 0)
        {
            printf("%s:开启线程失败!\n",__FUNCTION__);
        }
#endif
    }
    bPause = false;
    return ret;
}

int MediaControl::Pause()
{
    int ret = (bPause == true);
    bPause = true;
    return ret;
}

void MediaControl::Run()
{
    AttachThread();
    time_last = av_gettime();
    int64_t  timeBegin = 0;
    while(true)
    {
        if(bStop)
        {
            break;
        }
        if(bPause)
        {
            av_usleep(100);
            continue;
        }
        if(frame_last == NULL)
        {
            EvoFrame* out;
#ifdef _WIN32
			lock.lock();
			int ret = source.ReadFrame(&out);
			lock.unlock();
#else
            pthread_mutex_lock(&lock);
            int ret = source.ReadFrame(&out);		
            pthread_mutex_unlock(&lock);
#endif
            if(ret == AVERROR_EOF)
            {
                break;
            }
            if(out != NULL)
            {
                AVFrame *frame = NULL;
				if(timeBegin == 0)  timeBegin = av_gettime() / 1000;
                decoder->DecodeFrame(out,&frame);

                if(frame != NULL)
                {
                    int64_t timeEnd = av_gettime() / 1000;
#ifdef _ANDROID
                    __android_log_print(ANDROID_LOG_INFO,"native MeidaControl"," use:%lld\n", timeEnd - timeBegin);
#endif
                    timeBegin = 0;

					timestamp_now = out->timestamp;
                    if(av_gettime() - time_last > timestamp_now - timestamp_last) {
                        timestamp_last = timestamp_now;
                        time_last = av_gettime();
                        SendPacket(timestamp_now, frame);
                        FreeAVFrame(&frame);
                    } else{
                        frame_last = frame;
                    }
                }
                EvoFreeFrame(&out);
            }
        } else{
            if(av_gettime() - time_last > timestamp_now - timestamp_last)
            {
                timestamp_last = timestamp_now;
                time_last = av_gettime();
                SendPacket(timestamp_now,frame_last);
                FreeAVFrame(&frame_last);
            } else{
                uint64_t time = av_gettime() - time_last - (timestamp_now - timestamp_last) - 5;
                if(time > 1000*1000/10)
                {
                    time = 1000*1000/10;
                }
                av_usleep(time);
            }
        }
    }

    if(frame_last != NULL)
    {
        FreeAVFrame(&frame_last);
    }
    DetachThread();
	IsEnd = true;
}

void MediaControl::SendPacket(int64_t timestamp,AVFrame *frame)
{
}