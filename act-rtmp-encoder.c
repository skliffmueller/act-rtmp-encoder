/*
 * http://ffmpeg.org/doxygen/trunk/index.html
 *
 * Main components
 *
 * Format (Container) - a wrapper, providing sync, metadata and muxing for the streams.
 * Stream - a continuous stream (audio or video) of data over time.
 * Codec - defines how data are enCOded (from Frame to Packet)
 *        and DECoded (from Packet to Frame).
 * Packet - are the data (kind of slices of the stream data) to be decoded as raw frames.
 * Frame - a decoded raw frame (to be encoded or filtered).
 */

#include <windows.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>


#include "keylogger-thread.h"

typedef struct RTMPContext {
    AVCodecParameters* pVideoCodecParameters;
    AVCodec* pVideoCodec;
    AVCodecContext* pVideoContext;
    AVStream* pVideoStream;
    int iVideoStreamIndex;

    AVCodecParameters* pAudioCodecParameters;
    AVCodec* pAudioCodec;
    AVCodecContext* pAudioContext;
    AVStream* pAudioStream;
    int iAudioStreamIndex;

    AVCodecParameters* pDataCodecParameters;
    AVStream* pDataStream;
    int iDataStreamIndex;
} RTMPContext;

 // print out the steps and errors
static void logging(const char* fmt, ...);

static int setupOutputStreams(AVFormatContext* pFormatContext, RTMPContext* pOutputContext, RTMPContext* pInputContext, const char* url);

static int setupInputFormatContext(AVFormatContext* pFormatContext, const char* url);

static int setupInputStreams(AVFormatContext* pFormatContext, RTMPContext* dInputContext, const char* url);

int setupOutputStreams(AVFormatContext* pFormatContext, RTMPContext* pOutputContext, RTMPContext* pInputContext, const char* url)
{
    AVCodecParameters* pDataCodecParameters = avcodec_parameters_alloc();
    AVCodec* pVideoCodec;
    AVCodec* pAudioCodec;
    AVCodecContext* pVideoContext;
    AVCodecContext* pAudioContext;
    AVStream* pVideoStream;
    AVStream* pAudioStream;
    AVStream* pDataStream;

    pVideoStream = avformat_new_stream(pFormatContext, NULL);
    pVideoCodec = avcodec_find_encoder((*pInputContext).pVideoContext->codec_id);
    pVideoContext = avcodec_alloc_context3(pVideoCodec);
    pVideoContext->bit_rate = 2400000;
    pVideoContext->width = (*pInputContext).pVideoContext->width;
    pVideoContext->height = (*pInputContext).pVideoContext->height;
    pVideoContext->time_base = (*pInputContext).pVideoStream->time_base;
    pVideoContext->framerate = (*pInputContext).pVideoStream->r_frame_rate;
    pVideoContext->sample_aspect_ratio = (*pInputContext).pVideoContext->sample_aspect_ratio;
    pVideoContext->pix_fmt = AV_PIX_FMT_YUV420P;


    if (avcodec_open2(pVideoContext, pVideoCodec, NULL) < 0)
    {
        logging("ERROR could not open output video codec context");
        return -1;
    }

    if (avcodec_parameters_from_context(pVideoStream->codecpar, pVideoContext) < 0)
    {
        logging("ERROR could not get the stream info");
        return -1;
    }

    pVideoStream->time_base = pVideoContext->time_base;

    pAudioStream = avformat_new_stream(pFormatContext, NULL);
    pAudioCodec = avcodec_find_encoder((*pInputContext).pAudioContext->codec_id);
    pAudioContext = avcodec_alloc_context3(pAudioCodec);

    pAudioContext->sample_rate = (*pInputContext).pAudioContext->sample_rate;
    pAudioContext->channel_layout = (*pInputContext).pAudioContext->channel_layout;
    pAudioContext->channels = av_get_channel_layout_nb_channels(pAudioContext->channel_layout);
    pAudioContext->sample_fmt = pAudioCodec->sample_fmts[0];
    pAudioContext->time_base = (AVRational){ 1, (*pInputContext).pAudioContext->sample_rate };

    if (avcodec_open2(pAudioContext, pAudioCodec, NULL) < 0)
    {
        logging("ERROR could not open output audio codec context");
        return -1;
    }

    if (avcodec_parameters_from_context(pAudioStream->codecpar, pAudioContext) < 0)
    {
        logging("ERROR could not get the stream info");
        return -1;
    }

    pAudioStream->time_base = pAudioContext->time_base;

    pDataCodecParameters->codec_id = AV_CODEC_ID_TEXT;
    pDataCodecParameters->codec_type = AVMEDIA_TYPE_DATA;
    pDataCodecParameters->codec_tag = 0;

    pDataStream = avformat_new_stream(pFormatContext, NULL);

    if (avcodec_parameters_copy(pDataStream->codecpar, pDataCodecParameters) < 0)
    {
        logging("ERROR could not get the stream info");
        return -1;
    }

    pDataStream->time_base = (AVRational){ 1, 240 };


    (*pOutputContext).pVideoCodec = pVideoCodec;
    (*pOutputContext).pAudioCodec = pAudioCodec;

    (*pOutputContext).pVideoContext = pVideoContext;
    (*pOutputContext).pAudioContext = pAudioContext;

    (*pOutputContext).pVideoStream = pVideoStream;
    (*pOutputContext).pAudioStream = pAudioStream;
    (*pOutputContext).pDataStream = pDataStream;

    (*pOutputContext).iVideoStreamIndex = pVideoStream->index;
    (*pOutputContext).iAudioStreamIndex = pAudioStream->index;
    (*pOutputContext).iDataStreamIndex = pDataStream->index;

    (*pOutputContext).pDataCodecParameters = pDataCodecParameters;

    av_dump_format(pFormatContext, 0, url, 1);

    if (!(pFormatContext->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&pFormatContext->pb, url, AVIO_FLAG_READ_WRITE) < 0) {
            logging("Could not open output file '%s'", url);
            return -1;
        }
    }

    if (avformat_write_header(pFormatContext, NULL) < 0) {
        logging("Error occurred when writing header to hls Format Context.\n");
        return -1;
    }

    return 0;
}

int setupInputFormatContext(AVFormatContext* pFormatContext, const char* url) {
    AVDictionary* pOptions = NULL;


    avformat_network_init();

    av_dict_set(&pOptions, "protocol_whitelist", "tcp,rtmp,rtmps,tls", 0);
    av_dict_set(&pOptions, "listen", "1", 0);

    if (avformat_open_input(&pFormatContext, url, NULL, &pOptions) != 0) {
        logging("ERROR could not open rtmp input stream.\n");
        return -1;
    }

    logging("format %s, duration %lld us, bit_rate %lld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

    if (avformat_find_stream_info(pFormatContext, NULL) < 0) {
        logging("ERROR could not get the stream info");
        return -1;
    }
    logging("rtmp stream information retrieved successfully.\n");



    return 0;
}

int setupInputStreams(AVFormatContext* pFormatContext, RTMPContext* dInputContext, const char* url) {
    int i;
    
    (*dInputContext).iVideoStreamIndex = -1;
    (*dInputContext).iAudioStreamIndex = -1;
    
    for (i = 0; i < pFormatContext->nb_streams; i++) {
        AVStream* pInputStream = pFormatContext->streams[i];

        AVCodecParameters* pInputCodecParams = pInputStream->codecpar;

        if (pInputCodecParams->codec_type != AVMEDIA_TYPE_AUDIO &&
            pInputCodecParams->codec_type != AVMEDIA_TYPE_VIDEO &&
            pInputCodecParams->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            continue;
        }

        logging("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        logging("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
        logging("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);


        AVCodec* pInputCodec = NULL;
        AVCodecContext* pInputCodecContext = NULL;

        // finds the registered decoder for a codec ID
        // https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga19a0ca553277f019dd5b0fec6e1f9dca
        pInputCodec = avcodec_find_decoder(pInputCodecParams->codec_id);

        if (pInputCodec == NULL) {
            logging("ERROR unsupported codec!\n");
            // In this example if the codec is not found we just skip it
            continue;
        }

        pInputCodecContext = avcodec_alloc_context3(pInputCodec);

        if (!pInputCodecContext)
        {
            logging("failed to allocated memory for AVCodecContext");
            return -1;
        }

        if (avcodec_parameters_to_context(pInputCodecContext, pInputCodecParams) < 0)
        {
            logging("failed to copy codec params to codec context");
            return -1;
        }

        // when the stream is a video we store its index, codec parameters and codec
        if (pInputCodecParams->codec_type == AVMEDIA_TYPE_VIDEO) {
            if ((*dInputContext).iVideoStreamIndex == -1) {
                if (avcodec_open2(pInputCodecContext, pInputCodec, NULL) < 0)
                {
                    logging("failed to open codec through avcodec_open2");
                    return -1;
                }

                (*dInputContext).iVideoStreamIndex = i;
                (*dInputContext).pVideoCodec = pInputCodec;
                (*dInputContext).pVideoContext = pInputCodecContext;
                (*dInputContext).pVideoCodecParameters = pInputCodecParams;
                (*dInputContext).pVideoStream = pInputStream;


            }

            logging("Video Codec: resolution %d x %d", pInputCodecParams->width, pInputCodecParams->height);
        }
        else if (pInputCodecParams->codec_type == AVMEDIA_TYPE_AUDIO) {
            if ((*dInputContext).iAudioStreamIndex == -1) {
                if (avcodec_open2(pInputCodecContext, pInputCodec, NULL) < 0)
                {
                    logging("failed to open codec through avcodec_open2");
                    return -1;
                }

                (*dInputContext).iAudioStreamIndex = i;
                (*dInputContext).pAudioCodec = pInputCodec;
                (*dInputContext).pAudioContext = pInputCodecContext;
                (*dInputContext).pAudioCodecParameters = pInputCodecParams;
                (*dInputContext).pAudioStream = pInputStream;
            }

            logging("Audio Codec: %d channels, sample rate %d", pInputCodecParams->channels, pInputCodecParams->sample_rate);
        }

        // print its name, id and bitrate
        logging("\tCodec %s ID %d bit_rate %lld", pInputCodec->name, pInputCodec->id, pInputCodecParams->bit_rate);
    }

    if ((*dInputContext).iVideoStreamIndex == -1) {
        logging("ERROR Unable to find video stream in rtmp format context");
        return -1;
    }

    av_dump_format(pFormatContext, 0, url, 0);

    return 0;
}

// ./something.exe [input_url] [output_url] [awsArnChannel]

int main(int argc, const char* argv[])
{
    const char* input_url;
    const char* output_url;
    const char* awsArnChannel;
    AVPacket* pInputPacket;
    AVPacket* pOutputPacket;

    AVFormatContext* pOutputFormatContext;
    AVFormatContext* pInputFormatContext;

    RTMPContext dRTMPInputContext;
    RTMPContext dRTMPOutputContext;
    KeyLoggerContext dKeyLoggerContext;

    pthread_t tid;

    if (argc <= 3) {
        fprintf(stderr, "Usage: %s <input_url> <output_url> <awsArnChannel>\n", argv[0]);
        fprintf(stderr, "Example: %s rtmp://127.0.0.1:1934/stream \\\n            rtmps://abcdef123456.global-contribute.live-video.net:443/app/tu_us-east-1_ABCD1234_cdefgh6789asde \\\n            arn:aws:ivs:us-east-1:1234567890:channel/NBspedS72SE\n", argv[0]);
        fprintf(stderr, "Note: AWS CLI environment variables (AWS_ACCESS_KEY_ID,AWS_SECRET_ACCESS_KEY) must be set.", argv[0]);

        exit(0);
    }
    input_url = argv[1];
    output_url = argv[2];
    awsArnChannel = argv[3];

    pInputFormatContext = avformat_alloc_context();

    setupInputFormatContext(pInputFormatContext, input_url);

    if (setupInputStreams(pInputFormatContext, &dRTMPInputContext, input_url) < 0) {
        logging("ERROR could not setup rtmp streams.\n");
        return -1;
    }

    pOutputFormatContext = avformat_alloc_context();

    if (avformat_alloc_output_context2(&pOutputFormatContext, NULL, "flv", output_url) < 0) {
        logging("ERROR could not open rtmp output stream.\n");
        return -1;
    }

    if (setupOutputStreams(pOutputFormatContext, &dRTMPOutputContext, &dRTMPInputContext, output_url) < 0) {
        logging("ERROR could not setup hls streams.\n");
        return -1;
    }

    pInputPacket = av_packet_alloc();
    pOutputPacket = av_packet_alloc();
    dKeyLoggerContext.pOutputPacket = av_packet_alloc();
    dKeyLoggerContext.ptsTimebase = &dRTMPOutputContext.pVideoStream->time_base;
    if (!pInputPacket)
    {
        logging("failed to allocate memory for AVPacket");
        return -1;
    }

    void* threadArgs[2] = { &dKeyLoggerContext, awsArnChannel };
    pthread_create(&tid, NULL, keyLoggerThread, threadArgs);

    int counter = 0;
    // fill the Packet with data from the Stream
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61

    while (1)
    {

        if (av_read_frame(pInputFormatContext, pInputPacket) < 0) {
            break;
        }

        

        // if it's the video stream
        if (pInputPacket->stream_index == dRTMPInputContext.iVideoStreamIndex) {
            int response = avcodec_send_packet(dRTMPInputContext.pVideoContext, pInputPacket);
            while (response >= 0) {

                AVFrame* pFrame = av_frame_alloc();

                response = avcodec_receive_frame(dRTMPInputContext.pVideoContext, pFrame);
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                }
                else if (response < 0) {
                    return response;
                }
                if (response >= 0) {
                    
                    int res = avcodec_send_frame(dRTMPOutputContext.pVideoContext, pFrame);

                    while (res >= 0) {
                        pOutputPacket = av_packet_alloc();
                        res = avcodec_receive_packet(dRTMPOutputContext.pVideoContext, pOutputPacket);
                        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                            break;
                        }
                        else if (res < 0) {
                            return -1;
                        }

                        pOutputPacket->stream_index = dRTMPOutputContext.iVideoStreamIndex;
                        pOutputPacket->pts = av_rescale_q_rnd(pInputPacket->pts, dRTMPOutputContext.pVideoContext->time_base, dRTMPOutputContext.pVideoStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        pOutputPacket->dts = av_rescale_q_rnd(pInputPacket->dts, dRTMPOutputContext.pVideoContext->time_base, dRTMPOutputContext.pVideoStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        pOutputPacket->duration = av_rescale_q(pInputPacket->duration, dRTMPOutputContext.pVideoContext->time_base, dRTMPOutputContext.pVideoStream->time_base);

                        av_packet_unref(dKeyLoggerContext.pOutputPacket);
                        dKeyLoggerContext.pOutputPacket = av_packet_clone(pOutputPacket);
                        res = av_interleaved_write_frame(pOutputFormatContext, pOutputPacket);
                    }
                    av_packet_unref(pOutputPacket);
                }
                av_frame_unref(pFrame);
            }

        } else if (pInputPacket->stream_index == dRTMPInputContext.iAudioStreamIndex) {
            int response = avcodec_send_packet(dRTMPInputContext.pAudioContext, pInputPacket);
            while (response >= 0) {

                AVFrame* pFrame = av_frame_alloc();


                response = avcodec_receive_frame(dRTMPInputContext.pAudioContext, pFrame);
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                }
                else if (response < 0) {
                    return response;
                }
                if (response >= 0) {

                    int res = avcodec_send_frame(dRTMPOutputContext.pAudioContext, pFrame);

                    while (res >= 0) {
                        pOutputPacket = av_packet_alloc();
                        res = avcodec_receive_packet(dRTMPOutputContext.pAudioContext, pOutputPacket);
                        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                            break;
                        }
                        else if (res < 0) {
                            return -1;
                        }

                        pOutputPacket->stream_index = dRTMPOutputContext.iAudioStreamIndex;
                        av_packet_rescale_ts(pOutputPacket, dRTMPInputContext.pAudioStream->time_base, dRTMPOutputContext.pAudioStream->time_base);

                        res = av_interleaved_write_frame(pOutputFormatContext, pOutputPacket);
                    }
                    av_packet_unref(pOutputPacket);
                }
                av_frame_unref(pFrame);
            }
        }
        else {
            logging("Other InputPacket->pts %" PRId64, pInputPacket->pts);
        }
        av_packet_unref(pInputPacket);
        // https://ffmpeg.org/doxygen/trunk/group__lavc__packet.html#ga63d5a489b419bd5d45cfd09091cbcbc2
    }

    av_write_trailer(pOutputFormatContext);

    logging("releasing all the resources");

    if (pOutputFormatContext && !(pOutputFormatContext->oformat->flags & AVFMT_NOFILE))
        avio_closep(&pOutputFormatContext->pb);
    avformat_free_context(pOutputFormatContext);

    avformat_close_input(&pInputFormatContext);
    av_packet_free(&pInputPacket);
    avcodec_free_context(&dRTMPInputContext.pVideoContext);
    avcodec_free_context(&dRTMPInputContext.pAudioContext);



    return 0;
}

static void logging(const char* fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
