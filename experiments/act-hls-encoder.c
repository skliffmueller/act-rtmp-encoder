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

#define AMF_END_OF_OBJECT         0x09


typedef enum {
    AMF_DATA_TYPE_NUMBER = 0x00,
    AMF_DATA_TYPE_BOOL = 0x01,
    AMF_DATA_TYPE_STRING = 0x02,
    AMF_DATA_TYPE_OBJECT = 0x03,
    AMF_DATA_TYPE_NULL = 0x05,
    AMF_DATA_TYPE_UNDEFINED = 0x06,
    AMF_DATA_TYPE_REFERENCE = 0x07,
    AMF_DATA_TYPE_MIXEDARRAY = 0x08,
    AMF_DATA_TYPE_OBJECT_END = 0x09,
    AMF_DATA_TYPE_ARRAY = 0x0a,
    AMF_DATA_TYPE_DATE = 0x0b,
    AMF_DATA_TYPE_LONG_STRING = 0x0c,
    AMF_DATA_TYPE_UNSUPPORTED = 0x0d,
} AMFDataType;
enum FlvTagType {
    FLV_TAG_TYPE_AUDIO = 0x08,
    FLV_TAG_TYPE_VIDEO = 0x09,
    FLV_TAG_TYPE_META = 0x12,
};

typedef struct ID3v2Header  {
    unsigned long fileIdentifier;
    unsigned short versionCode;
    unsigned char flags;
    unsigned long size;
} ID3v2Header;

typedef struct ID3v2Frame {
    unsigned long frameId;
    unsigned long size;
    unsigned short flags;
    char* ownerIdentifier;
    char* data;
} ID3v2Frame;


typedef struct RTMPInputContext {
    AVFormatContext* pFormatContext;


    AVCodecParameters* pVideoCodecParameters;
    AVCodec* pVideoCodec;
    AVCodecContext* pVideoCodecContext;
    AVStream* pVideoStream;
    int iVideoStreamIndex;

    AVCodecParameters* pAudioCodecParameters;
    AVCodec* pAudioCodec;
    AVCodecContext* pAudioCodecContext;
    AVStream* pAudioStream;
    int iAudioStreamIndex;
} RTMPInputContext;

typedef struct RTMPOutputContext {
    AVFormatContext* pFormatContext;

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
} RTMPOutputContext;

typedef struct KeyLoggerContext {
    uint8_t hasChanged;
    int printLength;
    BYTE prevKeys[256];
    BYTE nextKeys[256];
    BYTE whiteListBuffer[256];
} KeyLoggerContext;



typedef struct DataPacketBuffer {
    AVPacket* buffer[256];
    size_t size;
} DataPacketBuffer;

 // print out the steps and errors
static void logging(const char* fmt, ...);

static int setupOutputStreams(AVFormatContext* pFormatContext, RTMPOutputContext* pOutputContext, RTMPInputContext* pInputContext, const char* url);

static int setupInputFormatContext(AVFormatContext* pFormatContext);

static int setupInputStreams(AVFormatContext* pFormatContext, RTMPInputContext* dInputContext);

//hls_init_time = 2
//hls_time = 2
//hls_list_size = 5
//hls_segment_options = <mpegts options>
//hls_start_number_source = epoch_us
//hls_segment_filename = segment_ % Y % m % d % H % M % S_ % %04d_ % %08s_ % %013t.ts
//hls_segment_type = mpegts
//hls_flags = second_level_segment_duration
//strftime = 1


const char* KeyWhiteList[] = {
    VK_ESCAPE,
    VK_F1,
    VK_F2,
    VK_F3,
    VK_F4,
    VK_F5,
    VK_F6,
    VK_F7,
    VK_F8,
    VK_F9,
    VK_F10,
    VK_F11,
    VK_F12,
    VK_OEM_3,
    0x30,
    0x31,
    0x32,
    0x33,
    0x34,
    0x35,
    0x36,
    0x37,
    0x38,
    0x39,

    0x41,
    0x42,
    0x43,
    0x44,
    0x45,
    0x46,
    0x47,
    0x48,
    0x49,
    0x4A,
    0x4B,
    0x4C,
    0x4D,
    0x4E,
    0x4F,
    0x50,
    0x51,
    0x52,
    0x53,
    0x54,
    0x55,
    0x56,
    0x57,
    0x58,
    0x59,
    0x5A,

    VK_OEM_MINUS,
    VK_OEM_PLUS,
    VK_BACK,
    VK_TAB,
    VK_OEM_4,
    VK_OEM_6,
    VK_OEM_5,
    VK_CAPITAL,

    VK_OEM_1,
    VK_RETURN,
    VK_LSHIFT,

    VK_OEM_COMMA,
    VK_OEM_PERIOD,
    VK_OEM_2,
    VK_RSHIFT,

    VK_LCONTROL,
    VK_LWIN,
    VK_LMENU,
    VK_SPACE,
    VK_RMENU,
    VK_RWIN,
    VK_APPS,
    VK_RCONTROL,
    0x00,
};

int setupOutputStreams(AVFormatContext* pFormatContext, RTMPOutputContext* pOutputContext, RTMPInputContext* pInputContext, const char* url)
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
    pVideoCodec = avcodec_find_encoder((*pInputContext).pVideoCodecContext->codec_id);
    pVideoContext = avcodec_alloc_context3(pVideoCodec);
    pVideoContext->bit_rate = 2400000;
    pVideoContext->width = (*pInputContext).pVideoCodecContext->width;
    pVideoContext->height = (*pInputContext).pVideoCodecContext->height;
    pVideoContext->time_base = (*pInputContext).pVideoStream->time_base;
    pVideoContext->framerate = (*pInputContext).pVideoStream->r_frame_rate;
    pVideoContext->sample_aspect_ratio = (*pInputContext).pVideoCodecContext->sample_aspect_ratio;
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
    pAudioCodec = avcodec_find_encoder((*pInputContext).pAudioCodecContext->codec_id);
    pAudioContext = avcodec_alloc_context3(pAudioCodec);

    pAudioContext->sample_rate = (*pInputContext).pAudioCodecContext->sample_rate;
    pAudioContext->channel_layout = (*pInputContext).pAudioCodecContext->channel_layout;
    pAudioContext->channels = av_get_channel_layout_nb_channels(pAudioContext->channel_layout);
    pAudioContext->sample_fmt = pAudioCodec->sample_fmts[0];
    pAudioContext->time_base = (AVRational){ 1, (*pInputContext).pAudioCodecContext->sample_rate };

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

    pDataCodecParameters->codec_id = AV_CODEC_ID_TIMED_ID3;
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

    //hls_init_time = 2
//hls_time = 2
//hls_list_size = 5
//hls_segment_options = <mpegts options> pes_payload_size
//hls_start_number_source = epoch_us
//hls_segment_filename = segment_ % Y % m % d % H % M % S_ % %04d_ % %08s_ % %013t.ts
//hls_segment_type = mpegts
//hls_flags = second_level_segment_duration
//strftime = 1
    //hls_playlist_type = event
    const char* segmentName = "segment_%Y%m%d%H%M%S_%%04d.ts";
    size_t segmentSize = strlen(segmentName);

    int pathIndex = 0;
    int lastMatchIndex = 0;
    while (url[pathIndex]) {
        if (url[pathIndex] == 0x2f) {
            lastMatchIndex = pathIndex;
        }
        pathIndex++;
    }

    size_t segmentPathSize = segmentSize + lastMatchIndex;
    if (lastMatchIndex > 0) {
        segmentPathSize++;
    }
    char* segmentPath = av_mallocz(segmentPathSize + 1);

    if (lastMatchIndex > 0) {
        lastMatchIndex++;
        memcpy(segmentPath, url, lastMatchIndex);
        
    }
    memcpy(segmentPath + lastMatchIndex, segmentName, segmentSize);

    AVDictionary* pOptions = NULL;

    av_dict_set(&pOptions, "hls_init_time", "5", 0);
    av_dict_set(&pOptions, "hls_time", "5", 0);
    av_dict_set(&pOptions, "hls_list_size", "0", 0);
    av_dict_set(&pOptions, "hls_start_number_source", "epoch_us", 0);
    av_dict_set(&pOptions, "hls_segment_type", "mpegts", 0);
    av_dict_set(&pOptions, "hls_segment_options", "pes_payload_size=32", 0);
    av_dict_set(&pOptions, "hls_segment_filename", segmentPath, 0);
    av_dict_set(&pOptions, "hls_flags", "append_list+second_level_segment_index+split_by_time", 0);
    av_dict_set(&pOptions, "strftime", "1", 0);
    av_dict_set(&pOptions, "hls_playlist_type", "event", 0);
    av_dict_set(&pOptions, "hls_allow_cache", "0", 0);
    // av_dict_set(&pOptions, "hls_delete_threshold", "100", 0);

    av_dict_set(&pOptions, "segment_format", "mpegts", 0);
    av_dict_set(&pOptions, "segment_list_type", "m3u8", 0);
    av_dict_set(&pOptions, "segment_list", url, 0);
    av_dict_set_int(&pOptions, "segment_list_size", 0, 0);
    av_dict_set(&pOptions, "segment_time_delta", "1.0", 0);
    av_dict_set(&pOptions, "segment_time", "5", 0);
    av_dict_set_int(&pOptions, "reference_stream", (*pOutputContext).iVideoStreamIndex, 0);
    av_dict_set(&pOptions, "segment_list_flags", "cache+live", 0);


    if (avformat_write_header(pFormatContext, &pOptions) < 0) {
        logging("Error occurred when writing header to hls Format Context.\n");
        return -1;
    }

    return 0;
}

int setupInputFormatContext(AVFormatContext* pFormatContext) {
    AVDictionary* pOptions = NULL;


    avformat_network_init();

    av_dict_set(&pOptions, "protocol_whitelist", "tcp,rtmp,rtmps,tls", 0);
    av_dict_set(&pOptions, "listen", "1", 0);

    if (avformat_open_input(&pFormatContext, "rtmp://127.0.0.1:1934/stream", NULL, &pOptions) != 0) {
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

int setupInputStreams(AVFormatContext* pFormatContext, RTMPInputContext* dInputContext) {
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
                (*dInputContext).pVideoCodecContext = pInputCodecContext;
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
                (*dInputContext).pAudioCodecContext = pInputCodecContext;
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

    av_dump_format(pFormatContext, 0, "rtmp://127.0.0.1:1934/stream", 0);

    return 0;
}

void* keyLoggerThread(void** vargp)
{
    AVPacket* pPacket = (AVPacket*)vargp[0];
    int* pVideoIndex = (int*)vargp[1];
    AVRational* srcTimebase = (AVRational*)vargp[2];
    AVRational* dstTimebase = (AVRational*)vargp[3];
    AVFormatContext* pOutputFormatContext = (AVFormatContext*)vargp[4];

    // {"module":"keyboard","version":"0.0.1","action":"change","props":%}
    const char* jsonFrame = "{\"module\":\"test\",\"version\":\"0.0.1\",\"action\":\"change\",\"pts\":%d,\"props\":%s}";
    size_t jsonFrameSize = 75;
    size_t jsonPtsIndex = strlen(jsonFrame);
    size_t jsonPropsIndex = strlen(jsonFrame + jsonPtsIndex + 1);
    KeyLoggerContext dKeyloggerContext;
    int64_t lastTime = 0;
    int64_t i = 0;
    int64_t lastDts = 0;
    int64_t lastPts = 0;
    int dataStreamIndex = -1;
    uint8_t hasChanged;
    int printLength;
    BYTE prevKeys[256];
    BYTE nextKeys[256];
    BYTE whiteListBuffer[256];


    hasChanged = 0;
    memset(nextKeys, 0, 256);
    memset(prevKeys, 0, 256);
    memset(whiteListBuffer, 0, 256);

    i = 0;
    while (KeyWhiteList[i]) {
        uint8_t bufferIndex = KeyWhiteList[i];
        whiteListBuffer[bufferIndex] = 1;
        i++;
    }
    i = 0;
    
    for (int s = 0; s < pOutputFormatContext->nb_streams; s++) {
        if (pOutputFormatContext->streams[s]->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
            dataStreamIndex = s;
            break;
        }
    }


    while (pPacket) {
        if (pPacket->stream_index == pVideoIndex) {
            if (pPacket->dts > 0 && pPacket->dts > i) {
                i = pPacket->dts;
                lastPts = pPacket->pts;
                int64_t newTime = av_gettime_relative();
                //int64_t dts = av_rescale_q_rnd(pPacket->dts, (*srcTimebase), (*dstTimebase), AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

                //logging("Other InputPacket->dts %d OutputPacket->dts %d %d", pPacket->dts, dts, newTime - lastTime);
                lastTime = newTime;
            }
        }
        if (i > 0 && i - lastDts > 10000) {
            const char* arrayBuffer = "{\"message\":\"Hello World\"}";
            size_t arrayBufferSize = strlen(arrayBuffer);

            int64_t newTime = av_gettime_relative();
            int64_t pts = lastPts;
            int64_t dts = i - 1;
            // int64_t delaySince = av_gettime_relative() - lastTime;

            // pts += delaySince;
            pts = av_rescale_q_rnd(pts, (*srcTimebase), (*dstTimebase), AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            dts = av_rescale_q_rnd(dts, (*srcTimebase), (*dstTimebase), AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            // delaySince = av_rescale_q_rnd((lastPts * 1000) + delaySince, (AVRational) { (*srcTimebase).num, ((*srcTimebase).den * 1000) }, (AVRational) { 1, 240 }, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            logging("Other OutputPacket->pts %d OutputPacket->dts %d", pts, dts);

            // {"module":"keyboard","version":"0.0.1","action":"change","props":%}
            size_t jsonDataSize = jsonFrameSize + arrayBufferSize - 1;
            uint8_t* jsonData = av_mallocz(jsonDataSize + 1);
            
            // "{"module":"keyboard","version":"0.0.1","action":"change","pts":%d,"props":%s}"
            sprintf(jsonData, jsonFrame, pts, arrayBuffer);
        
            AVPacket* pDataPacket = av_packet_alloc();
            uint8_t* buffer = av_mallocz(512);
            pDataPacket->size = priv_frame_write_data(buffer, jsonData, "metadata.live-video.net");
            pDataPacket->data = buffer;
        
            pDataPacket->pts = pts;
            pDataPacket->dts = dts;
            pDataPacket->duration = 0;
            pDataPacket->pos = -1;
            pDataPacket->stream_index = dataStreamIndex;
            av_interleaved_write_frame(pOutputFormatContext, pDataPacket);
            av_packet_unref(pDataPacket);
            av_packet_free(&pDataPacket);
            // pDataPacketBuffer->buffer[pDataPacketBuffer->size++] = pDataPacket;
            lastDts = i;
        }

        //if (i > 0) {
        //    uint8_t* arrayBuffer = av_mallocz(128);
        //    size_t arrayBufferSize = 0;
        //    hasChanged = 0;

        //    GetKeyState(0);

        //    if (GetKeyboardState(nextKeys))
        //    {
        //        arrayBuffer[arrayBufferSize++] = 0x5b; // "["
        //        for (uint8_t i = 0; i < 256; i++)
        //        {
        //            if (whiteListBuffer[i] != 1) {
        //                if (i == 255) {
        //                    break;
        //                }
        //                continue;
        //            }
        //            if (!hasChanged && (nextKeys[i] & 0b10000000) != (prevKeys[i] & 0b10000000)) {
        //                hasChanged = 1;
        //            }
        //            if (nextKeys[i] & 0b10000000) {
        //                arrayBufferSize += sprintf(arrayBuffer + arrayBufferSize, "%d,", i);
        //            }
        //            if (i == 255) {
        //                break;
        //            }
        //        }
        //        if (arrayBufferSize > 1) {
        //            arrayBuffer[arrayBufferSize - 1] = 0x5d; // "]"
        //        }
        //        else {
        //            arrayBuffer[arrayBufferSize++] = 0x5d; // "]"
        //        }

        //        memset(prevKeys, 0, 256);
        //        memcpy(prevKeys, nextKeys, 256);
        //        memset(nextKeys, 0, 256);
        //    }
        //    if (hasChanged == 1) {
        //        int64_t pts = i * 1000;
        //        int64_t dts = i * 1000;
        //        int64_t delaySince = av_gettime_relative() - lastTime;

        //        pts += delaySince;
        //        pts = av_rescale_q_rnd(pts, (AVRational) { (*srcTimebase).num, ((*srcTimebase).den * 1000) }, (AVRational) { 1, 240 }, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        //        dts = av_rescale_q_rnd(dts, (AVRational) { (*srcTimebase).num, ((*srcTimebase).den * 1000) }, (AVRational) { 1, 240 }, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        //        delaySince = av_rescale_q_rnd((lastPts * 1000) + delaySince, (AVRational) { (*srcTimebase).num, ((*srcTimebase).den * 1000) }, (AVRational) { 1, 240 }, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
        //        // logging("Other OutputPacket->pts %d ", pts);

        //        // {"module":"keyboard","version":"0.0.1","action":"change","props":%}
        //        size_t jsonDataSize = jsonFrameSize + arrayBufferSize - 1;
        //        uint8_t* jsonData = av_mallocz(jsonDataSize + 1);
        //        
        //        // "{"module":"keyboard","version":"0.0.1","action":"change","pts":%d,"props":%s}"
        //        sprintf(jsonData, jsonFrame, delaySince, arrayBuffer);
        //    
        //        AVPacket* pDataPacket = av_packet_alloc();
        //        uint8_t* buffer = av_mallocz(512);
        //        pDataPacket->size = priv_frame_write_data(buffer, jsonData, "metadata.live-video.net");
        //        pDataPacket->data = buffer;
        //    
        //        pDataPacket->pts = pts;
        //        pDataPacket->dts = dts;
        //        pDataPacket->duration = 0;
        //        pDataPacket->stream_index = dataStreamIndex;
        //        av_interleaved_write_frame(pOutputFormatContext, pDataPacket);
        //        av_packet_unref(pDataPacket);
        //        av_packet_free(&pDataPacket);
        //        // pDataPacketBuffer->buffer[pDataPacketBuffer->size++] = pDataPacket;
        //    }
        //    av_usleep(10000);
        //}
    }
}

int key_logger(KeyLoggerContext* pContext, char* buffer)
{
    int bufferSize = 0;
    (*pContext).hasChanged = 0;

    GetKeyState(0);

    if (GetKeyboardState((*pContext).nextKeys))
    {
        buffer[bufferSize++] = 0x5b; // "["
        for (uint8_t i = 0; i < 256; i++)
        {
            if ((*pContext).whiteListBuffer[i] != 1) {
                if (i == 255) {
                    break;
                }
                continue;
            }
            if (!(*pContext).hasChanged && ((*pContext).nextKeys[i] & 0b10000000) != ((*pContext).prevKeys[i] & 0b10000000)) {
                (*pContext).hasChanged = 1;
            }
            if ((*pContext).nextKeys[i] & 0b10000000) {
                uint8_t num = i;
                bufferSize += number_to_ascii(buffer + bufferSize, num);
                buffer[bufferSize++] = 0x2c; // ","
            }
            if (i == 255) {
                break;
            }
        }
        if (bufferSize > 1) {
            buffer[bufferSize - 1] = 0x5d; // "]"
        }
        else {
            buffer[bufferSize++] = 0x5d; // "]"
        }

        memset((*pContext).prevKeys, 0, 256);
        memcpy((*pContext).prevKeys, (*pContext).nextKeys, 256);
        memset((*pContext).nextKeys, 0, 256);
    }
    return bufferSize;
}

//void runPacketStuff(void) {
//    uint8_t* arrayBuffer = av_mallocz(128);
//    size_t arrayBufferSize = key_logger(&dKeyloggerContext, arrayBuffer);
//    if (dKeyloggerContext.hasChanged == 1) {
//        // {"module":"keyboard","version":"0.0.1","action":"change","props":%}
//        size_t jsonDataSize = jsonFrameSize + arrayBufferSize - 1;
//        uint8_t* jsonData = av_mallocz(jsonDataSize + 1);
//
//        memcpy(jsonData, jsonFrame, jsonFrameLen);
//        memcpy(jsonData + jsonFrameLen, arrayBuffer, arrayBufferSize);
//        memcpy(jsonData + jsonFrameLen + arrayBufferSize, jsonFrame + jsonFrameLen + 1, jsonFrameSize - jsonFrameLen - 1);
//
//        AVPacket* pDataPacket = av_packet_alloc();
//        uint8_t* buffer = av_mallocz(512);
//        pDataPacket->size = priv_frame_write_data(buffer, jsonData, "metadata.live-video.net");
//        pDataPacket->data = buffer;
//
//        pDataPacket->stream_index = dRTMPOutputContext.iDataStreamIndex;
//        pDataPacket->pts = av_rescale_q_rnd(pInputPacket->dts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pDataStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
//        pDataPacket->dts = av_rescale_q_rnd(pInputPacket->dts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pDataStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
//        pDataPacket->duration = av_rescale_q(pInputPacket->duration, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pDataStream->time_base);
//
//        av_interleaved_write_frame(pOutputFormatContext, pDataPacket);
//        av_packet_unref(pDataPacket);
//        av_packet_free(&pDataPacket);
//    }
//}



int main(int argc, const char* argv[])
{
    const char* output_filename;
    AVDictionary *pOptions = NULL;
    AVFrame* pInputFrame;
    AVFrame* pOutputFrame;
    AVPacket* pInputPacket;
    AVFormatContext* pOutputFormatContext;
    AVFormatContext* pRTMPFormatContext;
    RTMPInputContext dRTMPInputContext;
    RTMPOutputContext dRTMPOutputContext;
    KeyLoggerContext dKeyloggerContext;
    DataPacketBuffer dDataPacketBuffer;
    DataPacketBuffer dReceivingBuffer;

    int ret, i;
    int stream_index = 0;
    int* streams_list = NULL;
    int number_of_streams = 0;
    pthread_t tid;

    struct SwrContext* pSwrContext;
    uint8_t** src_data = NULL, ** dst_data = NULL;
    int src_nb_channels = 0, dst_nb_channels = 0;
    int src_linesize, dst_linesize;
    int src_nb_samples = 1024, dst_nb_samples, max_dst_nb_samples;



    dDataPacketBuffer.size = 0;
    memset(dDataPacketBuffer.buffer, 0, sizeof(AVPacket*) * 256);

    dReceivingBuffer.size = 0;
    memset(dReceivingBuffer.buffer, 0, sizeof(AVPacket*) * 256);

    if (argc <= 1) {
        fprintf(stderr, "Usage: %s <output file> <codec name>\n", argv[0]);
        exit(0);
    }
    output_filename = argv[1];
    pRTMPFormatContext = avformat_alloc_context();

    setupInputFormatContext(pRTMPFormatContext);

    if (setupInputStreams(pRTMPFormatContext, &dRTMPInputContext) < 0) {
        logging("ERROR could not setup rtmp streams.\n");
        return -1;
    }

    pOutputFormatContext = avformat_alloc_context();

    //if (avformat_alloc_output_context2(&pOutputFormatContext, NULL, "flv", "") < 0) {
    //    logging("ERROR could not open rtmp output stream.\n");
    //    return -1;
    //}

    //if (setupOutputStreams(pOutputFormatContext, &dRTMPOutputContext, &dRTMPInputContext, "") < 0) {
    //    logging("ERROR could not setup hls streams.\n");
    //    return -1;
    //}


    if (avformat_alloc_output_context2(&pOutputFormatContext, NULL, "hls", output_filename) < 0) {
        logging("ERROR could not open rtmp output stream.\n");
        return -1;
    }

    if (setupOutputStreams(pOutputFormatContext, &dRTMPOutputContext, &dRTMPInputContext, output_filename) < 0) {
        logging("ERROR could not setup hls streams.\n");
        return -1;
    }

    pInputPacket = av_packet_alloc();
    if (!pInputPacket)
    {
        logging("failed to allocate memory for AVPacket");
        return -1;
    }
    // void* threadArgs[5] = { pInputPacket, dRTMPInputContext.iVideoStreamIndex, &dRTMPInputContext.pVideoStream->time_base, &dRTMPOutputContext.pDataStream->time_base, pOutputFormatContext };
    // pthread_create(&tid, NULL, keyLoggerThread, threadArgs);

    int counter = 0;
    // fill the Packet with data from the Stream
    // https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61
    int lastDts = 0;
    const char* jsonFrame = "{\"module\":\"test\",\"version\":\"0.0.1\",\"action\":\"change\",\"pts\":%d,\"props\":%s}";
    size_t jsonFrameSize = 75;
    while (1)
    {

        if (av_read_frame(pRTMPFormatContext, pInputPacket) < 0) {
            break;
        }
        if (pInputPacket->stream_index == dRTMPInputContext.iVideoStreamIndex && pInputPacket->dts - lastDts > 5000)
        {
            const char* arrayBuffer = "{\"message\":\"Hello World\"}";
            size_t arrayBufferSize = strlen(arrayBuffer);

            int64_t newTime = av_gettime_relative();
            int64_t pts = pInputPacket->pts - 1;
            int64_t dts = pts;
            // int64_t delaySince = av_gettime_relative() - lastTime;

            // pts += delaySince;
            pts = av_rescale_q_rnd(pts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pDataStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            dts = av_rescale_q_rnd(dts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pDataStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            // delaySince = av_rescale_q_rnd((lastPts * 1000) + delaySince, (AVRational) { (*srcTimebase).num, ((*srcTimebase).den * 1000) }, (AVRational) { 1, 240 }, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            logging("Other OutputPacket->pts %d OutputPacket->dts %d", pts, dts);

            // {"module":"keyboard","version":"0.0.1","action":"change","props":%}
            size_t jsonDataSize = jsonFrameSize + arrayBufferSize - 1;
            uint8_t* jsonData = av_mallocz(jsonDataSize + 1);

            // "{"module":"keyboard","version":"0.0.1","action":"change","pts":%d,"props":%s}"
            sprintf(jsonData, jsonFrame, pts, arrayBuffer);

            AVPacket* pDataPacket = av_packet_alloc();
            uint8_t* buffer = av_mallocz(512);
            pDataPacket->size = priv_frame_write_data(buffer, jsonData, "metadata.live-video.net");
            pDataPacket->data = buffer;

            pDataPacket->pts = pts;
            pDataPacket->dts = dts;
            pDataPacket->duration = 0;
            pDataPacket->pos = -1;
            pDataPacket->stream_index = dRTMPOutputContext.iDataStreamIndex;
            av_interleaved_write_frame(pOutputFormatContext, pDataPacket);
            av_packet_unref(pDataPacket);
            av_packet_free(&pDataPacket);
            lastDts = pInputPacket->dts;
        }



        AVPacket* pOutputPacket = av_packet_alloc();

        // if it's the video stream
        if (pInputPacket->stream_index == dRTMPInputContext.iVideoStreamIndex) {
            int response = avcodec_send_packet(dRTMPInputContext.pVideoCodecContext, pInputPacket);
            while (response >= 0) {

                AVFrame* pFrame = av_frame_alloc();



                response = avcodec_receive_frame(dRTMPInputContext.pVideoCodecContext, pFrame);
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                }
                else if (response < 0) {
                    return response;
                }
                if (response >= 0) {
                    AVPacket* pOutputPacket = av_packet_alloc();
                    int res = avcodec_send_frame(dRTMPOutputContext.pVideoContext, pFrame);

                    while (res >= 0) {
                        res = avcodec_receive_packet(dRTMPOutputContext.pVideoContext, pOutputPacket);
                        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {



                            break;
                        }
                        else if (res < 0) {
                            return -1;
                        }

                        //pOutputPacket->stream_index = dRTMPOutputContext.iVideoStreamIndex;
                        //pOutputPacket->pts = av_rescale_q_rnd(pInputPacket->pts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pVideoContext->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        //pOutputPacket->dts = av_rescale_q_rnd(pInputPacket->dts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pVideoContext->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        //pOutputPacket->duration = av_rescale_q(pInputPacket->duration, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pVideoContext->time_base);
                        // pOutputPacket->duration = dRTMPOutputContext.pVideoStream->time_base.den / dRTMPOutputContext.pVideoStream->time_base.num / dRTMPInputContext.pVideoStream->avg_frame_rate.num * dRTMPInputContext.pVideoStream->avg_frame_rate.den;

                        pOutputPacket->stream_index = dRTMPOutputContext.iVideoStreamIndex;
                        pOutputPacket->pts = av_rescale_q_rnd(pInputPacket->pts, dRTMPOutputContext.pVideoContext->time_base, dRTMPOutputContext.pVideoStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        pOutputPacket->dts = av_rescale_q_rnd(pInputPacket->dts, dRTMPOutputContext.pVideoContext->time_base, dRTMPOutputContext.pVideoStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        pOutputPacket->duration = av_rescale_q(pInputPacket->duration, dRTMPOutputContext.pVideoContext->time_base, dRTMPOutputContext.pVideoStream->time_base);


                        // av_packet_rescale_ts(pOutputPacket, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pVideoStream->time_base);
                        // logging("Video OutputPacket->pts %d OutputPacket->dts %d", pOutputPacket->pts, pOutputPacket->dts);
                        res = av_interleaved_write_frame(pOutputFormatContext, pOutputPacket);
                    }
                    av_packet_unref(pOutputPacket);
                    av_packet_free(&pOutputPacket);

                }
                av_frame_unref(pFrame);
            }

            //dReceivingBuffer.size = 0;
            //memset(dReceivingBuffer.buffer, 0, sizeof(AVPacket*) * 256);
            //if (dDataPacketBuffer.size > 0) {
            //    dReceivingBuffer.size = dDataPacketBuffer.size;
            //    memcpy(dReceivingBuffer.buffer, dDataPacketBuffer.buffer, sizeof(AVPacket*) * dDataPacketBuffer.size);
            //    dDataPacketBuffer.size = 0;
            //    memset(dDataPacketBuffer.buffer, 0, sizeof(AVPacket*) * 256);
            //}

            //if (dReceivingBuffer.size > 0) {
            //    uint64_t dataIndex = 0;
            //    uint64_t dataDts = av_rescale_q_rnd(pInputPacket->dts, dRTMPInputContext.pVideoStream->time_base, dRTMPOutputContext.pVideoStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);

            //    while (dataIndex < dReceivingBuffer.size) {
            //        dReceivingBuffer.buffer[dataIndex]->stream_index = dRTMPOutputContext.iDataStreamIndex;

            //        av_interleaved_write_frame(pOutputFormatContext, dReceivingBuffer.buffer[dataIndex]);
            //        av_packet_unref(dReceivingBuffer.buffer[dataIndex]);
            //        av_packet_free(&dReceivingBuffer.buffer[dataIndex]);
            //        dataIndex++;
            //    }
            //}

        } else if (pInputPacket->stream_index == dRTMPInputContext.iAudioStreamIndex) {
            int response = avcodec_send_packet(dRTMPInputContext.pAudioCodecContext, pInputPacket);
            while (response >= 0) {

                AVFrame* pFrame = av_frame_alloc();


                response = avcodec_receive_frame(dRTMPInputContext.pAudioCodecContext, pFrame);
                if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
                    break;
                }
                else if (response < 0) {
                    return response;
                }
                if (response >= 0) {

                    AVPacket* pOutputPacket = av_packet_alloc();
                    int res = avcodec_send_frame(dRTMPOutputContext.pAudioContext, pFrame);

                    while (res >= 0) {
                        res = avcodec_receive_packet(dRTMPOutputContext.pAudioContext, pOutputPacket);
                        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
                            break;
                        }
                        else if (res < 0) {
                            return -1;
                        }

                        pOutputPacket->stream_index = dRTMPOutputContext.iAudioStreamIndex;
                        //pOutputPacket->pts = av_rescale_q_rnd(pInputPacket->pts, dRTMPInputContext.pAudioStream->time_base, dRTMPOutputContext.pAudioStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        //pOutputPacket->dts = av_rescale_q_rnd(pInputPacket->dts, dRTMPInputContext.pAudioStream->time_base, dRTMPOutputContext.pAudioStream->time_base, AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
                        //pOutputPacket->duration = av_rescale_q(pInputPacket->duration, dRTMPInputContext.pAudioStream->time_base, dRTMPOutputContext.pAudioStream->time_base);
                        av_packet_rescale_ts(pOutputPacket, dRTMPInputContext.pAudioStream->time_base, dRTMPOutputContext.pAudioStream->time_base);

                        res = av_interleaved_write_frame(pOutputFormatContext, pOutputPacket);
                    }
                    av_packet_unref(pOutputPacket);
                    av_packet_free(&pOutputPacket);

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

    avformat_close_input(&pRTMPFormatContext);
    av_packet_free(&pInputPacket);
    avcodec_free_context(&dRTMPInputContext.pVideoCodecContext);
    avcodec_free_context(&dRTMPInputContext.pAudioCodecContext);



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


int priv_frame_write_data(uint8_t* buffer, uint8_t* data, const char* frameOwner)
{
    int i = 0;
    size_t size = strlen(data);
    size_t frameOwnerSize = strlen(frameOwner);

    size_t privFrameSize = 11 + size + frameOwnerSize;
    size_t privDataSize = privFrameSize - 10;
    size_t id3HeaderSize = 10;
    size_t bufferSize = privFrameSize + id3HeaderSize;

    uint8_t* newBuffer = av_mallocz(bufferSize + 1);

    memcpy(newBuffer, "ID3\x04\x00\x00", 6); // ID3 header // Version Code // Experimental Flag
    i += 6;
    // ID3 Packet Size
    for (int j = 3; j >= 0; j--)
    {
        // memcpy(newBuffer + i + j, &(char){ privFrameSize & 0b01111111 }, 1);
        newBuffer[i + j] = privFrameSize & 0b01111111;
        privFrameSize = privFrameSize >> 7;
    }
    i += 4;

    memcpy(newBuffer + i, "PRIV", 4); // PRIV Frame header
    i += 4;

    // PRIV frame data size
    int dataSize = size;
    for (int j = 3; j >= 0; j--)
    {
        newBuffer[i + j] = privDataSize & 0b11111111;
        privDataSize = privDataSize >> 8;
    }
    i += 4;

    // PRIV Read Only Flag
    newBuffer[i++] = 0b00000000;
    newBuffer[i++] = 0b00000000;

    memcpy(newBuffer + i, frameOwner, frameOwnerSize);
    i += frameOwnerSize;

    newBuffer[i++] = 0x00;

    memcpy(newBuffer + i, data, size);
    i += size;

    av_realloc(buffer, bufferSize);

    memcpy(buffer, newBuffer, bufferSize);

    return bufferSize;
}

// KeyLog.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

// 0x30 - 0 ascii
// x % 10 = ones
// x -= ones
// x % 100 = tens;
// x -= tens;
// x / 100 = hundreds;

static int insert_at() {

}

static int number_to_ascii(char* buffer, uint8_t num)
{
    uint8_t ones, tens, hund;

    ones = num % 10;
    num -= ones;
    
    if (!num) {
        buffer[0] = (ones + 0x30);
        return 1;
    }
    tens = num % 100;
    num -= tens;
    tens = tens / 10;
    
    if (!num) {
        buffer[0] = (tens + 0x30);
        buffer[1] = (ones + 0x30);
        return 2;
    }
    hund = num / 100;
    buffer[0] = (hund + 0x30);
    buffer[1] = (tens + 0x30);
    buffer[2] = (ones + 0x30);
    return 3;
}




