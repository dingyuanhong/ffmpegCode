//
// Created by jephy on 7/13/17.
//

#ifndef MEDIACONTROLLERDEMO_MEDIACONTROL_H
#define MEDIACONTROLLERDEMO_MEDIACONTROL_H
#ifdef _WIN32
#include "EvHeade.h"
#include "../EvoInterface/EvoMediaSource.h"
#include "../EvoInterface/VideoDecoder.h"
#include "../EvoInterface/EvoVideoConvert.h"
#include  <Windows.h>
#include <mutex>
#else
#include "EvoInterface/EvoMediaSource.h"
#include "EvoInterface/VideoDecoder.h"
#include "EvoInterface/EvoVideoConvert.h"
#include <pthread.h>
#endif

class MediaControl {
public:
    MediaControl();
    ~MediaControl();
    int Open(const char * file);
    int Close();
    int Play();
    int Pause();
    int Seek(int second);
    void Run();
	bool CheckIsEnd() { return IsEnd; }
protected:
	virtual void AttachThread() {};
	virtual void DetachThread() {};
    virtual void SendPacket(int64_t timestamp,AVFrame *frame);
private:
    EvoMediaSource source;
    EvoVideoConvert convert;
    VideoDecoder *decoder;
	AVCodecContext *codecContext;

    int64_t timestamp_now;
    int64_t timestamp_last;
    int64_t time_last;
    AVFrame *frame_last;
    bool bStop;
    bool bPause;
	bool IsEnd;
#ifdef _WIN32
	std::mutex lock;
	HANDLE thread;
#else
	pthread_mutex_t lock;
    pthread_t thread;
#endif
};


#endif //MEDIACONTROLLERDEMO_MEDIACONTROL_H
