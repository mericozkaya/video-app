//sound_reader.cpp

extern "C" {
#include <libavutil/error.h>
#include <libavutil/channel_layout.h>
}
#include "sound_reader.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

static inline const char* err2str(int e) {
    static thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(e, buf, sizeof(buf));
    return buf;
}

bool sound_reader_open(SoundReaderState* st, const char* filename,
                       int dst_sample_rate, int dst_channels,
                       AVSampleFormat dst_fmt) {
    st->dst_sample_rate = dst_sample_rate;
    st->dst_channels    = dst_channels;
    st->dst_fmt         = dst_fmt;
    st->dst_ch_layout   = av_get_default_channel_layout(dst_channels);

    int ret = 0;

    ret = avformat_open_input(&st->fmt, filename, nullptr, nullptr);
    if (ret < 0) { std::printf("audio: open_input failed: %s\n", err2str(ret)); return false; }

    ret = avformat_find_stream_info(st->fmt, nullptr);
    if (ret < 0) { std::printf("audio: find_stream_info failed: %s\n", err2str(ret)); return false; }

    // Find first audio stream
    const AVCodec* dec = nullptr;
    AVCodecParameters* params = nullptr;
    for (unsigned i = 0; i < st->fmt->nb_streams; ++i) {
        if (st->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            st->stream_index = (int)i;
            params = st->fmt->streams[i]->codecpar;
            dec = avcodec_find_decoder(params->codec_id);
            if (dec) break;
        }
    }
    if (st->stream_index < 0 || !dec) {
        std::printf("audio: no audio stream/decoder\n");
        return false;
    }

    st->dec = avcodec_alloc_context3(dec);
    if (!st->dec) { std::printf("audio: alloc context failed\n"); return false; }

    ret = avcodec_parameters_to_context(st->dec, params);
    if (ret < 0) { std::printf("audio: params_to_ctx failed: %s\n", err2str(ret)); return false; }

    ret = avcodec_open2(st->dec, dec, nullptr);
    if (ret < 0) { std::printf("audio: open2 failed: %s\n", err2str(ret)); return false; }

    st->frame = av_frame_alloc();
    st->pkt   = av_packet_alloc();
    if (!st->frame || !st->pkt) { std::printf("audio: frame/pkt alloc failed\n"); return false; }

    // Resampler
    st->src_sample_rate = st->dec->sample_rate;
#if LIBAVUTIL_VERSION_MAJOR >= 57
    st->src_ch_layout = st->dec->ch_layout.u.mask ? (int64_t)st->dec->ch_layout.u.mask
                                                  : av_get_default_channel_layout(st->dec->ch_layout.nb_channels);
#else
    st->src_ch_layout = st->dec->channel_layout ? st->dec->channel_layout
                                                : av_get_default_channel_layout(st->dec->channels);
#endif

    st->swr = swr_alloc_set_opts(nullptr,
                                 st->dst_ch_layout, st->dst_fmt, st->dst_sample_rate,
                                 st->src_ch_layout, st->dec->sample_fmt, st->src_sample_rate,
                                 0, nullptr);
    if (!st->swr) { std::printf("audio: swr_alloc_set_opts failed\n"); return false; }

    ret = swr_init(st->swr);
    if (ret < 0) { std::printf("audio: swr_init failed: %s\n", err2str(ret)); return false; }

    st->time_base = st->fmt->streams[st->stream_index]->time_base;
    return true;
}

bool sound_reader_read(SoundReaderState* st,
                       uint8_t** out_data, int* out_nbytes,
                       double* pts_start_sec, double* pts_end_sec) {
    *out_data = nullptr; *out_nbytes = 0;
    *pts_start_sec = 0.0; *pts_end_sec = 0.0;

    int ret = 0;

    // Bir frame decode edene kadar oku
    while ((ret = av_read_frame(st->fmt, st->pkt)) >= 0) {
        if (st->pkt->stream_index != st->stream_index) {
            av_packet_unref(st->pkt);
            continue;
        }

        ret = avcodec_send_packet(st->dec, st->pkt);
        av_packet_unref(st->pkt);
        if (ret < 0) {
            std::printf("audio: send_packet: %s\n", err2str(ret));
            return false;
        }

        ret = avcodec_receive_frame(st->dec, st->frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret == AVERROR_EOF) {
            return false; // bitti
        } else if (ret < 0) {
            std::printf("audio: receive_frame: %s\n", err2str(ret));
            return false;
        }

        // PTS (saniye)
        int64_t ts = (st->frame->best_effort_timestamp == AV_NOPTS_VALUE)
                       ? st->frame->pts : st->frame->best_effort_timestamp;
        *pts_start_sec = ts * (double)st->time_base.num / (double)st->time_base.den;

        // Gerekli çıktı örnek sayısı
        int64_t delay = swr_get_delay(st->swr, st->src_sample_rate);
        int out_count = (int)av_rescale_rnd(delay + st->frame->nb_samples,
                                            st->dst_sample_rate, st->src_sample_rate, AV_ROUND_UP);

        // Çıkış buffer (interleaved)
        int out_linesize = 0;
        uint8_t* out_buf = nullptr;
        int ret_alloc = av_samples_alloc(&out_buf, &out_linesize,
                                         st->dst_channels, out_count, st->dst_fmt, 0);
        if (ret_alloc < 0) {
            std::printf("audio: av_samples_alloc failed: %s\n", err2str(ret_alloc));
            return false;
        }

        // Resample
        uint8_t** in_data = st->frame->extended_data;
        int out_samples = swr_convert(st->swr, &out_buf, out_count,
                                      (const uint8_t**)in_data, st->frame->nb_samples);
        if (out_samples < 0) {
            std::printf("audio: swr_convert failed\n");
            av_freep(&out_buf);
            return false;
        }

        int bytes_per_sample = av_get_bytes_per_sample(st->dst_fmt);
        int total_bytes = out_samples * st->dst_channels * bytes_per_sample;

        // Çağıran delete[] ile silebilsin diye kopyalıyoruz
        uint8_t* interleaved = new uint8_t[total_bytes];
        std::memcpy(interleaved, out_buf, total_bytes);
        av_freep(&out_buf);

        *out_data  = interleaved;
        *out_nbytes = total_bytes;
        *pts_end_sec = *pts_start_sec + (double)out_samples / (double)st->dst_sample_rate;

        av_frame_unref(st->frame);
        return true;
    }

    return false; // EOF
}

void sound_reader_close(SoundReaderState* st) {
    if (st->swr)   swr_free(&st->swr);
    if (st->dec)   avcodec_free_context(&st->dec);
    if (st->fmt)   { avformat_close_input(&st->fmt); avformat_free_context(st->fmt); }
    if (st->frame) av_frame_free(&st->frame);
    if (st->pkt)   av_packet_free(&st->pkt);
}
