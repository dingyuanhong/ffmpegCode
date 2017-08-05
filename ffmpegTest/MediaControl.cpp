//
// Created by jephy on 7/13/17.
//

#include "MediaControl.h"

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
     decoder(NULL),frame_last(NULL)
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

int MediaControl::Open(const char * file)
{
    int ret = source.Open(file);
    if(ret == 0)
    {
        AVStream * stream = source.GetVideoStream();
        if(stream == NULL) return -1;
        AVCodec *codec = (AVCodec*)stream->codec->codec;
        if (codec == NULL) codec = avcodec_find_decoder(stream->codec->codec_id);
        if(codec == NULL) return -1;
        if (avcodec_open2(stream->codec, codec, NULL) < 0)
        {
            return -1;
        }
        decoder = new VideoDecoder(stream->codec);
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
                decoder->DecodeFrame(out,&frame);
                if(frame != NULL)
                {
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