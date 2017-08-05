#include "find_stream_info.h"



#define AV_INPUT_BUFFER_PADDING_SIZE 32
#define AV_CODEC_CAP_CHANNEL_CONF        (1 << 10)

#define FF_MAX_EXTRADATA_SIZE ((1 << 28) - AV_INPUT_BUFFER_PADDING_SIZE)

#define RELATIVE_TS_BASE (INT64_MAX - (1LL<<48))

#if HAVE_PRAGMA_DEPRECATED
#    if defined(__ICL) || defined (__INTEL_COMPILER)
#        define FF_DISABLE_DEPRECATION_WARNINGS __pragma(warning(push)) __pragma(warning(disable:1478))
#        define FF_ENABLE_DEPRECATION_WARNINGS  __pragma(warning(pop))
#    elif defined(_MSC_VER)
#        define FF_DISABLE_DEPRECATION_WARNINGS __pragma(warning(push)) __pragma(warning(disable:4996))
#        define FF_ENABLE_DEPRECATION_WARNINGS  __pragma(warning(pop))
#    else
#        define FF_DISABLE_DEPRECATION_WARNINGS _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#        define FF_ENABLE_DEPRECATION_WARNINGS  _Pragma("GCC diagnostic warning \"-Wdeprecated-declarations\"")
#    endif
#else
#    define FF_DISABLE_DEPRECATION_WARNINGS
#    define FF_ENABLE_DEPRECATION_WARNINGS
#endif

#ifdef _WIN32

#ifdef AV_TIME_BASE_Q
#undef AV_TIME_BASE_Q
#define AV_TIME_BASE_Q { 1, AV_TIME_BASE }
#endif

#endif

static int is_relative(int64_t ts) {
	return (int)(ts > (RELATIVE_TS_BASE - (1LL << 48)));
}

/**
* Wrap a given time stamp, if there is an indication for an overflow
*
* @param st stream
* @param timestamp the time stamp to wrap
* @return resulting time stamp
*/
static int64_t wrap_timestamp(AVStream *st, int64_t timestamp)
{
	if (st->pts_wrap_behavior != AV_PTS_WRAP_IGNORE &&
		st->pts_wrap_reference != AV_NOPTS_VALUE && timestamp != AV_NOPTS_VALUE) {
		if (st->pts_wrap_behavior == AV_PTS_WRAP_ADD_OFFSET &&
			timestamp < st->pts_wrap_reference)
			return timestamp + (1ULL << st->pts_wrap_bits);
		else if (st->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET &&
			timestamp >= st->pts_wrap_reference)
			return timestamp - (1ULL << st->pts_wrap_bits);
	}
	return timestamp;
}

static int ff_copy_whitelists(AVFormatContext *dst, AVFormatContext *src)
{
	av_assert0(!dst->codec_whitelist && !dst->format_whitelist);
	dst->codec_whitelist = av_strdup(src->codec_whitelist);
	dst->format_whitelist = av_strdup(src->format_whitelist);
	if ((src->codec_whitelist && !dst->codec_whitelist)
		|| (src->format_whitelist && !dst->format_whitelist)) {
		av_log(dst, AV_LOG_ERROR, "Failed to duplicate whitelist\n");
		return AVERROR(ENOMEM);
	}
	return 0;
}

static const AVCodec *find_decoder(AVFormatContext *s, AVStream *st, enum AVCodecID codec_id)
{
	if (st->codec->codec)
		return st->codec->codec;

	switch (st->codec->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		if (s->video_codec)    return s->video_codec;
		break;
	case AVMEDIA_TYPE_AUDIO:
		if (s->audio_codec)    return s->audio_codec;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		if (s->subtitle_codec) return s->subtitle_codec;
		break;
	}

	return avcodec_find_decoder(codec_id);
}

/* an arbitrarily chosen "sane" max packet size -- 50M */
#define SANE_CHUNK_SIZE (50000000)

static int ffio_limit(AVIOContext *s, int size)
{
	if (s->maxsize >= 0) {
		int64_t remaining = s->maxsize - avio_tell(s);
		if (remaining < size) {
			int64_t newsize = avio_size(s);
			if (!s->maxsize || s->maxsize<newsize)
				s->maxsize = newsize - !newsize;
			remaining = s->maxsize - avio_tell(s);
			remaining = FFMAX(remaining, 0);
		}

		if (s->maxsize >= 0 && remaining + 1 < size) {
			av_log(NULL, remaining ? AV_LOG_ERROR : AV_LOG_DEBUG, "Truncating packet of size %d to %lld" "\n", size, remaining + 1);
			size = remaining + 1;
		}
	}
	return size;
}

/* Read the data in sane-sized chunks and append to pkt.
* Return the number of bytes read or an error. */
static int append_packet_chunked(AVIOContext *s, AVPacket *pkt, int size)
{
	int64_t orig_pos = pkt->pos; // av_grow_packet might reset pos
	int orig_size = pkt->size;
	int ret;

	do {
		int prev_size = pkt->size;
		int read_size;

		/* When the caller requests a lot of data, limit it to the amount
		* left in file or SANE_CHUNK_SIZE when it is not known. */
		read_size = size;
		if (read_size > SANE_CHUNK_SIZE / 10) {
			read_size = ffio_limit(s, read_size);
			// If filesize/maxsize is unknown, limit to SANE_CHUNK_SIZE
			if (s->maxsize < 0)
				read_size = FFMIN(read_size, SANE_CHUNK_SIZE);
		}

		ret = av_grow_packet(pkt, read_size);
		if (ret < 0)
			break;

		ret = avio_read(s, pkt->data + prev_size, read_size);
		if (ret != read_size) {
			av_shrink_packet(pkt, prev_size + FFMAX(ret, 0));
			break;
		}

		size -= read_size;
	} while (size > 0);
	if (size > 0)
		pkt->flags |= AV_PKT_FLAG_CORRUPT;

	pkt->pos = orig_pos;
	if (!pkt->size)
		av_free_packet(pkt);
	return pkt->size > orig_size ? pkt->size - orig_size : ret;
}

static int set_codec_from_probe_data(AVFormatContext *s, AVStream *st,
	AVProbeData *pd)
{
	static const struct {
		const char *name;
		enum AVCodecID id;
		enum AVMediaType type;
	} fmt_id_type[] = {
		{ "aac",       AV_CODEC_ID_AAC,        AVMEDIA_TYPE_AUDIO },
		{ "ac3",       AV_CODEC_ID_AC3,        AVMEDIA_TYPE_AUDIO },
		{ "dts",       AV_CODEC_ID_DTS,        AVMEDIA_TYPE_AUDIO },
		{ "dvbsub",    AV_CODEC_ID_DVB_SUBTITLE,AVMEDIA_TYPE_SUBTITLE },
		{ "eac3",      AV_CODEC_ID_EAC3,       AVMEDIA_TYPE_AUDIO },
		{ "h264",      AV_CODEC_ID_H264,       AVMEDIA_TYPE_VIDEO },
		{ "hevc",      AV_CODEC_ID_HEVC,       AVMEDIA_TYPE_VIDEO },
		{ "loas",      AV_CODEC_ID_AAC_LATM,   AVMEDIA_TYPE_AUDIO },
		{ "m4v",       AV_CODEC_ID_MPEG4,      AVMEDIA_TYPE_VIDEO },
		{ "mp3",       AV_CODEC_ID_MP3,        AVMEDIA_TYPE_AUDIO },
		{ "mpegvideo", AV_CODEC_ID_MPEG2VIDEO, AVMEDIA_TYPE_VIDEO },
		{ 0 }
	};
	int score;
	AVInputFormat *fmt = av_probe_input_format3(pd, 1, &score);

	if (fmt && st->request_probe <= score) {
		int i;
		av_log(s, AV_LOG_DEBUG,
			"Probe with size=%d, packets=%d detected %s with score=%d\n",
			pd->buf_size, MAX_PROBE_PACKETS - st->probe_packets,
			fmt->name, score);
		for (i = 0; fmt_id_type[i].name; i++) {
			if (!strcmp(fmt->name, fmt_id_type[i].name)) {
				st->codec->codec_id = fmt_id_type[i].id;
				st->codec->codec_type = fmt_id_type[i].type;
				return score;
			}
		}
	}
	return 0;
}

/* Open input file and probe the format if necessary. */
static int init_input(AVFormatContext *s, const char *filename,
	AVDictionary **options)
{
	int ret;
	AVProbeData pd = { filename, NULL, 0 };
	int score = AVPROBE_SCORE_RETRY;

	if (s->pb) {
		s->flags |= AVFMT_FLAG_CUSTOM_IO;
		if (!s->iformat)
			return av_probe_input_buffer2(s->pb, &s->iformat, filename,
				s, 0, s->format_probesize);
		else if (s->iformat->flags & AVFMT_NOFILE)
			av_log(s, AV_LOG_WARNING, "Custom AVIOContext makes no sense and "
				"will be ignored with AVFMT_NOFILE format.\n");
		return 0;
	}

	if ((s->iformat && s->iformat->flags & AVFMT_NOFILE) ||
		(!s->iformat && (s->iformat = av_probe_input_format2(&pd, 0, &score))))
		return score;

	if ((ret = avio_open2(&s->pb, filename, AVIO_FLAG_READ | s->avio_flags,
		&s->interrupt_callback, options)) < 0)
		return ret;
	if (s->iformat)
		return 0;
	return av_probe_input_buffer2(s->pb, &s->iformat, filename,
		s, 0, s->format_probesize);
}

static AVPacket *add_to_pktbuf(AVPacketList **packet_buffer, AVPacket *pkt,
	AVPacketList **plast_pktl)
{
	AVPacketList *pktl = (AVPacketList*)av_mallocz(sizeof(AVPacketList));
	if (!pktl)
		return NULL;

	if (*packet_buffer)
		(*plast_pktl)->next = pktl;
	else
		*packet_buffer = pktl;

	/* Add the packet in the buffered packet list. */
	*plast_pktl = pktl;
	pktl->pkt = *pkt;
	return &pktl->pkt;
}

/*******************************************************/

static void force_codec_ids(AVFormatContext *s, AVStream *st)
{
	switch (st->codec->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		if (s->video_codec_id)
			st->codec->codec_id = s->video_codec_id;
		break;
	case AVMEDIA_TYPE_AUDIO:
		if (s->audio_codec_id)
			st->codec->codec_id = s->audio_codec_id;
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		if (s->subtitle_codec_id)
			st->codec->codec_id = s->subtitle_codec_id;
		break;
	}
}

static int probe_codec(AVFormatContext *s, AVStream *st, const AVPacket *pkt)
{
	if (st->request_probe>0) {
		AVProbeData *pd = &st->probe_data;
		int end;
		av_log(s, AV_LOG_DEBUG, "probing stream %d pp:%d\n", st->index, st->probe_packets);
		--st->probe_packets;

		if (pkt) {
			uint8_t *new_buf = (uint8_t*)av_realloc(pd->buf, pd->buf_size + pkt->size + AVPROBE_PADDING_SIZE);
			if (!new_buf) {
				av_log(s, AV_LOG_WARNING,
					"Failed to reallocate probe buffer for stream %d\n",
					st->index);
				goto no_packet;
			}
			pd->buf = new_buf;
			memcpy(pd->buf + pd->buf_size, pkt->data, pkt->size);
			pd->buf_size += pkt->size;
			memset(pd->buf + pd->buf_size, 0, AVPROBE_PADDING_SIZE);
		}
		else {
		no_packet:
			st->probe_packets = 0;
			if (!pd->buf_size) {
				av_log(s, AV_LOG_WARNING,
					"nothing to probe for stream %d\n", st->index);
			}
		}

		end = s->internal->raw_packet_buffer_remaining_size <= 0
			|| st->probe_packets <= 0;

		if (end || av_log2(pd->buf_size) != av_log2(pd->buf_size - pkt->size)) {
			int score = set_codec_from_probe_data(s, st, pd);
			if ((st->codec->codec_id != AV_CODEC_ID_NONE && score > AVPROBE_SCORE_STREAM_RETRY)
				|| end) {
				pd->buf_size = 0;
				av_freep(&pd->buf);
				st->request_probe = -1;
				if (st->codec->codec_id != AV_CODEC_ID_NONE) {
					av_log(s, AV_LOG_DEBUG, "probed stream %d\n", st->index);
				}
				else
					av_log(s, AV_LOG_WARNING, "probed stream %d failed\n", st->index);
			}
			force_codec_ids(s, st);
		}
	}
	return 0;
}

static int update_wrap_reference(AVFormatContext *s, AVStream *st, int stream_index, AVPacket *pkt)
{
	int64_t ref = pkt->dts;
	int i, pts_wrap_behavior;
	int64_t pts_wrap_reference;
	AVProgram *first_program;

	if (ref == AV_NOPTS_VALUE)
		ref = pkt->pts;
	if (st->pts_wrap_reference != AV_NOPTS_VALUE || st->pts_wrap_bits >= 63 || ref == AV_NOPTS_VALUE || !s->correct_ts_overflow)
		return 0;
	ref &= (1LL << st->pts_wrap_bits) - 1;

	// reference time stamp should be 60 s before first time stamp
	pts_wrap_reference = ref - av_rescale(60, st->time_base.den, st->time_base.num);
	// if first time stamp is not more than 1/8 and 60s before the wrap point, subtract rather than add wrap offset
	pts_wrap_behavior = (ref < (1LL << st->pts_wrap_bits) - (1LL << st->pts_wrap_bits - 3)) ||
		(ref < (1LL << st->pts_wrap_bits) - av_rescale(60, st->time_base.den, st->time_base.num)) ?
		AV_PTS_WRAP_ADD_OFFSET : AV_PTS_WRAP_SUB_OFFSET;

	first_program = av_find_program_from_stream(s, NULL, stream_index);

	if (!first_program) {
		int default_stream_index = av_find_default_stream_index(s);
		if (s->streams[default_stream_index]->pts_wrap_reference == AV_NOPTS_VALUE) {
			for (i = 0; i < s->nb_streams; i++) {
				if (av_find_program_from_stream(s, NULL, i))
					continue;
				s->streams[i]->pts_wrap_reference = pts_wrap_reference;
				s->streams[i]->pts_wrap_behavior = pts_wrap_behavior;
			}
		}
		else {
			st->pts_wrap_reference = s->streams[default_stream_index]->pts_wrap_reference;
			st->pts_wrap_behavior = s->streams[default_stream_index]->pts_wrap_behavior;
		}
	}
	else {
		AVProgram *program = first_program;
		while (program) {
			if (program->pts_wrap_reference != AV_NOPTS_VALUE) {
				pts_wrap_reference = program->pts_wrap_reference;
				pts_wrap_behavior = program->pts_wrap_behavior;
				break;
			}
			program = av_find_program_from_stream(s, program, stream_index);
		}

		// update every program with differing pts_wrap_reference
		program = first_program;
		while (program) {
			if (program->pts_wrap_reference != pts_wrap_reference) {
				for (i = 0; i<program->nb_stream_indexes; i++) {
					s->streams[program->stream_index[i]]->pts_wrap_reference = pts_wrap_reference;
					s->streams[program->stream_index[i]]->pts_wrap_behavior = pts_wrap_behavior;
				}

				program->pts_wrap_reference = pts_wrap_reference;
				program->pts_wrap_behavior = pts_wrap_behavior;
			}
			program = av_find_program_from_stream(s, program, stream_index);
		}
	}
	return 1;
}

static int ff_read_packet(AVFormatContext *s, AVPacket *pkt)
{
	int ret, i, err;
	AVStream *st;

	for (;;) {
		AVPacketList *pktl = s->internal->raw_packet_buffer;

		if (pktl) {
			*pkt = pktl->pkt;
			st = s->streams[pkt->stream_index];
			if (s->internal->raw_packet_buffer_remaining_size <= 0)
				if ((err = probe_codec(s, st, NULL)) < 0)
					return err;
			if (st->request_probe <= 0) {
				s->internal->raw_packet_buffer = pktl->next;
				s->internal->raw_packet_buffer_remaining_size += pkt->size;
				av_free(pktl);
				return 0;
			}
		}

		pkt->data = NULL;
		pkt->size = 0;
		av_init_packet(pkt);
		ret = s->iformat->read_packet(s, pkt);
		if (ret < 0) {
			if (!pktl || ret == AVERROR(EAGAIN))
				return ret;
			for (i = 0; i < s->nb_streams; i++) {
				st = s->streams[i];
				if (st->probe_packets || st->request_probe > 0)
					if ((err = probe_codec(s, st, NULL)) < 0)
						return err;
				av_assert0(st->request_probe <= 0);
			}
			continue;
		}

		if ((s->flags & AVFMT_FLAG_DISCARD_CORRUPT) &&
			(pkt->flags & AV_PKT_FLAG_CORRUPT)) {
			av_log(s, AV_LOG_WARNING,
				"Dropped corrupted packet (stream = %d)\n",
				pkt->stream_index);
			av_free_packet(pkt);
			continue;
		}

		if (pkt->stream_index >= (unsigned)s->nb_streams) {
			av_log(s, AV_LOG_ERROR, "Invalid stream index %d\n", pkt->stream_index);
			continue;
		}

		st = s->streams[pkt->stream_index];

		if (update_wrap_reference(s, st, pkt->stream_index, pkt) && st->pts_wrap_behavior == AV_PTS_WRAP_SUB_OFFSET) {
			// correct first time stamps to negative values
			if (!is_relative(st->first_dts))
				st->first_dts = wrap_timestamp(st, st->first_dts);
			if (!is_relative(st->start_time))
				st->start_time = wrap_timestamp(st, st->start_time);
			if (!is_relative(st->cur_dts))
				st->cur_dts = wrap_timestamp(st, st->cur_dts);
		}

		pkt->dts = wrap_timestamp(st, pkt->dts);
		pkt->pts = wrap_timestamp(st, pkt->pts);

		force_codec_ids(s, st);

		/* TODO: audio: time filter; video: frame reordering (pts != dts) */
		if (s->use_wallclock_as_timestamps) {
			pkt->dts = pkt->pts = av_rescale_q(av_gettime(), AV_TIME_BASE_Q, st->time_base);
		}
			

		if (!pktl && st->request_probe <= 0)
			return ret;

		add_to_pktbuf(&s->internal->raw_packet_buffer, pkt,
			&s->internal->raw_packet_buffer_end);
		s->internal->raw_packet_buffer_remaining_size -= pkt->size;

		if ((err = probe_codec(s, st, pkt)) < 0)
			return err;
	}
}


/**********************************************************/

static int determinable_frame_size(AVCodecContext *avctx)
{
	if (/*avctx->codec_id == AV_CODEC_ID_AAC ||*/
		avctx->codec_id == AV_CODEC_ID_MP1 ||
		avctx->codec_id == AV_CODEC_ID_MP2 ||
		avctx->codec_id == AV_CODEC_ID_MP3/* ||
										  avctx->codec_id == AV_CODEC_ID_CELT*/)
		return 1;
	return 0;
}

/**
* Return the frame duration in seconds. Return 0 if not available.
*/
static void ff_compute_frame_duration(AVFormatContext *s, int *pnum, int *pden, AVStream *st,
	AVCodecParserContext *pc, AVPacket *pkt)
{
	AVRational codec_framerate = s->iformat ? st->codec->framerate :
		av_mul_q(av_inv_q(st->codec->time_base), { 1, st->codec->ticks_per_frame });
	int frame_size;

	*pnum = 0;
	*pden = 0;
	switch (st->codec->codec_type) {
	case AVMEDIA_TYPE_VIDEO:
		if (st->r_frame_rate.num && !pc && s->iformat) {
			*pnum = st->r_frame_rate.den;
			*pden = st->r_frame_rate.num;
		}
		else if (st->time_base.num * 1000LL > st->time_base.den) {
			*pnum = st->time_base.num;
			*pden = st->time_base.den;
		}
		else if (codec_framerate.den * 1000LL > codec_framerate.num) {
			av_assert0(st->codec->ticks_per_frame);
			av_reduce(pnum, pden,
				codec_framerate.den,
				codec_framerate.num * (int64_t)st->codec->ticks_per_frame,
				INT_MAX);

			if (pc && pc->repeat_pict) {
				av_assert0(s->iformat); // this may be wrong for interlaced encoding but its not used for that case
				av_reduce(pnum, pden,
					(*pnum) * (1LL + pc->repeat_pict),
					(*pden),
					INT_MAX);
			}
			/* If this codec can be interlaced or progressive then we need
			* a parser to compute duration of a packet. Thus if we have
			* no parser in such case leave duration undefined. */
			if (st->codec->ticks_per_frame > 1 && !pc)
				*pnum = *pden = 0;
		}
		break;
	case AVMEDIA_TYPE_AUDIO:
		frame_size = av_get_audio_frame_duration(st->codec, pkt->size);
		if (frame_size <= 0 || st->codec->sample_rate <= 0)
			break;
		*pnum = frame_size;
		*pden = st->codec->sample_rate;
		break;
	default:
		break;
	}
}

static int is_intra_only(AVCodecContext *enc) {
	const AVCodecDescriptor *desc;

	if (enc->codec_type != AVMEDIA_TYPE_VIDEO)
		return 1;

	desc = av_codec_get_codec_descriptor(enc);
	if (!desc) {
		desc = avcodec_descriptor_get(enc->codec_id);
		av_codec_set_codec_descriptor(enc, desc);
	}
	if (desc)
		return !!(desc->props & AV_CODEC_PROP_INTRA_ONLY);
	return 0;
}

static int has_decode_delay_been_guessed(AVStream *st)
{
	if (st->codec->codec_id != AV_CODEC_ID_H264) return 1;
	if (!st->info) // if we have left find_stream_info then nb_decoded_frames won't increase anymore for stream copy
		return 1;
#if CONFIG_H264_DECODER
	if (st->codec->has_b_frames &&
		avpriv_h264_has_num_reorder_frames(st->codec) == st->codec->has_b_frames)
		return 1;
#endif
	if (st->codec->has_b_frames<3)
		return st->nb_decoded_frames >= 7;
	else if (st->codec->has_b_frames<4)
		return st->nb_decoded_frames >= 18;
	else
		return st->nb_decoded_frames >= 20;
}

static AVPacketList *get_next_pkt(AVFormatContext *s, AVStream *st, AVPacketList *pktl)
{
	if (pktl->next)
		return pktl->next;
	if (pktl == s->internal->packet_buffer_end)
		return s->internal->parse_queue;
	return NULL;
}

static int64_t select_from_pts_buffer(AVStream *st, int64_t *pts_buffer, int64_t dts) {
	int onein_oneout = st->codec->codec_id != AV_CODEC_ID_H264 &&
		st->codec->codec_id != AV_CODEC_ID_HEVC;

	if (!onein_oneout) {
		int delay = st->codec->has_b_frames;
		int i;

		if (dts == AV_NOPTS_VALUE) {
			int64_t best_score = INT64_MAX;
			for (i = 0; i<delay; i++) {
				if (st->pts_reorder_error_count[i]) {
					int64_t score = st->pts_reorder_error[i] / st->pts_reorder_error_count[i];
					if (score < best_score) {
						best_score = score;
						dts = pts_buffer[i];
					}
				}
			}
		}
		else {
			for (i = 0; i<delay; i++) {
				if (pts_buffer[i] != AV_NOPTS_VALUE) {
					int64_t diff = FFABS(pts_buffer[i] - dts)
						+ (uint64_t)st->pts_reorder_error[i];
					diff = FFMAX(diff, st->pts_reorder_error[i]);
					st->pts_reorder_error[i] = diff;
					st->pts_reorder_error_count[i]++;
					if (st->pts_reorder_error_count[i] > 250) {
						st->pts_reorder_error[i] >>= 1;
						st->pts_reorder_error_count[i] >>= 1;
					}
				}
			}
		}
	}

	if (dts == AV_NOPTS_VALUE)
		dts = pts_buffer[0];

	return dts;
}

static void update_initial_timestamps(AVFormatContext *s, int stream_index,
	int64_t dts, int64_t pts, AVPacket *pkt)
{
	AVStream *st = s->streams[stream_index];
	AVPacketList *pktl = s->internal->packet_buffer ? s->internal->packet_buffer : s->internal->parse_queue;
	int64_t pts_buffer[MAX_REORDER_DELAY + 1];
	int64_t shift;
	int i, delay;

	if (st->first_dts != AV_NOPTS_VALUE ||
		dts == AV_NOPTS_VALUE ||
		st->cur_dts == AV_NOPTS_VALUE ||
		is_relative(dts))
		return;

	delay = st->codec->has_b_frames;
	st->first_dts = dts - (st->cur_dts - RELATIVE_TS_BASE);
	st->cur_dts = dts;
	shift = st->first_dts - RELATIVE_TS_BASE;

	for (i = 0; i<MAX_REORDER_DELAY + 1; i++)
		pts_buffer[i] = AV_NOPTS_VALUE;

	if (is_relative(pts))
		pts += shift;

	for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
		if (pktl->pkt.stream_index != stream_index)
			continue;
		if (is_relative(pktl->pkt.pts))
			pktl->pkt.pts += shift;

		if (is_relative(pktl->pkt.dts))
			pktl->pkt.dts += shift;

		if (st->start_time == AV_NOPTS_VALUE && pktl->pkt.pts != AV_NOPTS_VALUE)
			st->start_time = pktl->pkt.pts;

		if (pktl->pkt.pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY && has_decode_delay_been_guessed(st)) {
			pts_buffer[0] = pktl->pkt.pts;
			for (i = 0; i<delay && pts_buffer[i] > pts_buffer[i + 1]; i++)
				FFSWAP(int64_t, pts_buffer[i], pts_buffer[i + 1]);

			pktl->pkt.dts = select_from_pts_buffer(st, pts_buffer, pktl->pkt.dts);
		}
	}

	if (st->start_time == AV_NOPTS_VALUE)
		st->start_time = pts;
}

static void update_initial_durations(AVFormatContext *s, AVStream *st,
	int stream_index, int duration)
{
	AVPacketList *pktl = s->internal->packet_buffer ? s->internal->packet_buffer : s->internal->parse_queue;
	int64_t cur_dts = RELATIVE_TS_BASE;

	if (st->first_dts != AV_NOPTS_VALUE) {
		if (st->update_initial_durations_done)
			return;
		st->update_initial_durations_done = 1;
		cur_dts = st->first_dts;
		for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
			if (pktl->pkt.stream_index == stream_index) {
				if (pktl->pkt.pts != pktl->pkt.dts ||
					pktl->pkt.dts != AV_NOPTS_VALUE ||
					pktl->pkt.duration)
					break;
				cur_dts -= duration;
			}
		}
		if (pktl && pktl->pkt.dts != st->first_dts) {
			av_log(s, AV_LOG_DEBUG, "first_dts %lld not matching first dts %lld (pts %lld, duration %d) in the queue\n",
				st->first_dts, pktl->pkt.dts, pktl->pkt.pts, pktl->pkt.duration);
			return;
		}
		if (!pktl) {
			av_log(s, AV_LOG_DEBUG, "first_dts %lld but no packet with dts in the queue\n", st->first_dts);
			return;
		}
		pktl = s->internal->packet_buffer ? s->internal->packet_buffer : s->internal->parse_queue;
		st->first_dts = cur_dts;
	}
	else if (st->cur_dts != RELATIVE_TS_BASE)
		return;

	for (; pktl; pktl = get_next_pkt(s, st, pktl)) {
		if (pktl->pkt.stream_index != stream_index)
			continue;
		if (pktl->pkt.pts == pktl->pkt.dts &&
			(pktl->pkt.dts == AV_NOPTS_VALUE || pktl->pkt.dts == st->first_dts) &&
			!pktl->pkt.duration) {
			pktl->pkt.dts = cur_dts;
			if (!st->codec->has_b_frames)
				pktl->pkt.pts = cur_dts;
			//            if (st->codec->codec_type != AVMEDIA_TYPE_AUDIO)
			pktl->pkt.duration = duration;
		}
		else
			break;
		cur_dts = pktl->pkt.dts + pktl->pkt.duration;
	}
	if (!pktl)
		st->cur_dts = cur_dts;
}

static void compute_pkt_fields(AVFormatContext *s, AVStream *st,
	AVCodecParserContext *pc, AVPacket *pkt,
	int64_t next_dts, int64_t next_pts)
{
	int num, den, presentation_delayed, delay, i;
	int64_t offset;
	AVRational duration;
	int onein_oneout = st->codec->codec_id != AV_CODEC_ID_H264 &&
		st->codec->codec_id != AV_CODEC_ID_HEVC;

	if (s->flags & AVFMT_FLAG_NOFILLIN)
		return;

	if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && pkt->dts != AV_NOPTS_VALUE) {
		if (pkt->dts == pkt->pts && st->last_dts_for_order_check != AV_NOPTS_VALUE) {
			if (st->last_dts_for_order_check <= pkt->dts) {
				st->dts_ordered++;
			}
			else {
				av_log(s, st->dts_misordered ? AV_LOG_DEBUG : AV_LOG_WARNING,
					"DTS %" "lli" " < %" "lli" " out of order\n",
					pkt->dts,
					st->last_dts_for_order_check);
				st->dts_misordered++;
			}
			if (st->dts_ordered + st->dts_misordered > 250) {
				st->dts_ordered >>= 1;
				st->dts_misordered >>= 1;
			}
		}

		st->last_dts_for_order_check = pkt->dts;
		if (st->dts_ordered < 8 * st->dts_misordered && pkt->dts == pkt->pts)
			pkt->dts = AV_NOPTS_VALUE;
	}

	if ((s->flags & AVFMT_FLAG_IGNDTS) && pkt->pts != AV_NOPTS_VALUE)
		pkt->dts = AV_NOPTS_VALUE;

	if (pc && pc->pict_type == AV_PICTURE_TYPE_B
		&& !st->codec->has_b_frames)
		//FIXME Set low_delay = 0 when has_b_frames = 1
		st->codec->has_b_frames = 1;

	/* do we have a video B-frame ? */
	delay = st->codec->has_b_frames;
	presentation_delayed = 0;

	/* XXX: need has_b_frame, but cannot get it if the codec is
	*  not initialized */
	if (delay &&
		pc && pc->pict_type != AV_PICTURE_TYPE_B)
		presentation_delayed = 1;

	if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE &&
		st->pts_wrap_bits < 63 &&
		pkt->dts - (1LL << (st->pts_wrap_bits - 1)) > pkt->pts) {
		if (is_relative(st->cur_dts) || pkt->dts - (1LL << (st->pts_wrap_bits - 1)) > st->cur_dts) {
			pkt->dts -= 1LL << st->pts_wrap_bits;
		}
		else
			pkt->pts += 1LL << st->pts_wrap_bits;
	}

	/* Some MPEG-2 in MPEG-PS lack dts (issue #171 / input_file.mpg).
	* We take the conservative approach and discard both.
	* Note: If this is misbehaving for an H.264 file, then possibly
	* presentation_delayed is not set correctly. */
	if (delay == 1 && pkt->dts == pkt->pts &&
		pkt->dts != AV_NOPTS_VALUE && presentation_delayed) {
		av_log(s, AV_LOG_DEBUG, "invalid dts/pts combination %" "lli" "\n", pkt->dts);
		if (strcmp(s->iformat->name, "mov,mp4,m4a,3gp,3g2,mj2")
			&& strcmp(s->iformat->name, "flv")) // otherwise we discard correct timestamps for vc1-wmapro.ism
			pkt->dts = AV_NOPTS_VALUE;
	}
	AVRational avrational_duration = {
		pkt->duration, 1
	};
	duration = av_mul_q(avrational_duration, st->time_base);
	if (pkt->duration == 0) {
		ff_compute_frame_duration(s, &num, &den, st, pc, pkt);
		if (den && num) {
			duration = { num, den };
			pkt->duration = av_rescale_rnd(1,
				num * (int64_t)st->time_base.den,
				den * (int64_t)st->time_base.num,
				AV_ROUND_DOWN);
		}
	}

	if (pkt->duration != 0 && (s->internal->packet_buffer || s->internal->parse_queue))
		update_initial_durations(s, st, pkt->stream_index, pkt->duration);

	/* Correct timestamps with byte offset if demuxers only have timestamps
	* on packet boundaries */
	if (pc && st->need_parsing == AVSTREAM_PARSE_TIMESTAMPS && pkt->size) {
		/* this will estimate bitrate based on this frame's duration and size */
		offset = av_rescale(pc->offset, pkt->duration, pkt->size);
		if (pkt->pts != AV_NOPTS_VALUE)
			pkt->pts += offset;
		if (pkt->dts != AV_NOPTS_VALUE)
			pkt->dts += offset;
	}

	/* This may be redundant, but it should not hurt. */
	if (pkt->dts != AV_NOPTS_VALUE &&
		pkt->pts != AV_NOPTS_VALUE &&
		pkt->pts > pkt->dts)
		presentation_delayed = 1;

	if (s->debug & FF_FDEBUG_TS)
		av_log(s, AV_LOG_TRACE,
			"IN delayed:%d pts:%lld, dts:%lld cur_dts:%lld st:%d pc:%p duration:%d delay:%d onein_oneout:%d\n",
			presentation_delayed, pkt->pts, pkt->dts, st->cur_dts,
			pkt->stream_index, pc, pkt->duration, delay, onein_oneout);

	/* Interpolate PTS and DTS if they are not present. We skip H264
	* currently because delay and has_b_frames are not reliably set. */
	if ((delay == 0 || (delay == 1 && pc)) &&
		onein_oneout) {
		if (presentation_delayed) {
			/* DTS = decompression timestamp */
			/* PTS = presentation timestamp */
			if (pkt->dts == AV_NOPTS_VALUE)
				pkt->dts = st->last_IP_pts;
			update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
			if (pkt->dts == AV_NOPTS_VALUE)
				pkt->dts = st->cur_dts;

			/* This is tricky: the dts must be incremented by the duration
			* of the frame we are displaying, i.e. the last I- or P-frame. */
			if (st->last_IP_duration == 0)
				st->last_IP_duration = pkt->duration;
			if (pkt->dts != AV_NOPTS_VALUE)
				st->cur_dts = pkt->dts + st->last_IP_duration;
			if (pkt->dts != AV_NOPTS_VALUE &&
				pkt->pts == AV_NOPTS_VALUE &&
				st->last_IP_duration > 0 &&
				((uint64_t)st->cur_dts - (uint64_t)next_dts + 1) <= 2 &&
				next_dts != next_pts &&
				next_pts != AV_NOPTS_VALUE)
				pkt->pts = next_dts;

			st->last_IP_duration = pkt->duration;
			st->last_IP_pts = pkt->pts;
			/* Cannot compute PTS if not present (we can compute it only
			* by knowing the future. */
		}
		else if (pkt->pts != AV_NOPTS_VALUE ||
			pkt->dts != AV_NOPTS_VALUE ||
			pkt->duration) {

			/* presentation is not delayed : PTS and DTS are the same */
			if (pkt->pts == AV_NOPTS_VALUE)
				pkt->pts = pkt->dts;
			update_initial_timestamps(s, pkt->stream_index, pkt->pts,
				pkt->pts, pkt);
			if (pkt->pts == AV_NOPTS_VALUE)
				pkt->pts = st->cur_dts;
			pkt->dts = pkt->pts;
			if (pkt->pts != AV_NOPTS_VALUE)
				st->cur_dts = av_add_stable(st->time_base, pkt->pts, duration, 1);
		}
	}

	if (pkt->pts != AV_NOPTS_VALUE && delay <= MAX_REORDER_DELAY) {
		st->pts_buffer[0] = pkt->pts;
		for (i = 0; i<delay && st->pts_buffer[i] > st->pts_buffer[i + 1]; i++)
			FFSWAP(int64_t, st->pts_buffer[i], st->pts_buffer[i + 1]);

		if (has_decode_delay_been_guessed(st))
			pkt->dts = select_from_pts_buffer(st, st->pts_buffer, pkt->dts);
	}
	// We skipped it above so we try here.
	if (!onein_oneout)
		// This should happen on the first packet
		update_initial_timestamps(s, pkt->stream_index, pkt->dts, pkt->pts, pkt);
	if (pkt->dts > st->cur_dts)
		st->cur_dts = pkt->dts;

	if (s->debug & FF_FDEBUG_TS)
		av_log(s, AV_LOG_TRACE, "OUTdelayed:%d/%d pts:%lld, dts:%lld cur_dts:%lld\n",
			presentation_delayed, delay, pkt->pts, pkt->dts, st->cur_dts);

	/* update flags */
	if (is_intra_only(st->codec))
		pkt->flags |= AV_PKT_FLAG_KEY;
	if (pc)
		pkt->convergence_duration = pc->convergence_duration;
}

static void free_packet_buffer(AVPacketList **pkt_buf, AVPacketList **pkt_buf_end)
{
	while (*pkt_buf) {
		AVPacketList *pktl = *pkt_buf;
		*pkt_buf = pktl->next;
		av_free_packet(&pktl->pkt);
		av_freep(&pktl);
	}
	*pkt_buf_end = NULL;
}

/**
* Parse a packet, add all split parts to parse_queue.
*
* @param pkt Packet to parse, NULL when flushing the parser at end of stream.
*/
static int parse_packet(AVFormatContext *s, AVPacket *pkt, int stream_index)
{
	AVPacket out_pkt = { 0 }, flush_pkt = { 0 };
	AVStream *st = s->streams[stream_index];
	uint8_t *data = pkt ? pkt->data : NULL;
	int size = pkt ? pkt->size : 0;
	int ret = 0, got_output = 0;

	if (!pkt) {
		av_init_packet(&flush_pkt);
		pkt = &flush_pkt;
		got_output = 1;
	}
	else if (!size && st->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) {
		// preserve 0-size sync packets
		compute_pkt_fields(s, st, st->parser, pkt, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
	}

	while (size > 0 || (pkt == &flush_pkt && got_output)) {
		int len;
		int64_t next_pts = pkt->pts;
		int64_t next_dts = pkt->dts;

		av_init_packet(&out_pkt);
		len = av_parser_parse2(st->parser, st->codec,
			&out_pkt.data, &out_pkt.size, data, size,
			pkt->pts, pkt->dts, pkt->pos);

		pkt->pts = pkt->dts = AV_NOPTS_VALUE;
		pkt->pos = -1;
		/* increment read pointer */
		data += len;
		size -= len;

		got_output = !!out_pkt.size;

		if (!out_pkt.size)
			continue;

		if (pkt->side_data) {
			out_pkt.side_data = pkt->side_data;
			out_pkt.side_data_elems = pkt->side_data_elems;
			pkt->side_data = NULL;
			pkt->side_data_elems = 0;
		}

		/* set the duration */
		out_pkt.duration = (st->parser->flags & PARSER_FLAG_COMPLETE_FRAMES) ? pkt->duration : 0;
		if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			if (st->codec->sample_rate > 0) {
				AVRational tmp = { 1, st->codec->sample_rate };
				out_pkt.duration =
					av_rescale_q_rnd(st->parser->duration,
						tmp,
						st->time_base,
						AV_ROUND_DOWN);
			}
		}

		out_pkt.stream_index = st->index;
		out_pkt.pts = st->parser->pts;
		out_pkt.dts = st->parser->dts;
		out_pkt.pos = st->parser->pos;

		if (st->need_parsing == AVSTREAM_PARSE_FULL_RAW)
			out_pkt.pos = st->parser->frame_offset;

		if (st->parser->key_frame == 1 ||
			(st->parser->key_frame == -1 &&
				st->parser->pict_type == AV_PICTURE_TYPE_I))
			out_pkt.flags |= AV_PKT_FLAG_KEY;

		if (st->parser->key_frame == -1 && st->parser->pict_type == AV_PICTURE_TYPE_NONE && (pkt->flags&AV_PKT_FLAG_KEY))
			out_pkt.flags |= AV_PKT_FLAG_KEY;

		compute_pkt_fields(s, st, st->parser, &out_pkt, next_dts, next_pts);

		if (out_pkt.data == pkt->data && out_pkt.size == pkt->size) {
			out_pkt.buf = pkt->buf;
			pkt->buf = NULL;
#if FF_API_DESTRUCT_PACKET
			FF_DISABLE_DEPRECATION_WARNINGS
				out_pkt.destruct = pkt->destruct;
			pkt->destruct = NULL;
			FF_ENABLE_DEPRECATION_WARNINGS
#endif
		}
		if ((ret = av_dup_packet(&out_pkt)) < 0)
			goto fail;

		if (!add_to_pktbuf(&s->internal->parse_queue, &out_pkt, &s->internal->parse_queue_end)) {
			av_free_packet(&out_pkt);
			ret = AVERROR(ENOMEM);
			goto fail;
		}
	}

	/* end of the stream => close and free the parser */
	if (pkt == &flush_pkt) {
		av_parser_close(st->parser);
		st->parser = NULL;
	}

fail:
	av_free_packet(pkt);
	return ret;
}

static int read_from_packet_buffer(AVPacketList **pkt_buffer,
	AVPacketList **pkt_buffer_end,
	AVPacket      *pkt)
{
	AVPacketList *pktl;
	av_assert0(*pkt_buffer);
	pktl = *pkt_buffer;
	*pkt = pktl->pkt;
	*pkt_buffer = pktl->next;
	if (!pktl->next)
		*pkt_buffer_end = NULL;
	av_freep(&pktl);
	return 0;
}

static void ff_reduce_index(AVFormatContext *s, int stream_index)
{
	AVStream *st = s->streams[stream_index];
	unsigned int max_entries = s->max_index_size / sizeof(AVIndexEntry);

	if ((unsigned)st->nb_index_entries >= max_entries) {
		int i;
		for (i = 0; 2 * i < st->nb_index_entries; i++)
			st->index_entries[i] = st->index_entries[2 * i];
		st->nb_index_entries = i;
	}
}

static int64_t ts_to_samples(AVStream *st, int64_t ts)
{
	return av_rescale(ts, st->time_base.num * st->codec->sample_rate, st->time_base.den);
}

static int read_frame_internal(AVFormatContext *s, AVPacket *pkt)
{
	int ret = 0, i, got_packet = 0;
	AVDictionary *metadata = NULL;

	av_init_packet(pkt);

	while (!got_packet && !s->internal->parse_queue) {
		AVStream *st;
		AVPacket cur_pkt;

		/* read next packet */
		ret = ff_read_packet(s, &cur_pkt);
		if (ret < 0) {
			if (ret == AVERROR(EAGAIN))
				return ret;
			/* flush the parsers */
			for (i = 0; i < s->nb_streams; i++) {
				st = s->streams[i];
				if (st->parser && st->need_parsing)
					parse_packet(s, NULL, st->index);
			}
			/* all remaining packets are now in parse_queue =>
			* really terminate parsing */
			break;
		}
		ret = 0;
		st = s->streams[cur_pkt.stream_index];

		if (cur_pkt.pts != AV_NOPTS_VALUE &&
			cur_pkt.dts != AV_NOPTS_VALUE &&
			cur_pkt.pts < cur_pkt.dts) {
			av_log(s, AV_LOG_WARNING,
				"Invalid timestamps stream=%d, pts=%lld, dts=%lld, size=%d\n",
				cur_pkt.stream_index,
				cur_pkt.pts,
				cur_pkt.dts,
				cur_pkt.size);
		}
		if (s->debug & FF_FDEBUG_TS)
			av_log(s, AV_LOG_DEBUG,
				"ff_read_packet stream=%d, pts=%lld, dts=%lld, size=%d, duration=%d, flags=%d\n",
				cur_pkt.stream_index,
				cur_pkt.pts,
				cur_pkt.dts,
				cur_pkt.size, cur_pkt.duration, cur_pkt.flags);

		if (st->need_parsing && !st->parser && !(s->flags & AVFMT_FLAG_NOPARSE)) {
			st->parser = av_parser_init(st->codec->codec_id);
			if (!st->parser) {
				av_log(s, AV_LOG_VERBOSE, "parser not found for codec "
					"%s, packets or times may be invalid.\n",
					avcodec_get_name(st->codec->codec_id));
				/* no parser available: just output the raw packets */
				st->need_parsing = AVSTREAM_PARSE_NONE;
			}
			else if (st->need_parsing == AVSTREAM_PARSE_HEADERS)
				st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
			else if (st->need_parsing == AVSTREAM_PARSE_FULL_ONCE)
				st->parser->flags |= PARSER_FLAG_ONCE;
			else if (st->need_parsing == AVSTREAM_PARSE_FULL_RAW)
				st->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
		}

		if (!st->need_parsing || !st->parser) {
			/* no parsing needed: we just output the packet as is */
			*pkt = cur_pkt;
			compute_pkt_fields(s, st, NULL, pkt, AV_NOPTS_VALUE, AV_NOPTS_VALUE);
			if ((s->iformat->flags & AVFMT_GENERIC_INDEX) &&
				(pkt->flags & AV_PKT_FLAG_KEY) && pkt->dts != AV_NOPTS_VALUE) {
				ff_reduce_index(s, st->index);
				av_add_index_entry(st, pkt->pos, pkt->dts,
					0, 0, AVINDEX_KEYFRAME);
			}
			got_packet = 1;
		}
		else if (st->discard < AVDISCARD_ALL) {
			if ((ret = parse_packet(s, &cur_pkt, cur_pkt.stream_index)) < 0)
				return ret;
		}
		else {
			/* free packet */
			av_free_packet(&cur_pkt);
		}
		if (pkt->flags & AV_PKT_FLAG_KEY)
			st->skip_to_keyframe = 0;
		if (st->skip_to_keyframe) {
			av_free_packet(&cur_pkt);
			if (got_packet) {
				*pkt = cur_pkt;
			}
			got_packet = 0;
		}
	}

	if (!got_packet && s->internal->parse_queue)
		ret = read_from_packet_buffer(&s->internal->parse_queue, &s->internal->parse_queue_end, pkt);

	if (ret >= 0) {
		AVStream *st = s->streams[pkt->stream_index];
		int discard_padding = 0;
		if (st->first_discard_sample && pkt->pts != AV_NOPTS_VALUE) {
			int64_t pts = pkt->pts - (is_relative(pkt->pts) ? RELATIVE_TS_BASE : 0);
			int64_t sample = ts_to_samples(st, pts);
			int duration = ts_to_samples(st, pkt->duration);
			int64_t end_sample = sample + duration;
			if (duration > 0 && end_sample >= st->first_discard_sample &&
				sample < st->last_discard_sample)
				discard_padding = FFMIN(end_sample - st->first_discard_sample, duration);
		}
		if (st->start_skip_samples && (pkt->pts == 0 || pkt->pts == RELATIVE_TS_BASE))
			st->skip_samples = st->start_skip_samples;
		if (st->skip_samples || discard_padding) {
			uint8_t *p = av_packet_new_side_data(pkt, AV_PKT_DATA_SKIP_SAMPLES, 10);
			if (p) {
				AV_WL32(p, st->skip_samples);
				AV_WL32(p + 4, discard_padding);
				av_log(s, AV_LOG_DEBUG, "demuxer injecting skip %d / discard %d\n", st->skip_samples, discard_padding);
			}
			st->skip_samples = 0;
		}

		if (st->inject_global_side_data) {
			for (i = 0; i < st->nb_side_data; i++) {
				AVPacketSideData *src_sd = &st->side_data[i];
				uint8_t *dst_data;

				if (av_packet_get_side_data(pkt, src_sd->type, NULL))
					continue;

				dst_data = av_packet_new_side_data(pkt, src_sd->type, src_sd->size);
				if (!dst_data) {
					av_log(s, AV_LOG_WARNING, "Could not inject global side data\n");
					continue;
				}

				memcpy(dst_data, src_sd->data, src_sd->size);
			}
			st->inject_global_side_data = 0;
		}

		if (!(s->flags & AVFMT_FLAG_KEEP_SIDE_DATA))
			av_packet_merge_side_data(pkt);
	}

	av_opt_get_dict_val(s, "metadata", AV_OPT_SEARCH_CHILDREN, &metadata);
	if (metadata) {
		s->event_flags |= AVFMT_EVENT_FLAG_METADATA_UPDATED;
		av_dict_copy(&s->metadata, metadata, 0);
		av_dict_free(&metadata);
		av_opt_set_dict_val(s, "metadata", NULL, AV_OPT_SEARCH_CHILDREN);
	}

	if (s->debug & FF_FDEBUG_TS)
		av_log(s, AV_LOG_DEBUG,
			"read_frame_internal stream=%d, pts=%lld, dts=%lld, "
			"size=%d, duration=%d, flags=%d\n",
			pkt->stream_index,
			pkt->pts,
			pkt->dts,
			pkt->size, pkt->duration, pkt->flags);

	return ret;
}

/* XXX: suppress the packet queue */
static void flush_packet_queue(AVFormatContext *s)
{
	if (!s->internal)
		return;
	free_packet_buffer(&s->internal->parse_queue, &s->internal->parse_queue_end);
	free_packet_buffer(&s->internal->packet_buffer, &s->internal->packet_buffer_end);
	free_packet_buffer(&s->internal->raw_packet_buffer, &s->internal->raw_packet_buffer_end);

	s->internal->raw_packet_buffer_remaining_size = RAW_PACKET_BUFFER_SIZE;
}

/**
* Return TRUE if the stream has accurate duration in any stream.
*
* @return TRUE if the stream has accurate duration for at least one component.
*/
static int has_duration(AVFormatContext *ic)
{
	int i;
	AVStream *st;

	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (st->duration != AV_NOPTS_VALUE)
			return 1;
	}
	if (ic->duration != AV_NOPTS_VALUE)
		return 1;
	return 0;
}

/**
* Estimate the stream timings from the one of each components.
*
* Also computes the global bitrate if possible.
*/
static void update_stream_timings(AVFormatContext *ic)
{
	int64_t start_time, start_time1, start_time_text, end_time, end_time1;
	int64_t duration, duration1, filesize;
	int i;
	AVStream *st;
	AVProgram *p;

	start_time = INT64_MAX;
	start_time_text = INT64_MAX;
	end_time = INT64_MIN;
	duration = INT64_MIN;
	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (st->start_time != AV_NOPTS_VALUE && st->time_base.den) {
			start_time1 = av_rescale_q(st->start_time, st->time_base,
				AV_TIME_BASE_Q);
			if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE || st->codec->codec_type == AVMEDIA_TYPE_DATA) {
				if (start_time1 < start_time_text)
					start_time_text = start_time1;
			}
			else
				start_time = FFMIN(start_time, start_time1);
			end_time1 = AV_NOPTS_VALUE;
			if (st->duration != AV_NOPTS_VALUE) {
				end_time1 = start_time1 +
					av_rescale_q(st->duration, st->time_base,
						AV_TIME_BASE_Q);
				end_time = FFMAX(end_time, end_time1);
			}
			for (p = NULL; (p = av_find_program_from_stream(ic, p, i)); ) {
				if (p->start_time == AV_NOPTS_VALUE || p->start_time > start_time1)
					p->start_time = start_time1;
				if (p->end_time < end_time1)
					p->end_time = end_time1;
			}
		}
		if (st->duration != AV_NOPTS_VALUE) {
			duration1 = av_rescale_q(st->duration, st->time_base,
				AV_TIME_BASE_Q);
			duration = FFMAX(duration, duration1);
		}
	}
	if (start_time == INT64_MAX || (start_time > start_time_text && start_time - start_time_text < AV_TIME_BASE))
		start_time = start_time_text;
	else if (start_time > start_time_text)
		av_log(ic, AV_LOG_VERBOSE, "Ignoring outlier non primary stream starttime %f\n", start_time_text / (float)AV_TIME_BASE);

	if (start_time != INT64_MAX) {
		ic->start_time = start_time;
		if (end_time != INT64_MIN) {
			if (ic->nb_programs) {
				for (i = 0; i < ic->nb_programs; i++) {
					p = ic->programs[i];
					if (p->start_time != AV_NOPTS_VALUE &&
						p->end_time > p->start_time &&
						p->end_time - (uint64_t)p->start_time <= INT64_MAX)
						duration = FFMAX(duration, p->end_time - p->start_time);
				}
			}
			else if (end_time >= start_time && end_time - (uint64_t)start_time <= INT64_MAX) {
				duration = FFMAX(duration, end_time - start_time);
			}
		}
	}
	if (duration != INT64_MIN && duration > 0 && ic->duration == AV_NOPTS_VALUE) {
		ic->duration = duration;
	}
	if (ic->pb && (filesize = avio_size(ic->pb)) > 0 && ic->duration > 0) {
		/* compute the bitrate */
		double bitrate = (double)filesize * 8.0 * AV_TIME_BASE /
			(double)ic->duration;
		if (bitrate >= 0 && bitrate <= INT_MAX)
			ic->bit_rate = bitrate;
	}
}

static void fill_all_stream_timings(AVFormatContext *ic)
{
	int i;
	AVStream *st;

	update_stream_timings(ic);
	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (st->start_time == AV_NOPTS_VALUE) {
			if (ic->start_time != AV_NOPTS_VALUE)
				st->start_time = av_rescale_q(ic->start_time, AV_TIME_BASE_Q,
					st->time_base);
			if (ic->duration != AV_NOPTS_VALUE)
				st->duration = av_rescale_q(ic->duration, AV_TIME_BASE_Q,
					st->time_base);
		}
	}
}

static void estimate_timings_from_bit_rate(AVFormatContext *ic)
{
	int64_t filesize, duration;
	int i, show_warning = 0;
	AVStream *st;

	/* if bit_rate is already set, we believe it */
	if (ic->bit_rate <= 0) {
		int bit_rate = 0;
		for (i = 0; i < ic->nb_streams; i++) {
			st = ic->streams[i];
			if (st->codec->bit_rate > 0) {
				if (INT_MAX - st->codec->bit_rate < bit_rate) {
					bit_rate = 0;
					break;
				}
				bit_rate += st->codec->bit_rate;
			}
			else if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO && st->codec_info_nb_frames > 1) {
				// If we have a videostream with packets but without a bitrate
				// then consider the sum not known
				bit_rate = 0;
				break;
			}
		}
		ic->bit_rate = bit_rate;
	}

	/* if duration is already set, we believe it */
	if (ic->duration == AV_NOPTS_VALUE &&
		ic->bit_rate != 0) {
		filesize = ic->pb ? avio_size(ic->pb) : 0;
		if (filesize > ic->internal->data_offset) {
			filesize -= ic->internal->data_offset;
			for (i = 0; i < ic->nb_streams; i++) {
				st = ic->streams[i];
				if (st->time_base.num <= INT64_MAX / ic->bit_rate
					&& st->duration == AV_NOPTS_VALUE) {
					duration = av_rescale(8 * filesize, st->time_base.den,
						ic->bit_rate *
						(int64_t)st->time_base.num);
					st->duration = duration;
					show_warning = 1;
				}
			}
		}
	}
	if (show_warning)
		av_log(ic, AV_LOG_WARNING,
			"Estimating duration from bitrate, this may be inaccurate\n");
}

#define DURATION_MAX_READ_SIZE 250000LL
#define DURATION_MAX_RETRY 6

/* only usable for MPEG-PS streams */
static void estimate_timings_from_pts(AVFormatContext *ic, int64_t old_offset)
{
	AVPacket pkt1, *pkt = &pkt1;
	AVStream *st;
	int num, den, read_size, i, ret;
	int found_duration = 0;
	int is_end;
	int64_t filesize, offset, duration;
	int retry = 0;

	/* flush packet queue */
	flush_packet_queue(ic);

	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (st->start_time == AV_NOPTS_VALUE &&
			st->first_dts == AV_NOPTS_VALUE &&
			st->codec->codec_type != AVMEDIA_TYPE_UNKNOWN)
			av_log(st->codec, AV_LOG_WARNING,
				"start time for stream %d is not set in estimate_timings_from_pts\n", i);

		if (st->parser) {
			av_parser_close(st->parser);
			st->parser = NULL;
		}
	}

	av_opt_set(ic, "skip_changes", "1", AV_OPT_SEARCH_CHILDREN);
	/* estimate the end time (duration) */
	/* XXX: may need to support wrapping */
	filesize = ic->pb ? avio_size(ic->pb) : 0;
	do {
		is_end = found_duration;
		offset = filesize - (DURATION_MAX_READ_SIZE << retry);
		if (offset < 0)
			offset = 0;

		avio_seek(ic->pb, offset, SEEK_SET);
		read_size = 0;
		for (;;) {
			if (read_size >= DURATION_MAX_READ_SIZE << (FFMAX(retry - 1, 0)))
				break;

			do {
				ret = ff_read_packet(ic, pkt);
			} while (ret == AVERROR(EAGAIN));
			if (ret != 0)
				break;
			read_size += pkt->size;
			st = ic->streams[pkt->stream_index];
			if (pkt->pts != AV_NOPTS_VALUE &&
				(st->start_time != AV_NOPTS_VALUE ||
					st->first_dts != AV_NOPTS_VALUE)) {
				if (pkt->duration == 0) {
					ff_compute_frame_duration(ic, &num, &den, st, st->parser, pkt);
					if (den && num) {
						pkt->duration = av_rescale_rnd(1,
							num * (int64_t)st->time_base.den,
							den * (int64_t)st->time_base.num,
							AV_ROUND_DOWN);
					}
				}
				duration = pkt->pts + pkt->duration;
				found_duration = 1;
				if (st->start_time != AV_NOPTS_VALUE)
					duration -= st->start_time;
				else
					duration -= st->first_dts;
				if (duration > 0) {
					if (st->duration == AV_NOPTS_VALUE || st->info->last_duration <= 0 ||
						(st->duration < duration && FFABS(duration - st->info->last_duration) < 60LL * st->time_base.den / st->time_base.num))
						st->duration = duration;
					st->info->last_duration = duration;
				}
			}
			av_free_packet(pkt);
		}

		/* check if all audio/video streams have valid duration */
		if (!is_end) {
			is_end = 1;
			for (i = 0; i < ic->nb_streams; i++) {
				st = ic->streams[i];
				switch (st->codec->codec_type) {
				case AVMEDIA_TYPE_VIDEO:
				case AVMEDIA_TYPE_AUDIO:
					if (st->duration == AV_NOPTS_VALUE)
						is_end = 0;
				}
			}
		}
	} while (!is_end &&
		offset &&
		++retry <= DURATION_MAX_RETRY);

	av_opt_set(ic, "skip_changes", "0", AV_OPT_SEARCH_CHILDREN);

	/* warn about audio/video streams which duration could not be estimated */
	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (st->duration == AV_NOPTS_VALUE) {
			switch (st->codec->codec_type) {
			case AVMEDIA_TYPE_VIDEO:
			case AVMEDIA_TYPE_AUDIO:
				if (st->start_time != AV_NOPTS_VALUE || st->first_dts != AV_NOPTS_VALUE) {
					av_log(ic, AV_LOG_DEBUG, "stream %d : no PTS found at end of file, duration not set\n", i);
				}
				else
					av_log(ic, AV_LOG_DEBUG, "stream %d : no TS found at start of file, duration not set\n", i);
			}
		}
	}
	fill_all_stream_timings(ic);

	avio_seek(ic->pb, old_offset, SEEK_SET);
	for (i = 0; i < ic->nb_streams; i++) {
		int j;

		st = ic->streams[i];
		st->cur_dts = st->first_dts;
		st->last_IP_pts = AV_NOPTS_VALUE;
		st->last_dts_for_order_check = AV_NOPTS_VALUE;
		for (j = 0; j < MAX_REORDER_DELAY + 1; j++)
			st->pts_buffer[j] = AV_NOPTS_VALUE;
	}
}

static void estimate_timings(AVFormatContext *ic, int64_t old_offset)
{
	int64_t file_size;

	/* get the file size, if possible */
	if (ic->iformat->flags & AVFMT_NOFILE) {
		file_size = 0;
	}
	else {
		file_size = avio_size(ic->pb);
		file_size = FFMAX(0, file_size);
	}

	if ((!strcmp(ic->iformat->name, "mpeg") ||
		!strcmp(ic->iformat->name, "mpegts")) &&
		file_size && ic->pb->seekable) {
		/* get accurate estimate from the PTSes */
		estimate_timings_from_pts(ic, old_offset);
		ic->duration_estimation_method = AVFMT_DURATION_FROM_PTS;
	}
	else if (has_duration(ic)) {
		/* at least one component has timings - we use them for all
		* the components */
		fill_all_stream_timings(ic);
		ic->duration_estimation_method = AVFMT_DURATION_FROM_STREAM;
	}
	else {
		/* less precise: use bitrate info */
		estimate_timings_from_bit_rate(ic);
		ic->duration_estimation_method = AVFMT_DURATION_FROM_BITRATE;
	}
	update_stream_timings(ic);

	{
		int i;
		AVStream av_unused *st;
		for (i = 0; i < ic->nb_streams; i++) {
			st = ic->streams[i];
			av_log(ic, AV_LOG_TRACE, "%d: start_time: %0.3f duration: %0.3f\n", i,
				(double)st->start_time / AV_TIME_BASE,
				(double)st->duration / AV_TIME_BASE);
		}
		av_log(ic, AV_LOG_TRACE,
			"stream: start_time: %0.3f duration: %0.3f bitrate=%d kb/s\n",
			(double)ic->start_time / AV_TIME_BASE,
			(double)ic->duration / AV_TIME_BASE,
			ic->bit_rate / 1000);
	}
}

static int has_codec_parameters(AVStream *st, const char **errmsg_ptr)
{
	AVCodecContext *avctx = st->codec;

#define FAIL(errmsg) do {                                         \
        if (errmsg_ptr)                                           \
            *errmsg_ptr = errmsg;                                 \
        return 0;                                                 \
    } while (0)

	if (avctx->codec_id == AV_CODEC_ID_NONE
		&& avctx->codec_type != AVMEDIA_TYPE_DATA)
		FAIL("unknown codec");
	switch (avctx->codec_type) {
	case AVMEDIA_TYPE_AUDIO:
		if (!avctx->frame_size && determinable_frame_size(avctx))
			FAIL("unspecified frame size");
		if (st->info->found_decoder >= 0 &&
			avctx->sample_fmt == AV_SAMPLE_FMT_NONE)
			FAIL("unspecified sample format");
		if (!avctx->sample_rate)
			FAIL("unspecified sample rate");
		if (!avctx->channels)
			FAIL("unspecified number of channels");
		if (st->info->found_decoder >= 0 && !st->nb_decoded_frames && avctx->codec_id == AV_CODEC_ID_DTS)
			FAIL("no decodable DTS frames");
		break;
	case AVMEDIA_TYPE_VIDEO:
		if (!avctx->width)
			FAIL("unspecified size");
		if (st->info->found_decoder >= 0 && avctx->pix_fmt == AV_PIX_FMT_NONE)
			FAIL("unspecified pixel format");
		if (st->codec->codec_id == AV_CODEC_ID_RV30 || st->codec->codec_id == AV_CODEC_ID_RV40)
			if (!st->sample_aspect_ratio.num && !st->codec->sample_aspect_ratio.num && !st->codec_info_nb_frames)
				FAIL("no frame in rv30/40 and no sar");
		break;
	case AVMEDIA_TYPE_SUBTITLE:
		if (avctx->codec_id == AV_CODEC_ID_HDMV_PGS_SUBTITLE && !avctx->width)
			FAIL("unspecified size");
		break;
	case AVMEDIA_TYPE_DATA:
		if (avctx->codec_id == AV_CODEC_ID_NONE) return 1;
	}

	return 1;
}

/* returns 1 or 0 if or if not decoded data was returned, or a negative error */
static int try_decode_frame(AVFormatContext *s, AVStream *st, AVPacket *avpkt,
	AVDictionary **options)
{
	const AVCodec *codec;
	int got_picture = 1, ret = 0;
	AVFrame *frame = av_frame_alloc();
	AVSubtitle subtitle;
	AVPacket pkt = *avpkt;

	if (!frame)
		return AVERROR(ENOMEM);

	if (!avcodec_is_open(st->codec) &&
		st->info->found_decoder <= 0 &&
		(st->codec->codec_id != -st->info->found_decoder || !st->codec->codec_id)) {
		AVDictionary *thread_opt = NULL;

		codec = find_decoder(s, st, st->codec->codec_id);

		if (!codec) {
			st->info->found_decoder = -st->codec->codec_id;
			ret = -1;
			goto fail;
		}

		/* Force thread count to 1 since the H.264 decoder will not extract
		* SPS and PPS to extradata during multi-threaded decoding. */
		av_dict_set(options ? options : &thread_opt, "threads", "1", 0);
		if (s->codec_whitelist)
			av_dict_set(options ? options : &thread_opt, "codec_whitelist", s->codec_whitelist, 0);
		ret = avcodec_open2(st->codec, codec, options ? options : &thread_opt);
		if (!options)
			av_dict_free(&thread_opt);
		if (ret < 0) {
			st->info->found_decoder = -st->codec->codec_id;
			goto fail;
		}
		st->info->found_decoder = 1;
	}
	else if (!st->info->found_decoder)
		st->info->found_decoder = 1;

	if (st->info->found_decoder < 0) {
		ret = -1;
		goto fail;
	}

	while ((pkt.size > 0 || (!pkt.data && got_picture)) &&
		ret >= 0 &&
		(!has_codec_parameters(st, NULL) || !has_decode_delay_been_guessed(st) ||
			(!st->codec_info_nb_frames &&
				(st->codec->codec->capabilities & AV_CODEC_CAP_CHANNEL_CONF)))) {
		got_picture = 0;
		switch (st->codec->codec_type) {
		case AVMEDIA_TYPE_VIDEO:
			ret = avcodec_decode_video2(st->codec, frame,
				&got_picture, &pkt);
			break;
		case AVMEDIA_TYPE_AUDIO:
			ret = avcodec_decode_audio4(st->codec, frame, &got_picture, &pkt);
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			ret = avcodec_decode_subtitle2(st->codec, &subtitle,
				&got_picture, &pkt);
			ret = pkt.size;
			break;
		default:
			break;
		}
		if (ret >= 0) {
			if (got_picture)
				st->nb_decoded_frames++;
			pkt.data += ret;
			pkt.size -= ret;
			ret = got_picture;
		}
	}

	if (!pkt.data && !got_picture)
		ret = -1;

fail:
	av_frame_free(&frame);
	return ret;
}

static void compute_chapters_end(AVFormatContext *s)
{
	unsigned int i, j;
	int64_t max_time = s->duration +
		((s->start_time == AV_NOPTS_VALUE) ? 0 : s->start_time);

	for (i = 0; i < s->nb_chapters; i++)
		if (s->chapters[i]->end == AV_NOPTS_VALUE) {
			AVChapter *ch = s->chapters[i];
			int64_t end = max_time ? av_rescale_q(max_time, AV_TIME_BASE_Q,
				ch->time_base)
				: INT64_MAX;

			for (j = 0; j < s->nb_chapters; j++) {
				AVChapter *ch1 = s->chapters[j];
				int64_t next_start = av_rescale_q(ch1->start, ch1->time_base,
					ch->time_base);
				if (j != i && next_start > ch->start && next_start < end)
					end = next_start;
			}
			ch->end = (end == INT64_MAX) ? ch->start : end;
		}
}

static int get_std_framerate(int i)
{
	if (i < 30 * 12)
		return (i + 1) * 1001;
	i -= 30 * 12;

	if (i < 30)
		return (i + 31) * 1001 * 12;
	i -= 30;
	const int  tmp[] = { 80, 120, 240 };
	if (i < 3)
		return tmp[i] * 1001 * 12;

	i -= 3;
	const int tmp2[] = { 24, 30, 60, 12, 15, 48 };
	return tmp2[i] * 1000 * 12;
}

/* Is the time base unreliable?
* This is a heuristic to balance between quick acceptance of the values in
* the headers vs. some extra checks.
* Old DivX and Xvid often have nonsense timebases like 1fps or 2fps.
* MPEG-2 commonly misuses field repeat flags to store different framerates.
* And there are "variable" fps files this needs to detect as well. */
static int tb_unreliable(AVCodecContext *c)
{
	if (c->time_base.den >= 101LL * c->time_base.num ||
		c->time_base.den <    5LL * c->time_base.num ||
		// c->codec_tag == AV_RL32("DIVX") ||
		// c->codec_tag == AV_RL32("XVID") ||
		c->codec_tag == AV_RL32("mp4v") ||
		c->codec_id == AV_CODEC_ID_MPEG2VIDEO ||
		c->codec_id == AV_CODEC_ID_GIF ||
		c->codec_id == AV_CODEC_ID_HEVC ||
		c->codec_id == AV_CODEC_ID_H264)
		return 1;
	return 0;
}

int ff_alloc_extradata(AVCodecContext *avctx, int size)
{
	int ret;

	if (size < 0 || size >= INT32_MAX - AV_INPUT_BUFFER_PADDING_SIZE) {
		avctx->extradata = NULL;
		avctx->extradata_size = 0;
		return AVERROR(EINVAL);
	}
	avctx->extradata = (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
	if (avctx->extradata) {
		memset(avctx->extradata + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
		avctx->extradata_size = size;
		ret = 0;
	}
	else {
		avctx->extradata_size = 0;
		ret = AVERROR(ENOMEM);
	}
	return ret;
}

int ff_rfps_add_frame(AVFormatContext *ic, AVStream *st, int64_t ts)
{
	int i, j;
	int64_t last = st->info->last_dts;

	if (ts != AV_NOPTS_VALUE && last != AV_NOPTS_VALUE && ts > last
		&& ts - (uint64_t)last < INT64_MAX) {
		double dts = (is_relative(ts) ? ts - RELATIVE_TS_BASE : ts) * av_q2d(st->time_base);
		int64_t duration = ts - last;

		if (!st->info->duration_error)
			st->info->duration_error = (double(*)[2][MAX_STD_TIMEBASES])av_mallocz(sizeof(st->info->duration_error[0]) * 2);
		if (!st->info->duration_error)
			return AVERROR(ENOMEM);

		//         if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		//             av_log(NULL, AV_LOG_ERROR, "%f\n", dts);
		for (i = 0; i<MAX_STD_TIMEBASES; i++) {
			if (st->info->duration_error[0][1][i] < 1e10) {
				int framerate = get_std_framerate(i);
				double sdts = dts*framerate / (1001 * 12);
				for (j = 0; j<2; j++) {
					int64_t ticks = llrint(sdts + j*0.5);
					double error = sdts - ticks + j*0.5;
					st->info->duration_error[j][0][i] += error;
					st->info->duration_error[j][1][i] += error*error;
				}
			}
		}
		st->info->duration_count++;
		st->info->rfps_duration_sum += duration;

		if (st->info->duration_count % 10 == 0) {
			int n = st->info->duration_count;
			for (i = 0; i<MAX_STD_TIMEBASES; i++) {
				if (st->info->duration_error[0][1][i] < 1e10) {
					double a0 = st->info->duration_error[0][0][i] / n;
					double error0 = st->info->duration_error[0][1][i] / n - a0*a0;
					double a1 = st->info->duration_error[1][0][i] / n;
					double error1 = st->info->duration_error[1][1][i] / n - a1*a1;
					if (error0 > 0.04 && error1 > 0.04) {
						st->info->duration_error[0][1][i] = 2e10;
						st->info->duration_error[1][1][i] = 2e10;
					}
				}
			}
		}

		// ignore the first 4 values, they might have some random jitter
		if (st->info->duration_count > 3 && is_relative(ts) == is_relative(last))
			st->info->duration_gcd = av_gcd(st->info->duration_gcd, duration);
	}
	if (ts != AV_NOPTS_VALUE)
		st->info->last_dts = ts;

	return 0;
}

void ff_rfps_calculate(AVFormatContext *ic)
{
	int i, j;

	for (i = 0; i < ic->nb_streams; i++) {
		AVStream *st = ic->streams[i];

		if (st->codec->codec_type != AVMEDIA_TYPE_VIDEO)
			continue;
		// the check for tb_unreliable() is not completely correct, since this is not about handling
		// a unreliable/inexact time base, but a time base that is finer than necessary, as e.g.
		// ipmovie.c produces.
		if (tb_unreliable(st->codec) && st->info->duration_count > 15 && st->info->duration_gcd > FFMAX(1, st->time_base.den / (500LL * st->time_base.num)) && !st->r_frame_rate.num)
			av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, st->time_base.den, st->time_base.num * st->info->duration_gcd, INT_MAX);
		if (st->info->duration_count>1 && !st->r_frame_rate.num
			&& tb_unreliable(st->codec)) {
			int num = 0;
			double best_error = 0.01;
			AVRational ref_rate = st->r_frame_rate.num ? st->r_frame_rate : av_inv_q(st->time_base);

			for (j = 0; j<MAX_STD_TIMEBASES; j++) {
				int k;

				if (st->info->codec_info_duration && st->info->codec_info_duration*av_q2d(st->time_base) < (1001 * 12.0) / get_std_framerate(j))
					continue;
				if (!st->info->codec_info_duration && get_std_framerate(j) < 1001 * 12)
					continue;

				if (av_q2d(st->time_base) * st->info->rfps_duration_sum / st->info->duration_count < (1001 * 12.0 * 0.8) / get_std_framerate(j))
					continue;

				for (k = 0; k<2; k++) {
					int n = st->info->duration_count;
					double a = st->info->duration_error[k][0][j] / n;
					double error = st->info->duration_error[k][1][j] / n - a*a;

					if (error < best_error && best_error> 0.000000001) {
						best_error = error;
						num = get_std_framerate(j);
					}
					if (error < 0.02)
						av_log(ic, AV_LOG_DEBUG, "rfps: %f %f\n", get_std_framerate(j) / 12.0 / 1001, error);
				}
			}
			// do not increase frame rate by more than 1 % in order to match a standard rate.
			if (num && (!ref_rate.num || (double)num / (12 * 1001) < 1.01 * av_q2d(ref_rate)))
				av_reduce(&st->r_frame_rate.num, &st->r_frame_rate.den, num, 12 * 1001, INT_MAX);
		}
		if (!st->avg_frame_rate.num
			&& st->r_frame_rate.num && st->info->rfps_duration_sum
			&& st->info->codec_info_duration <= 0
			&& st->info->duration_count > 2
			&& fabs(1.0 / (av_q2d(st->r_frame_rate) * av_q2d(st->time_base)) - st->info->rfps_duration_sum / (double)st->info->duration_count) <= 1.0
			) {
			av_log(ic, AV_LOG_DEBUG, "Setting avg frame rate based on r frame rate\n");
			st->avg_frame_rate = st->r_frame_rate;
		}

		av_freep(&st->info->duration_error);
		st->info->last_dts = AV_NOPTS_VALUE;
		st->info->duration_count = 0;
		st->info->rfps_duration_sum = 0;
	}
}

static int ff_check_interrupt(AVIOInterruptCB *cb)
{
	int ret;
	if (cb && cb->callback && (ret = cb->callback(cb->opaque)))
		return ret;
	return 0;
}

int self_avformat_find_stream_info(AVFormatContext *ic, AVDictionary **options)
{
	int i, count, ret = 0, j;
	int64_t read_size;
	AVStream *st;
	AVPacket pkt1, *pkt;
	int64_t old_offset = avio_tell(ic->pb);
	// new streams might appear, no options for those
	int orig_nb_streams = ic->nb_streams;
	int flush_codecs;
#if FF_API_PROBESIZE_32
	int64_t max_analyze_duration = ic->max_analyze_duration2;
#else
	int64_t max_analyze_duration = ic->max_analyze_duration;
#endif
	int64_t max_stream_analyze_duration;
	int64_t max_subtitle_analyze_duration;
#if FF_API_PROBESIZE_32
	int64_t probesize = ic->probesize2;
#else
	int64_t probesize = ic->probesize;
#endif

	if (!max_analyze_duration)
		max_analyze_duration = ic->max_analyze_duration;
	if (ic->probesize)
		probesize = ic->probesize;
	flush_codecs = probesize > 0;

	av_opt_set(ic, "skip_clear", "1", AV_OPT_SEARCH_CHILDREN);

	max_stream_analyze_duration = max_analyze_duration;
	max_subtitle_analyze_duration = max_analyze_duration;
	if (!max_analyze_duration) {
		max_stream_analyze_duration =
			max_analyze_duration = 5 * AV_TIME_BASE;
		max_subtitle_analyze_duration = 30 * AV_TIME_BASE;
		if (!strcmp(ic->iformat->name, "flv"))
			max_stream_analyze_duration = 30 * AV_TIME_BASE;
	}
	printf("1111 %lld\n", av_gettime() / 1000);
	if (ic->pb)
		av_log(ic, AV_LOG_DEBUG, "Before avformat_find_stream_info() pos: %" "lld" " bytes read:%" "lld" " seeks:%d\n",
			avio_tell(ic->pb), ic->pb->bytes_read, ic->pb->seek_count);

	printf("2222 %lld\n", av_gettime() / 1000);
	for (i = 0; i < ic->nb_streams; i++) {
		const AVCodec *codec;
		AVDictionary *thread_opt = NULL;
		st = ic->streams[i];

		if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
			st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) {
			/*            if (!st->time_base.num)
			st->time_base = */
			if (!st->codec->time_base.num)
				st->codec->time_base = st->time_base;
		}
		// only for the split stuff
		if (!st->parser && !(ic->flags & AVFMT_FLAG_NOPARSE) && st->request_probe <= 0) {
			st->parser = av_parser_init(st->codec->codec_id);
			if (st->parser) {
				if (st->need_parsing == AVSTREAM_PARSE_HEADERS) {
					st->parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
				}
				else if (st->need_parsing == AVSTREAM_PARSE_FULL_RAW) {
					st->parser->flags |= PARSER_FLAG_USE_CODEC_TS;
				}
			}
			else if (st->need_parsing) {
				av_log(ic, AV_LOG_VERBOSE, "parser not found for codec "
					"%s, packets or times may be invalid.\n",
					avcodec_get_name(st->codec->codec_id));
			}
		}
		printf("    2222->1 %lld\n", av_gettime() / 1000);
		codec = find_decoder(ic, st, st->codec->codec_id);
		printf("    2222->2 %lld\n", av_gettime() / 1000);
		/* Force thread count to 1 since the H.264 decoder will not extract
		* SPS and PPS to extradata during multi-threaded decoding. */
		av_dict_set(options ? &options[i] : &thread_opt, "threads", "1", 0);

		if (ic->codec_whitelist)
			av_dict_set(options ? &options[i] : &thread_opt, "codec_whitelist", ic->codec_whitelist, 0);
		printf("    2222->3 %lld\n", av_gettime() / 1000);
		/* Ensure that subtitle_header is properly set. */
		if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE
			&& codec && !st->codec->codec) {
			if (avcodec_open2(st->codec, codec, options ? &options[i] : &thread_opt) < 0)
				av_log(ic, AV_LOG_WARNING,
					"Failed to open codec in av_find_stream_info\n");
		}
		printf("    2222->4 %lld\n", av_gettime() / 1000);
		// Try to just open decoders, in case this is enough to get parameters.
		if (!has_codec_parameters(st, NULL) && st->request_probe <= 0) {
			if (codec && !st->codec->codec)
				if (avcodec_open2(st->codec, codec, options ? &options[i] : &thread_opt) < 0)
					av_log(ic, AV_LOG_WARNING,
						"Failed to open codec in av_find_stream_info\n");
		}
		printf("    2222->5 %lld\n", av_gettime() / 1000);
		if (!options)
			av_dict_free(&thread_opt);
	}

	printf("3333 %lld\n", av_gettime() / 1000);
	for (i = 0; i < ic->nb_streams; i++) {
#if FF_API_R_FRAME_RATE
		ic->streams[i]->info->last_dts = AV_NOPTS_VALUE;
#endif
		ic->streams[i]->info->fps_first_dts = AV_NOPTS_VALUE;
		ic->streams[i]->info->fps_last_dts = AV_NOPTS_VALUE;
	}
	printf("4444 %lld\n", av_gettime() / 1000);
	count = 0;
	read_size = 0;
	for (;;) {
		int analyzed_all_streams;
		printf("    4444->1 %lld\n", av_gettime() / 1000);
		if (ff_check_interrupt(&ic->interrupt_callback)) {
			ret = AVERROR_EXIT;
			av_log(ic, AV_LOG_DEBUG, "interrupted\n");
			break;
		}
		printf("    4444->2 %lld\n", av_gettime() / 1000);
		/* check if one codec still needs to be handled */
		for (i = 0; i < ic->nb_streams; i++) {
			int fps_analyze_framecount = 20;

			st = ic->streams[i];
			if (!has_codec_parameters(st, NULL))
				break;
			/* If the timebase is coarse (like the usual millisecond precision
			* of mkv), we need to analyze more frames to reliably arrive at
			* the correct fps. */
			if (av_q2d(st->time_base) > 0.0005)
				fps_analyze_framecount *= 2;
			if (!tb_unreliable(st->codec))
				fps_analyze_framecount = 0;
			if (ic->fps_probe_size >= 0)
				fps_analyze_framecount = ic->fps_probe_size;
			if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
				fps_analyze_framecount = 0;
			/* variable fps and no guess at the real fps */
			if (!(st->r_frame_rate.num && st->avg_frame_rate.num) &&
				st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
				int count = (ic->iformat->flags & AVFMT_NOTIMESTAMPS) ?
					st->info->codec_info_duration_fields / 2 :
					st->info->duration_count;
				if (count < fps_analyze_framecount)
					break;
			}
			if (st->parser && st->parser->parser->split &&
				!st->codec->extradata)
				break;
			if (st->first_dts == AV_NOPTS_VALUE &&
				!(ic->iformat->flags & AVFMT_NOTIMESTAMPS) &&
				st->codec_info_nb_frames < ic->max_ts_probe &&
				(st->codec->codec_type == AVMEDIA_TYPE_VIDEO ||
					st->codec->codec_type == AVMEDIA_TYPE_AUDIO))
				break;
		}
		printf("    4444->3 %lld\n", av_gettime() / 1000);
		analyzed_all_streams = 0;
		if (i == ic->nb_streams) {
			analyzed_all_streams = 1;
			/* NOTE: If the format has no header, then we need to read some
			* packets to get most of the streams, so we cannot stop here. */
			if (!(ic->ctx_flags & AVFMTCTX_NOHEADER)) {
				/* If we found the info for all the codecs, we can stop. */
				ret = count;
				av_log(ic, AV_LOG_DEBUG, "All info found\n");
				flush_codecs = 0;
				break;
			}
		}
		printf("    4444->4 %lld\n", av_gettime() / 1000);
		/* We did not get all the codec info, but we read too much data. */
		if (read_size >= probesize) {
			ret = count;
			av_log(ic, AV_LOG_DEBUG,
				"Probe buffer size limit of %" "lld" " bytes reached\n", probesize);
			for (i = 0; i < ic->nb_streams; i++)
				if (!ic->streams[i]->r_frame_rate.num &&
					ic->streams[i]->info->duration_count <= 1 &&
					ic->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO &&
					strcmp(ic->iformat->name, "image2"))
					av_log(ic, AV_LOG_WARNING,
						"Stream #%d: not enough frames to estimate rate; "
						"consider increasing probesize\n", i);
			break;
		}
		printf("    4444->5 %lld\n", av_gettime() / 1000);
		/* NOTE: A new stream can be added there if no header in file
		* (AVFMTCTX_NOHEADER). */
		ret = read_frame_internal(ic, &pkt1);
		if (ret == AVERROR(EAGAIN))
			continue;

		if (ret < 0) {
			/* EOF or error*/
			break;
		}
		printf("    4444->6 %lld\n", av_gettime() / 1000);
		if (ic->flags & AVFMT_FLAG_NOBUFFER)
			free_packet_buffer(&ic->internal->packet_buffer,
				&ic->internal->packet_buffer_end);
		{
			pkt = add_to_pktbuf(&ic->internal->packet_buffer, &pkt1,
				&ic->internal->packet_buffer_end);
			if (!pkt) {
				ret = AVERROR(ENOMEM);
				goto find_stream_info_err;
			}
			if ((ret = av_dup_packet(pkt)) < 0)
				goto find_stream_info_err;
		}
		printf("    4444->7 %lld\n", av_gettime() / 1000);
		st = ic->streams[pkt->stream_index];
		if (!(st->disposition & AV_DISPOSITION_ATTACHED_PIC))
			read_size += pkt->size;

		if (pkt->dts != AV_NOPTS_VALUE && st->codec_info_nb_frames > 1) {
			/* check for non-increasing dts */
			if (st->info->fps_last_dts != AV_NOPTS_VALUE &&
				st->info->fps_last_dts >= pkt->dts) {
				av_log(ic, AV_LOG_DEBUG,
					"Non-increasing DTS in stream %d: packet %d with DTS "
					"%" "lld"", packet %d with DTS %" "lld""\n",
					st->index, st->info->fps_last_dts_idx,
					st->info->fps_last_dts, st->codec_info_nb_frames,
					pkt->dts);
				st->info->fps_first_dts =
					st->info->fps_last_dts = AV_NOPTS_VALUE;
			}
			/* Check for a discontinuity in dts. If the difference in dts
			* is more than 1000 times the average packet duration in the
			* sequence, we treat it as a discontinuity. */
			if (st->info->fps_last_dts != AV_NOPTS_VALUE &&
				st->info->fps_last_dts_idx > st->info->fps_first_dts_idx &&
				(pkt->dts - st->info->fps_last_dts) / 1000 >
				(st->info->fps_last_dts - st->info->fps_first_dts) /
				(st->info->fps_last_dts_idx - st->info->fps_first_dts_idx)) {
				av_log(ic, AV_LOG_WARNING,
					"DTS discontinuity in stream %d: packet %d with DTS "
					"%" "lld"", packet %d with DTS %" "lld""\n",
					st->index, st->info->fps_last_dts_idx,
					st->info->fps_last_dts, st->codec_info_nb_frames,
					pkt->dts);
				st->info->fps_first_dts =
					st->info->fps_last_dts = AV_NOPTS_VALUE;
			}

			/* update stored dts values */
			if (st->info->fps_first_dts == AV_NOPTS_VALUE) {
				st->info->fps_first_dts = pkt->dts;
				st->info->fps_first_dts_idx = st->codec_info_nb_frames;
			}
			st->info->fps_last_dts = pkt->dts;
			st->info->fps_last_dts_idx = st->codec_info_nb_frames;
		}
		printf("    4444->8 %lld\n", av_gettime() / 1000);
		if (st->codec_info_nb_frames>1) {
			int64_t t = 0;
			int64_t limit;

			if (st->time_base.den > 0) {
				t = av_rescale_q(st->info->codec_info_duration, st->time_base, AV_TIME_BASE_Q);
			}
				
			if (st->avg_frame_rate.num > 0)
				t = FFMAX((t), (av_rescale_q(st->codec_info_nb_frames, av_inv_q(st->avg_frame_rate), AV_TIME_BASE_Q)) ) ;

			if (t == 0
				&& st->codec_info_nb_frames>30
				&& st->info->fps_first_dts != AV_NOPTS_VALUE
				&& st->info->fps_last_dts != AV_NOPTS_VALUE)
				t = FFMAX(t, av_rescale_q(st->info->fps_last_dts - st->info->fps_first_dts, st->time_base, AV_TIME_BASE_Q)) ;

			if (analyzed_all_streams)                                limit = max_analyze_duration;
			else if (st->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) limit = max_subtitle_analyze_duration;
			else                                                     limit = max_stream_analyze_duration;

			if (t >= limit) {
				av_log(ic, AV_LOG_VERBOSE, "max_analyze_duration %" "lld"" reached at %" "lld"" microseconds st:%d\n",
					max_analyze_duration,
					t, pkt->stream_index);
				if (ic->flags & AVFMT_FLAG_NOBUFFER)
					av_packet_unref(pkt);
				break;
			}
			if (pkt->duration) {
				st->info->codec_info_duration += pkt->duration;
				st->info->codec_info_duration_fields += st->parser && st->need_parsing && st->codec->ticks_per_frame == 2 ? st->parser->repeat_pict + 1 : 2;
			}
		}
		printf("    4444->9 %lld\n", av_gettime() / 1000);
#if FF_API_R_FRAME_RATE
		if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			ff_rfps_add_frame(ic, st, pkt->dts);
#endif
		printf("    4444->10 %lld\n", av_gettime() / 1000);
		if (st->parser && st->parser->parser->split && !st->codec->extradata) {
			int i = st->parser->parser->split(st->codec, pkt->data, pkt->size);
			if (i > 0 && i < FF_MAX_EXTRADATA_SIZE) {
				if (ff_alloc_extradata(st->codec, i))
					return AVERROR(ENOMEM);
				memcpy(st->codec->extradata, pkt->data,
					st->codec->extradata_size);
			}
		}
		printf("    4444->11 %lld\n", av_gettime() / 1000);
		/* If still no information, we try to open the codec and to
		* decompress the frame. We try to avoid that in most cases as
		* it takes longer and uses more memory. For MPEG-4, we need to
		* decompress for QuickTime.
		*
		* If AV_CODEC_CAP_CHANNEL_CONF is set this will force decoding of at
		* least one frame of codec data, this makes sure the codec initializes
		* the channel configuration and does not only trust the values from
		* the container. */
		try_decode_frame(ic, st, pkt,
			(options && i < orig_nb_streams) ? &options[i] : NULL);
		printf("    4444->12 %lld\n", av_gettime() / 1000);
		if (ic->flags & AVFMT_FLAG_NOBUFFER) {
			av_packet_unref(pkt);
			printf("    4444->13 %lld\n", av_gettime() / 1000);
		}
			
		st->codec_info_nb_frames++;
		count++;
	}
	printf("5555 %lld\n", av_gettime() / 1000);
	if (flush_codecs) {
		AVPacket empty_pkt = { 0 };
		int err = 0;
		av_init_packet(&empty_pkt);

		for (i = 0; i < ic->nb_streams; i++) {

			st = ic->streams[i];

			/* flush the decoders */
			if (st->info->found_decoder == 1) {
				printf("    5555->1 %lld\n", av_gettime() / 1000);
				do {
					err = try_decode_frame(ic, st, &empty_pkt,
						(options && i < orig_nb_streams)
						? &options[i] : NULL);
				} while (err > 0 && !has_codec_parameters(st, NULL));
				printf("    5555->2 %lld\n", av_gettime() / 1000);
				if (err < 0) {
					av_log(ic, AV_LOG_INFO,
						"decoding for stream %d failed\n", st->index);
				}
			}
		}
	}
	printf("6666 %lld\n", av_gettime() / 1000);
	// close codecs which were opened in try_decode_frame()
	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		avcodec_close(st->codec);
	}
	printf("7777 %lld\n", av_gettime() / 1000);
	ff_rfps_calculate(ic);
	printf("8888 %lld\n", av_gettime() / 1000);
	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			if (st->codec->codec_id == AV_CODEC_ID_RAWVIDEO && !st->codec->codec_tag && !st->codec->bits_per_coded_sample) {
				uint32_t tag = avcodec_pix_fmt_to_codec_tag(st->codec->pix_fmt);
				goto find_stream_info_err;
				/*if (avpriv_find_pix_fmt(avpriv_get_raw_pix_fmt_tags(), tag) == st->codec->pix_fmt)
					st->codec->codec_tag = tag;*/
			}
			printf("    8888->1 %lld\n", av_gettime() / 1000);
			/* estimate average framerate if not set by demuxer */
			if (st->info->codec_info_duration_fields &&
				!st->avg_frame_rate.num &&
				st->info->codec_info_duration) {
				int best_fps = 0;
				double best_error = 0.01;

				if (st->info->codec_info_duration >= INT64_MAX / st->time_base.num / 2 ||
					st->info->codec_info_duration_fields >= INT64_MAX / st->time_base.den ||
					st->info->codec_info_duration        < 0)
					continue;
				printf("    8888->2 %lld\n", av_gettime() / 1000);
				av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
					st->info->codec_info_duration_fields * (int64_t)st->time_base.den,
					st->info->codec_info_duration * 2 * (int64_t)st->time_base.num, 60000);
				printf("    8888->3 %lld\n", av_gettime() / 1000);
				/* Round guessed framerate to a "standard" framerate if it's
				* within 1% of the original estimate. */
				for (j = 0; j < MAX_STD_TIMEBASES; j++) {
					AVRational std_fps = { get_std_framerate(j), 12 * 1001 };
					double error = fabs(av_q2d(st->avg_frame_rate) /
						av_q2d(std_fps) - 1);

					if (error < best_error) {
						best_error = error;
						best_fps = std_fps.num;
					}
				}
				printf("    8888->4 %lld\n", av_gettime() / 1000);
				if (best_fps)
					av_reduce(&st->avg_frame_rate.num, &st->avg_frame_rate.den,
						best_fps, 12 * 1001, INT_MAX);
				printf("    8888->5 %lld\n", av_gettime() / 1000);
			}

			if (!st->r_frame_rate.num) {
				if (st->codec->time_base.den * (int64_t)st->time_base.num
					<= st->codec->time_base.num * st->codec->ticks_per_frame * (int64_t)st->time_base.den) {
					st->r_frame_rate.num = st->codec->time_base.den;
					st->r_frame_rate.den = st->codec->time_base.num * st->codec->ticks_per_frame;
				}
				else {
					st->r_frame_rate.num = st->time_base.den;
					st->r_frame_rate.den = st->time_base.num;
				}
			}
			if (st->display_aspect_ratio.num && st->display_aspect_ratio.den) {
				AVRational hw_ratio = { st->codec->height, st->codec->width };
				st->sample_aspect_ratio = av_mul_q(st->display_aspect_ratio,
					hw_ratio);
			}
		}
		else if (st->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
			if (!st->codec->bits_per_coded_sample)
				st->codec->bits_per_coded_sample =
				av_get_bits_per_sample(st->codec->codec_id);
			// set stream disposition based on audio service type
			switch (st->codec->audio_service_type) {
			case AV_AUDIO_SERVICE_TYPE_EFFECTS:
				st->disposition = AV_DISPOSITION_CLEAN_EFFECTS;
				break;
			case AV_AUDIO_SERVICE_TYPE_VISUALLY_IMPAIRED:
				st->disposition = AV_DISPOSITION_VISUAL_IMPAIRED;
				break;
			case AV_AUDIO_SERVICE_TYPE_HEARING_IMPAIRED:
				st->disposition = AV_DISPOSITION_HEARING_IMPAIRED;
				break;
			case AV_AUDIO_SERVICE_TYPE_COMMENTARY:
				st->disposition = AV_DISPOSITION_COMMENT;
				break;
			case AV_AUDIO_SERVICE_TYPE_KARAOKE:
				st->disposition = AV_DISPOSITION_KARAOKE;
				break;
			}
		}
	}
	printf("9999 %lld\n", av_gettime() / 1000);
	if (probesize)
		estimate_timings(ic, old_offset);

	av_opt_set(ic, "skip_clear", "0", AV_OPT_SEARCH_CHILDREN);

	if (ret >= 0 && ic->nb_streams)
		/* We could not have all the codec parameters before EOF. */
		ret = -1;
	for (i = 0; i < ic->nb_streams; i++) {
		const char *errmsg;
		st = ic->streams[i];
		if (!has_codec_parameters(st, &errmsg)) {
			char buf[256];
			avcodec_string(buf, sizeof(buf), st->codec, 0);
			av_log(ic, AV_LOG_WARNING,
				"Could not find codec parameters for stream %d (%s): %s\n"
				"Consider increasing the value for the 'analyzeduration' and 'probesize' options\n",
				i, buf, errmsg);
		}
		else {
			ret = 0;
		}
	}
	printf("10101010 %lld\n", av_gettime() / 1000);
	compute_chapters_end(ic);
	printf("11111111 %lld\n", av_gettime() / 1000);
find_stream_info_err:
	for (i = 0; i < ic->nb_streams; i++) {
		st = ic->streams[i];
		if (ic->streams[i]->codec->codec_type != AVMEDIA_TYPE_AUDIO)
			ic->streams[i]->codec->thread_count = 0;
		if (st->info)
			av_freep(&st->info->duration_error);
		av_freep(&ic->streams[i]->info);
	}
	if (ic->pb)
		av_log(ic, AV_LOG_DEBUG, "After avformat_find_stream_info() pos: %" "lld"" bytes read:%" "lld"" seeks:%d frames:%d\n",
			avio_tell(ic->pb), ic->pb->bytes_read, ic->pb->seek_count, count);
	printf("12121212 %lld\n", av_gettime() / 1000);
	return ret;
}
