#include "EvHeade.h"

#define nil NULL

typedef struct AVHeader{
    AVFormatContext *fc;
    AVStream *st_video;
    AVStream *st_audio;
    int id_video;
    int id_audio;
    AVCodec *codec_video;
    AVCodec *codec_audio;
    AVCodecContext * cc_video;
    AVCodecContext * cc_audio;
}AVHeader;

AVHeader *avheader_new()
{
    AVHeader * header = (AVHeader*)av_malloc(sizeof(struct AVHeader));
    header->fc = NULL;
    header->st_video = NULL;
    header->st_audio = NULL;
    header->id_video = -1;
    header->id_audio = -1;
    header->codec_video = NULL;
    header->codec_audio = NULL;
    header->cc_video = NULL;
    header->cc_audio = NULL;
    return header;
}

void avheader_free(AVHeader**pheader)
{
    if(pheader == NULL) return;
    AVHeader * header = *pheader;
    if(header == NULL) return;
    if(header->fc != NULL){
        if(header->fc->pb!= NULL){
            avio_close(header->fc->pb);
            header->fc->pb = NULL;
        }
        avformat_free_context(header->fc);
    }
    if(header->cc_video != NULL){
        avcodec_free_context(&header->cc_video);
    }
    if(header->cc_audio != NULL){
        avcodec_free_context(&header->cc_audio);
    }
    av_free(header);
    *pheader = NULL;
}

AVHeader *OpenRead(const char * url)
{
    AVFormatContext *fc = avformat_alloc_context();
    int ret;
    ret = avformat_open_input(&fc, url, NULL, 0);
    if (ret < 0) {
        
        avformat_close_input(&fc);
        return nil;
    }
//    fc->probesize = 200;
    ret = avformat_find_stream_info(fc, NULL);
    if(ret != 0){

        avformat_close_input(&fc);
        return nil;
    }
    
    AVStream *video_st = NULL;
    AVCodec *video_codec = NULL;
    int video_st_index = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, &video_codec, 0);
    if (ret >= 0) {
        video_st = fc->streams[video_st_index];
    }
    AVStream *audio_st = NULL;
    AVCodec *audio_codec = NULL;
    int audio_st_index = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, -1, &audio_codec, 0);
    if (ret >= 0) {
        audio_st = fc->streams[audio_st_index];
    }
    
    AVCodecContext *video_avctx = avcodec_alloc_context3(NULL);
    if (video_avctx == NULL) {
        avformat_close_input(&fc);
        return nil;
    }
    ret = avcodec_parameters_to_context(video_avctx, video_st->codecpar);
    if (ret < 0) {
        avcodec_close(video_avctx);
        avformat_close_input(&fc);
        return nil;
    }
    
    AVCodecContext *audio_avctx = avcodec_alloc_context3(NULL);
    if (audio_avctx == NULL) {
        avcodec_close(video_avctx);
        avformat_close_input(&fc);
        return nil;
    }
    ret = avcodec_parameters_to_context(audio_avctx, audio_st->codecpar);
    if (ret < 0) {
        avcodec_close(video_avctx);
        avcodec_close(audio_avctx);
        avformat_close_input(&fc);
        return nil;
    }
    AVHeader * header = (AVHeader*)av_malloc(sizeof(struct AVHeader));
    header->fc = fc;
    header->st_video = video_st;
    header->st_audio = audio_st;
    header->id_video = video_st_index;
    header->id_audio = audio_st_index;
    header->cc_video = video_avctx;
    header->cc_audio = audio_avctx;
    header->codec_video = video_codec;
    header->codec_audio = audio_codec;
    return header;
}

AVHeader *OpenWrite(const char * url)
{
    AVFormatContext *ic = avformat_alloc_context();
    AVOutputFormat * output = av_guess_format(NULL,url,NULL);
    ic->oformat = output;
    
//    avformat_alloc_output_context2(&ic, NULL, NULL, url);
    int ret = avio_open(&ic->pb,url,AVIO_FLAG_READ_WRITE);
    if(ret < 0){
        return NULL;
    }
    
    AVHeader * header = avheader_new();
    header->fc = ic;
    return header;
}

bool CopyStream(AVHeader *dsc,AVHeader *src,int type)
{
    if(type == AVMEDIA_TYPE_VIDEO){
        if(src->st_video!= NULL){
            AVStream * st_src = src->st_video;
            if(dsc->cc_video != NULL){
                avcodec_free_context(&dsc->cc_video);
                dsc->cc_video = NULL;
            }
            AVStream * st = dsc->st_video;
            if(st == NULL){
                st = avformat_new_stream(dsc->fc, NULL);
            }
            
            AVCodecContext * ac = avcodec_alloc_context3(NULL);
            if (ac == NULL) {
                return false;
            }
            int ret = avcodec_parameters_to_context(ac, st_src->codecpar);
            if (ret < 0) {
                avcodec_free_context(&ac);
                return false;
            }
            /*if(ac->codec == NULL){
                ac->codec = avcodec_find_encoder(ac->codec_id);
            }*/
            if((ac->flags & CODEC_FLAG_GLOBAL_HEADER) != CODEC_FLAG_GLOBAL_HEADER)
            {
                dsc->fc->oformat->flags &= ~AVFMT_GLOBALHEADER;
            }

			avcodec_parameters_from_context(st->codecpar, ac);
			st->time_base = st_src->time_base;
			st->r_frame_rate = st_src->r_frame_rate;
			st->start_time = st_src->start_time;
			st->avg_frame_rate = st_src->avg_frame_rate;

            dsc->st_video = st;
            dsc->id_video = st->index;
            dsc->cc_video = ac;
            dsc->codec_video = (AVCodec*)ac->codec;
            //dsc->st_video->codec = ac;
			dsc->st_video->codecpar->codec_tag = 0;
        }
    }else if(type == AVMEDIA_TYPE_AUDIO){
        AVStream * st_src = src->st_audio;
        if(dsc->cc_audio != NULL){
            avcodec_free_context(&dsc->cc_audio);
            dsc->cc_audio = NULL;
        }
        AVStream * st = dsc->st_audio;
        if(st == NULL){
            st = avformat_new_stream(dsc->fc, NULL);
        }
        AVCodecContext * ac = avcodec_alloc_context3(NULL);
        if (ac == NULL) {
            return false;
        }
        int ret = avcodec_parameters_to_context(ac, st_src->codecpar);
        if (ret < 0) {
            avcodec_free_context(&ac);
            return false;
        }
        /*if(ac->codec == NULL){
            ac->codec = avcodec_find_encoder(ac->codec_id);
        }*/
        if((ac->flags & CODEC_FLAG_GLOBAL_HEADER) != CODEC_FLAG_GLOBAL_HEADER)
        {
            dsc->fc->oformat->flags &= ~AVFMT_GLOBALHEADER;
        }

		avcodec_parameters_from_context(st->codecpar, ac);
		st->time_base = st_src->time_base;
		st->r_frame_rate = st_src->r_frame_rate;
		st->start_time = st_src->start_time;
		st->avg_frame_rate = st_src->avg_frame_rate;

        dsc->st_audio = st;
        dsc->id_audio = st->index;
        dsc->cc_audio = ac;
        dsc->codec_audio = (AVCodec*)ac->codec;
        //dsc->st_audio->codec = ac;
		dsc->st_audio->codecpar->codec_tag = 0;
    }
    
    return true;
}

bool EVOReconductanceDone(const char *url , const char * outUrl)
{
    av_register_all();
    
    AVHeader * srcHeader = OpenRead(url);
    if(srcHeader == NULL){
        return false;
    }
    AVHeader * outHeader = OpenWrite(outUrl);
    if(outHeader == NULL){
        avheader_free(&srcHeader);
        avheader_free(&outHeader);
        return false;
    }
    CopyStream(outHeader, srcHeader, AVMEDIA_TYPE_VIDEO);
    CopyStream(outHeader, srcHeader, AVMEDIA_TYPE_AUDIO);
    
    int ret = avformat_write_header(outHeader->fc, NULL);
    if(ret != 0){
        avheader_free(&srcHeader);
        avheader_free(&outHeader);
        return false;
    }
    
    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);
    while(true){
        ret = av_read_frame(srcHeader->fc, pkt);
        if(ret != 0){
            break;
        }
        
        //adjust_packet(pkt);
        
        if(pkt->stream_index == srcHeader->id_video){
            pkt->stream_index = outHeader->id_video;
        }
        else if(pkt->stream_index == srcHeader->id_audio){
            pkt->stream_index = outHeader->id_audio;
        }else{
            pkt->stream_index = -1;
        }
        if(pkt->stream_index != -1){
            av_write_frame(outHeader->fc,pkt);
        }
        av_packet_unref(pkt);
    }
    av_write_trailer(outHeader->fc);
    av_packet_free(&pkt);
    
    avheader_free(&srcHeader);
    avheader_free(&outHeader);
    return true;
}
