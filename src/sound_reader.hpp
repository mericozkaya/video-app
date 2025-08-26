//sound_reader.hpp

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
    int dst_sample_rate;   // ör: 48000
    int dst_channels;      // ör: 2
    AVSampleFormat dst_fmt; // ör: AV_SAMPLE_FMT_S16
    AVRational time_base;  // audio stream time_base

    // Private
    AVFormatContext* fmt = nullptr;
    AVCodecContext*  dec = nullptr;
    int              stream_index = -1;
    AVFrame*         frame = nullptr;
    AVPacket*        pkt   = nullptr;
    SwrContext*      swr   = nullptr;
    int64_t          dst_ch_layout = 0;
    int64_t          src_ch_layout = 0;
    int              src_sample_rate = 0;
};

bool sound_reader_open(SoundReaderState* st, const char* filename,
                       int dst_sample_rate = 48000,
                       int dst_channels    = 2,
                       AVSampleFormat dst_fmt = AV_SAMPLE_FMT_S16);

/// Bir miktar ses örneği döndürür:
/// out_data: new[] ile ayrılmış interleaved buffer (AUDIO_S16, stereo vb.); çağıran delete[] yapar
/// out_nbytes: out_data boyutu (byte)
/// pts_start_sec: bu chunk’ın PTS başlangıcı (saniye)
/// pts_end_sec:   bu chunk’ın PTS bitişi (saniye) = start + (num_samples / sample_rate)
bool sound_reader_read(SoundReaderState* st,
                       uint8_t** out_data, int* out_nbytes,
                       double* pts_start_sec, double* pts_end_sec);

void sound_reader_close(SoundReaderState* st);

#endif
