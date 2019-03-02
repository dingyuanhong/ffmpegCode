#include "AudioDecoder.h"

#define LOGA printf

AudioDecoder::AudioDecoder(AVCodecContext	*codec)
	:AudioCodecCtx(codec), AudioConvertCtx(NULL), AudioInfo({0}), audio_frame_next_pts(AV_NOPTS_VALUE)
{
	AudioInfo.format = AV_SAMPLE_FMT_NONE;

	this->AudioFrame = av_frame_alloc();
	this->Packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	memset(this->Packet, 0, sizeof(AVPacket));
	av_init_packet(this->Packet);

	this->packet_count = 0;
}

AudioDecoder::~AudioDecoder()
{
	if (this->AudioConvertCtx != NULL) {
		swr_free(&this->AudioConvertCtx);
		this->AudioConvertCtx = NULL;
	}

	if (this->AudioFrame != NULL) {
		av_frame_free(&this->AudioFrame);
		this->AudioFrame = NULL;
	}
}

void AudioDecoder::Flush() {
	if (this->AudioCodecCtx != NULL) {
		avcodec_flush_buffers(this->AudioCodecCtx);
	}
}

int AudioDecoder::AudioResampling(AVFrame * audioDecodeFrame, uint8_t * audioBuffer, int bufferLen)
{
	if (!this->AudioConvertCtx) {
		int64_t decChannelLayout, tagChannelLayout;
		decChannelLayout = av_get_default_channel_layout(audioDecodeFrame->channels);
		tagChannelLayout = av_get_default_channel_layout(this->AudioCodecCtx->channels);
		if (AudioInfo.Channels > 0) 
		{
			tagChannelLayout = av_get_default_channel_layout(AudioInfo.Channels);
		}
		int tagsample_rate = this->AudioCodecCtx->sample_rate ;
		if (AudioInfo.SampleRate > 0)
		{
			tagsample_rate = AudioInfo.SampleRate;
		}
		AVSampleFormat tagSampleFormat = AV_SAMPLE_FMT_S16;
		if (AudioInfo.format != AV_SAMPLE_FMT_NONE)
		{
			tagSampleFormat = AudioInfo.format;
		}

		this->AudioConvertCtx = swr_alloc_set_opts(this->AudioConvertCtx,
			tagChannelLayout,
			tagSampleFormat, //AV_SAMPLE_FMT_S16,
			tagsample_rate,
			decChannelLayout,
			(AVSampleFormat)(audioDecodeFrame->format), audioDecodeFrame->sample_rate, 0, NULL);

		if (!this->AudioConvertCtx || swr_init(this->AudioConvertCtx) < 0) {

		}
	}

	swr_convert(this->AudioConvertCtx, &audioBuffer, bufferLen, (const uint8_t **)(audioDecodeFrame->data), audioDecodeFrame->nb_samples);
	return 1;
}

int AudioDecoder::DecodeFrame(EvoFrame *packet, AVFrame **evoResult)
{
	if (packet == NULL || packet->size == 0)
	{
		return DecodePacket(NULL, evoResult);
	}
	else
	{
		int ret = av_new_packet(this->Packet, packet->size);

		if (ret == 0) {
			memcpy(this->Packet->data, packet->data, packet->size);
			Packet->pts = packet->pts;
			Packet->dts = packet->dts;
			Packet->pos = packet->timestamp;
			Packet->flags = packet->flags;
			ret = DecodePacket(Packet, evoResult);
		}

		av_packet_unref(this->Packet);
		return ret;
	}
}

int AudioDecoder::DecodePacket(AVPacket *packet, AVFrame **evoResult)
{
	int gotFrame = 0;

	if (evoResult != NULL)
	{
		*evoResult = NULL;
	}
#ifndef USE_NEW_API
	if (packet == NULL) {
		return 0;
	}

	int decoded = avcodec_decode_audio4(this->AudioCodecCtx, AudioFrame, &gotFrame, packet);
	if (decoded < 0) {
		if (decoded == AVERROR(EAGAIN)) return 0;
		char errbuf[1024] = { 0 };
		av_strerror(decoded, errbuf, 1024);
		LOGA("VideoDecoder::DecodePacket:avcodec_decode_video2:%d(%s).\n", decoded, errbuf);
		if (decoded == AVERROR_INVALIDDATA) return 0;
		if (decoded == AVERROR_EOF) return -1;
		if (decoded == AVERROR(EINVAL)) return -1;
		if (AVERROR(ENOMEM)) return -1;
		return -1;
	}
#else
	int decoded = 0;
	if (packet != NULL)
	{
		decoded = avcodec_send_packet(this->AudioCodecCtx, packet);
		if (decoded < 0)
		{
			if (decoded == AVERROR(EAGAIN)) return 0;
			char errbuf[1024] = { 0 };
			av_strerror(decoded, errbuf, 1024);
			LOGA("AudioDecoder::DecodePacket:avcodec_send_packet:%d(%s).\n", decoded, errbuf);
			if (decoded == AVERROR_EOF) return -1;
			if (decoded == AVERROR(EINVAL)) return -1;
			if (AVERROR(ENOMEM)) return -1;
			return -1;
		}
	}

	decoded = avcodec_receive_frame(this->AudioCodecCtx, this->AudioFrame);
	if (decoded < 0)
	{
		if (decoded == AVERROR(EAGAIN)) return 0;
		char errbuf[1024] = { 0 };
		av_strerror(decoded, errbuf, 1024);
		LOGA("AudioDecoder::DecodePacket:avcodec_receive_frame:%d(%s).\n", decoded, errbuf);
		if (decoded == AVERROR_EOF) return -1;
		if (decoded == AVERROR(EINVAL)) return -1;
		return -1;
	}
	else
	{
		gotFrame = 1;
	}
#endif
	if (gotFrame) {
		EvoAudioInfo audioInfo = GetCorrectTargetInfo();
		AVFrame * audioFrame = CreateAVFrame(audioInfo, AudioFrame, 1);
		if (audioFrame != NULL)
		{
			AVFrame * frame = audioFrame;
			
			int ret = this->AudioResampling(this->AudioFrame, frame->data[0], frame->pkt_size);
			frame->pts = this->AudioFrame->pts;
#ifndef USE_NEW_API
			frame->pkt_pts = this->AudioFrame->pkt_pts;
#endif
			frame->pkt_dts = this->AudioFrame->pkt_dts;
			/*frame->nb_samples = this->AudioFrame->nb_samples;
			frame->channels = this->AudioFrame->channels;
			frame->channel_layout = this->AudioFrame->channel_layout;
			frame->format = AV_SAMPLE_FMT_S16;
			frame->sample_rate = this->AudioCodecCtx->sample_rate;*/

			int64_t pts = frame->pts == AV_NOPTS_VALUE? 
#ifndef USE_NEW_API
				frame->pkt_pts == AV_NOPTS_VALUE ?
#endif
				frame ->pkt_dts == AV_NOPTS_VALUE ? 
				AV_NOPTS_VALUE 
				: frame->pkt_dts
#ifndef USE_NEW_API
				: frame->pkt_pts
#endif
				: frame->pts ;
			int64_t timeStamp = GetAudioTimeStamp(AudioCodecCtx->time_base, pts, frame->nb_samples,frame->sample_rate);

			if (evoResult != NULL)
			{
				*evoResult = audioFrame;
			}
			else
			{
				FreeAVFrame(&audioFrame);
			}
		}
		else
		{
			av_frame_unref(this->AudioFrame);
			return -1;
		}
		av_frame_unref(this->AudioFrame);
		return 1;
	}
	else {
		packet_count++;
	}
	return 0;
}

int AudioDecoder::DecodePacket(AVFrame **evoResult)
{
	int gotFrame = 0;

	if (evoResult != NULL)
	{
		*evoResult = NULL;
	}
#ifndef USE_NEW_API
	AVPacket packet;
	av_init_packet(&packet);
	packet.size = 0;
	packet.buf = NULL;

	int decoded = avcodec_decode_audio4(this->AudioCodecCtx, AudioFrame, &gotFrame, &packet);
	if (decoded < 0) {
		if (decoded == AVERROR(EAGAIN)) return 0;
		char errbuf[1024] = { 0 };
		av_strerror(decoded, errbuf, 1024);
		LOGA("VideoDecoder::DecodePacket:avcodec_decode_video2:%d(%s).\n", decoded, errbuf);
		if (decoded == AVERROR_INVALIDDATA) return 0;
		if (decoded == AVERROR_EOF) return -1;
		if (decoded == AVERROR(EINVAL)) return -1;
		if (AVERROR(ENOMEM)) return -1;
		return -1;
	}
#else
	int decoded = 0;

	decoded = avcodec_receive_frame(this->AudioCodecCtx, this->AudioFrame);
	if (decoded < 0)
	{
		if (decoded == AVERROR(EAGAIN)) return 0;
		char errbuf[1024] = { 0 };
		av_strerror(decoded, errbuf, 1024);
		LOGA("AudioDecoder::DecodePacket:avcodec_receive_frame:%d(%s).\n", decoded, errbuf);
		if (decoded == AVERROR_EOF) return -1;
		if (decoded == AVERROR(EINVAL)) return -1;
		return -1;
	}
	else
	{
		gotFrame = 1;
	}
#endif
	if (gotFrame) {
		packet_count--;
		EvoAudioInfo audioInfo = GetCorrectTargetInfo();
		AVFrame * audioFrame = CreateAVFrame(audioInfo, AudioFrame, 1);
		if (audioFrame != NULL)
		{
			AVFrame * frame = audioFrame;

			int ret = this->AudioResampling(this->AudioFrame, frame->data[0], frame->pkt_size);
			frame->pts = this->AudioFrame->pts;
#ifndef USE_NEW_API
			frame->pkt_pts = this->AudioFrame->pkt_pts;
#endif
			frame->pkt_dts = this->AudioFrame->pkt_dts;
			/*frame->nb_samples = this->AudioFrame->nb_samples;
			frame->channels = this->AudioFrame->channels;
			frame->channel_layout = this->AudioFrame->channel_layout;
			frame->format = AV_SAMPLE_FMT_S16;
			frame->sample_rate = this->AudioCodecCtx->sample_rate;*/

			int64_t pts = frame->pts == AV_NOPTS_VALUE ?
#ifndef USE_NEW_API
				frame->pkt_pts == AV_NOPTS_VALUE ?
#endif
				frame->pkt_dts == AV_NOPTS_VALUE ?
				AV_NOPTS_VALUE
				: frame->pkt_dts
#ifndef USE_NEW_API
				: frame->pkt_pts
#endif
				: frame->pts;
			int64_t timeStamp = GetAudioTimeStamp(AudioCodecCtx->time_base, pts, frame->nb_samples, frame->sample_rate);

			if (evoResult != NULL)
			{
				*evoResult = audioFrame;
			}
			else
			{
				FreeAVFrame(&audioFrame);
			}
		}
		else
		{
			av_frame_unref(this->AudioFrame);
			return -1;
		}
		av_frame_unref(this->AudioFrame);
		return 1;
	}
	return packet_count;
}

AVFrame* AudioDecoder::Decode(AVPacket *packet)
{
	int gotFrame = 0;
#ifndef USE_NEW_API
	if (packet == NULL) {
		return NULL;
	}

	int decoded = avcodec_decode_audio4(this->AudioCodecCtx, AudioFrame, &gotFrame, packet);
	if (decoded < 0) {
		if (decoded == AVERROR(EAGAIN)) return NULL;
		char errbuf[1024] = { 0 };
		av_strerror(decoded, errbuf, 1024);
		LOGA("VideoDecoder::DecodePacket:avcodec_decode_video2:%d(%s).\n", decoded, errbuf);
		if (decoded == AVERROR_INVALIDDATA) return NULL;
		if (decoded == AVERROR_EOF) return NULL;
		if (decoded == AVERROR(EINVAL)) return NULL;
		if (AVERROR(ENOMEM)) return NULL;
		return NULL;
	}
#else
	int decoded = 0;
	if (packet != NULL)
	{
		decoded = avcodec_send_packet(this->AudioCodecCtx, packet);
		if (decoded < 0)
		{
			return NULL;
		}
	}

	decoded = avcodec_receive_frame(this->AudioCodecCtx, this->AudioFrame);
	if (decoded < 0)
	{
		return NULL;
	}
	else
	{
		gotFrame = 1;
	}
#endif
	if (gotFrame) {
		EvoAudioInfo audioInfo = GetCorrectTargetInfo();
		AVFrame * audioFrame = CreateAVFrame(audioInfo, AudioFrame, 1);
		if (audioFrame != NULL)
		{
			AVFrame * frame = audioFrame;

			int ret = this->AudioResampling(this->AudioFrame, frame->data[0], frame->pkt_size);

			frame->pts = this->AudioFrame->pts;
#ifndef USE_NEW_API
			frame->pkt_pts = this->AudioFrame->pkt_pts;
#endif
			frame->pkt_dts = this->AudioFrame->pkt_dts;
			/*frame->nb_samples = this->AudioFrame->nb_samples;
			frame->channels = this->AudioFrame->channels;
			frame->channel_layout = this->AudioFrame->channel_layout;
			frame->format = AV_SAMPLE_FMT_S16;
			frame->sample_rate = this->AudioCodecCtx->sample_rate;*/

			return audioFrame;
		}
		else
		{
			packet_count++;
			return NULL;
		}
	}
	return NULL;
}

AVFrame* AudioDecoder::CreateAVFrame(EvoAudioInfo audioInfo, AVFrame *Frame,int align)
{
	int channels = audioInfo.Channels;
	int nb_samples = audioInfo.nb_samples;
	AVSampleFormat format = audioInfo.format;
	if (channels <= 0)
	{
		channels = Frame->channels;
	}
	if (nb_samples <= 0)
	{
		nb_samples = Frame->nb_samples;
	}
	if (format == AV_SAMPLE_FMT_NONE)
	{
		format = (AVSampleFormat)Frame->format;
	}

	AVFrame * frame = av_frame_alloc();
	int DataSize = av_samples_get_buffer_size(NULL, channels, nb_samples, format, align);
	av_samples_alloc(frame->data, frame->linesize,
		channels, nb_samples, format, align);
	frame->format = format;
	frame->channels = channels;
	frame->nb_samples = nb_samples;
	frame->sample_rate = audioInfo.SampleRate;
	if (frame->sample_rate <= 0)
	{
		frame->sample_rate = frame->sample_rate;
	}
	frame->pkt_size = DataSize;

	return frame;
}

void AudioDecoder::FreeAVFrame(AVFrame** frame)
{
	if (frame == NULL) return;
	if (*frame == NULL) return;
	av_frame_free(frame);
	*frame = NULL;
}

EvoAudioInfo AudioDecoder::GetTargetInfo()
{
	return AudioInfo;
}

void AudioDecoder::SetTargetInfo(const EvoAudioInfo info)
{
	AudioInfo = info;
}

EvoAudioInfo AudioDecoder::GetCorrectTargetInfo()
{
	EvoAudioInfo info = AudioInfo;
	if (this->AudioCodecCtx != NULL)
	{
		if(info.BitSize <= 0) info.BitSize = AudioCodecCtx->bit_rate;
		if (info.Channels <= 0) info.Channels = AudioCodecCtx->channels;
		if (info.format == AV_SAMPLE_FMT_NONE) info.format = AudioCodecCtx->sample_fmt;
		if (info.SampleRate <= 0) info.SampleRate = AudioCodecCtx->sample_rate;
	}
	return info;
}

int64_t AudioDecoder::GetAudioTimeStamp(AVRational time_base, int64_t pts, int nb_samples, int SampleRate)
{
	AVRational tb = { 0 };
	tb.num = 1;
	tb.den = SampleRate;

	if (pts != AV_NOPTS_VALUE)
	{
		pts = av_rescale_q(pts, time_base, tb);
	}
	else if (audio_frame_next_pts != AV_NOPTS_VALUE)
	{
		AVRational AVR_tmp = { 0 };
		AVR_tmp.num = 1;
		AVR_tmp.den = SampleRate;
		pts = av_rescale_q(audio_frame_next_pts, AVR_tmp, tb);
	}
	if (pts != AV_NOPTS_VALUE) {
		audio_frame_next_pts = pts + (double)(nb_samples * 1000)/ SampleRate;
	}
	return pts;
}