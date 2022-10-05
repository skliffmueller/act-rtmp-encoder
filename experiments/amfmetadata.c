#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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



static int create_amf_metadata_buffer(uint8_t* buffer, const char* text)
{
    int i = 0;
    size_t len = strlen(text);

    buffer[i++] = AMF_DATA_TYPE_STRING;
    amf_write_str(buffer + i, "onTextData");
    i += 10;
    buffer[i++] = AMF_DATA_TYPE_MIXEDARRAY;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x02;
    amf_write_str(buffer + i, "type");
    i += 4;
    buffer[i++] = AMF_DATA_TYPE_STRING;
    amf_write_str(buffer + i, "Metadata");
    i += 8;
    amf_write_str(buffer + i, "text");
    i += 4;
    buffer[i++] = AMF_DATA_TYPE_STRING;
    amf_write_str(buffer + i, text);
    i += len;
    buffer[i++] = 0x00;
    buffer[i++] = AMF_END_OF_OBJECT;

    return i;
}

static void amf_write_str(uint8_t* buffer, const char* str)
{
    int i = 0;
    size_t len = strlen(str);
    
    buffer[i++] = (len >> 8) & 0xff;
    buffer[i++] = len & 0xff;
    memcpy(buffer + i, str, len);
}
