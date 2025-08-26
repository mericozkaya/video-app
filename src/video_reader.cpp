extern "C" {
#include <libavutil/error.h>
}
#include <cmath>
#include <cstdio>
#include "video_reader.hpp"

// C++ uyumlu wrapper
static inline const char* av_err2str_cpp(int errnum) {
    static thread_local char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(errnum, buf, sizeof(buf));
    return buf;
}
#ifdef av_err2str
#undef av_err2str
#endif
#define av_err2str(e) av_err2str_cpp((e))

bool video_reader_open(VideoReaderState* state, const char* filename) {
    auto& width            = state->width;
    auto& height           = state->height;
    auto& time_base        = state->time_base;
    auto& av_format_ctx    = state->av_format_ctx;
    auto& av_codec_ctx     = state->av_codec_ctx;
    auto& video_stream_idx = state->video_stream_index;
    auto& av_frame         = state->av_frame;
    auto& av_packet        = state->av_packet;

    av_format_ctx = avformat_alloc_context();
    if (!av_format_ctx) {
        std::printf("Couldn't created AVFormatContext\n");
        return false;
    }
    int err = avformat_open_input(&av_format_ctx, filename, NULL, NULL);
    if (err < 0) {
        std::fprintf(stderr, "Couldn't open video file '%s': %s\n", filename, av_err2str(err));
        return false;
    }

    video_stream_idx = -1;
    AVCodecParameters* av_codec_params = nullptr;
    const AVCodec* av_codec = nullptr;

    for (unsigned i = 0; i < av_format_ctx->nb_streams; ++i) {
        auto* st = av_format_ctx->streams[i];
        auto* params = st->codecpar;
        const AVCodec* dec = avcodec_find_decoder(params->codec_id);
        if (!dec) continue;
        if (params->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = (int)i;
            av_codec_params = params;
            av_codec = dec;
            width  = params->width;
            height = params->height;
            time_base = st->time_base;
            break;
        }
    }
    if (video_stream_idx == -1) {
        std::printf("Couldn't find valid video stream inside file\n");
        return false;
    }

    av_codec_ctx = avcodec_alloc_context3(av_codec);
    if (!av_codec_ctx) { std::printf("Couldn't create AVCodecContext\n"); return false; }
    if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0) {
        std::printf("Couldn't initialize AVCodecContext\n"); return false;
    }
    if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0) {
        std::printf("Couldn't open codec\n"); return false;
    }

    av_frame  = av_frame_alloc();
    av_packet = av_packet_alloc();
    if (!av_frame || !av_packet) {
        std::printf("Couldn't allocate AVFrame/AVPacket\n");
        return false;
    }
    state->sws_scaler_ctx = nullptr;
    return true;
}

bool video_reader_read_frame(VideoReaderState* state, uint8_t* frame_buffer, int64_t* pts) {
    auto& av_format_ctx    = state->av_format_ctx;
    auto& av_codec_ctx     = state->av_codec_ctx;
    auto& video_stream_idx = state->video_stream_index;
    auto& av_frame         = state->av_frame;
    auto& av_packet        = state->av_packet;
    auto& sws_scaler_ctx   = state->sws_scaler_ctx;
    auto& width            = state->width;
    auto& height           = state->height;

    int response = 0;
    while (av_read_frame(av_format_ctx, av_packet) >= 0) {
        if (av_packet->stream_index != video_stream_idx) {
            av_packet_unref(av_packet);
            continue;
        }
        response = avcodec_send_packet(av_codec_ctx, av_packet);
        av_packet_unref(av_packet);
        if (response < 0) {
            std::printf("Failed to decode packet: %s\n", av_err2str(response));
            return false;
        }
        response = avcodec_receive_frame(av_codec_ctx, av_frame);
        if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            continue;
        } else if (response < 0) {
            std::printf("Failed to receive frame: %s\n", av_err2str(response));
            return false;
        }
        break;
    }

    // PTS: best_effort_timestamp öncelikli
    int64_t ts = (av_frame->best_effort_timestamp == AV_NOPTS_VALUE)
                 ? av_frame->pts
                 : av_frame->best_effort_timestamp;
    *pts = ts;

    if (!sws_scaler_ctx) {
        sws_scaler_ctx = sws_getContext(width, height, av_codec_ctx->pix_fmt,
                                        width, height, AV_PIX_FMT_RGB0,
                                        SWS_BILINEAR, NULL, NULL, NULL);
    }
    if (!sws_scaler_ctx) {
        std::printf("Couldn't initialize sw scaler\n");
        return false;
    }
    uint8_t* dest[4] = { frame_buffer, NULL, NULL, NULL };
    int dest_linesize[4] = { width * 4, 0, 0, 0 };
    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, av_frame->height,
              dest, dest_linesize);
    return true;
}

bool video_reader_seek(VideoReaderState* s, double seconds) {
    if (!s || !s->av_format_ctx) return false;
    int64_t ts = (int64_t)llround(seconds * s->time_base.den / (double)s->time_base.num);
    if (av_seek_frame(s->av_format_ctx, s->video_stream_index, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(s->av_codec_ctx);
    if (s->av_packet) av_packet_unref(s->av_packet);
    if (s->av_frame)  av_frame_unref(s->av_frame);
    return true;
}

double video_reader_get_duration_sec(const VideoReaderState* s) {
    if (!s || !s->av_format_ctx) return 0.0;
    AVStream* st = s->av_format_ctx->streams[s->video_stream_index];
    if (st && st->duration > 0 && st->time_base.den != 0) {
        return st->duration * (double)st->time_base.num / (double)st->time_base.den;
    }
    if (s->av_format_ctx->duration > 0) {
        return (double)s->av_format_ctx->duration / (double)AV_TIME_BASE;
    }
    return 0.0; // bilinmiyor (ör. canlı yayın)
}

void video_reader_close(VideoReaderState* state) {
    sws_freeContext(state->sws_scaler_ctx);
    avformat_close_input(&state->av_format_ctx);
    avformat_free_context(state->av_format_ctx);
    av_frame_free(&state->av_frame);
    av_packet_free(&state->av_packet);
    avcodec_free_context(&state->av_codec_ctx);
}
