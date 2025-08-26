#ifndef video_reader_hpp
#define video_reader_hpp

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <inttypes.h>
}

struct VideoReaderState {
    // Public
    int width, height;
    AVRational time_base;

    // Private internal state
    AVFormatContext* av_format_ctx;
    AVCodecContext*  av_codec_ctx;
    int              video_stream_index;
    AVFrame*         av_frame;
    AVPacket*        av_packet;
    SwsContext*      sws_scaler_ctx;
};

bool video_reader_open(VideoReaderState* state, const char* filename);
bool video_reader_read_frame(VideoReaderState* state, uint8_t* frame_buffer, int64_t* pts);
void video_reader_close(VideoReaderState* state);


// NEW: seek (seconds, stream time_base ile hesaplanır)
bool video_reader_seek(VideoReaderState* state, double seconds);

#endif
