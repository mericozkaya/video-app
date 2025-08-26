#ifndef sound_reader_hpp
#define sound_reader_hpp

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
}
#include <cstdint>

struct SoundReaderState {
    // Public
    int dst_sample_rate;
    int dst_channels;
    AVSampleFormat dst_fmt;
    AVRational time_base;

    // Private
    AVFormatContext* fmt = nullptr;
    AVCodecContext*  dec = nullptr;
    int              stream_index = -1;
    AVFrame*         frame = nullptr;
    AVPacket*        pkt   = nullptr;
    SwrContext*      swr   = nullptr;
    int64_t          dst_ch_layout = 0; // (deprecated API kullanımıyla uyumlu)
    int64_t          src_ch_layout = 0;
    int              src_sample_rate = 0;
};

bool sound_reader_open(SoundReaderState* st, const char* filename,
                       int dst_sample_rate = 48000,
                       int dst_channels    = 2,
                       AVSampleFormat dst_fmt = AV_SAMPLE_FMT_S16);

bool sound_reader_read(SoundReaderState* st,
                       uint8_t** out_data, int* out_nbytes,
                       double* pts_start_sec, double* pts_end_sec);

void sound_reader_close(SoundReaderState* st);

// NEW: seek (seconds)
bool sound_reader_seek(SoundReaderState* st, double seconds);

#endif
