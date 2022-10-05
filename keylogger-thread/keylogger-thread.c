#include "keylogger-thread.h"

#include <windows.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>



const char KeyWatchList[] = {
    VK_ESCAPE, VK_F1, VK_F2, VK_F3,
    VK_F4, VK_F5, VK_F6, VK_F7,
    VK_F8, VK_F9, VK_F10, VK_F11,
    VK_F12, VK_OEM_3,

    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
    0x38,0x39,

    0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,
    0x51,0x52,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5A,

    VK_OEM_MINUS,VK_OEM_PLUS,VK_BACK,VK_TAB,
    VK_OEM_4,VK_OEM_6,VK_OEM_5,VK_CAPITAL,

    VK_OEM_1,VK_RETURN,VK_LSHIFT,

    VK_OEM_COMMA,VK_OEM_PERIOD,VK_OEM_2,VK_RSHIFT,

    VK_LCONTROL,VK_LWIN,VK_LMENU,VK_SPACE,
    VK_RMENU,VK_RWIN,VK_APPS,VK_RCONTROL,
    0x00,
};


static void logging(const char* fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void* keyLoggerThread(void** vargp)
{
    KeyLoggerContext* pContext = (KeyLoggerContext*)vargp[0];
    AVRational* timebase = pContext->ptsTimebase;

    char* awsArnChannel = (char*)vargp[1];

    // {"module":"keyboard","version":"0.0.1","action":"change","props":%}
    const char* jsonObjFrame = "{\"m\":\"keyboard\",\"a\":\"change\",\"t\":%d,\"d\":%d,\"p\":%s}\0";
    const int64_t oneSecond = timebase->den;
    const int64_t jsonObjFrameSize = strlen(jsonObjFrame) - 4;
    int64_t lastDts = 0;
    int64_t lastPtsEvent = 0;
    BYTE prevKeys[256];
    BYTE nextKeys[256];
    uint8_t jsonBuffer[1024];
    int64_t jsonBufferSize = 0;
    char jsonKeyArrayBuffer[26];
    char jsonObjBuffer[84];// (((6-keys * 3dec) + 2, + 2[] + (jsonFrameSize - 4) + 20int)

    memset(nextKeys, 0, 256);
    memset(prevKeys, 0, 256);
    memset(jsonBuffer, 0, 1024);
    memset(jsonObjBuffer, 0, 84);

    jsonBuffer[jsonBufferSize++] = '[';
    jsonBuffer[jsonBufferSize] = ']';
    GetKeyState(0);
    while (1) {
        AVPacket* pOutputPacket = pContext->pOutputPacket;
        if (pOutputPacket->dts > 0 && lastDts < pOutputPacket->dts) {
            // Setup first dts and timestamps
            lastDts = pOutputPacket->dts;
        }
        if (lastDts > 0) {
            if (key_logger(prevKeys, nextKeys, KeyWatchList, jsonKeyArrayBuffer) > 0)
            { // Keys have changed from previous buffer
                if (sprintf(jsonObjBuffer, jsonObjFrame, lastDts, lastDts - lastPtsEvent, jsonKeyArrayBuffer) + jsonBufferSize < 1024)
                {
                    if (jsonBufferSize > 1) {
                        jsonBuffer[jsonBufferSize - 1] = ',';
                    }

                    jsonBufferSize += sprintf(jsonBuffer + jsonBufferSize, jsonObjBuffer);
                    jsonBuffer[jsonBufferSize++] = ']';
                }
                else
                    logging("Keylogger-thread: Not enough room in jsonBuffer for packet. pts: %d ", lastDts);
                
            }

            if (lastDts - lastPtsEvent > oneSecond) {
                if (jsonBufferSize > 1) {
                    logging("Other OutputPacket->pts %d ", lastDts);
                    logging("      OutputPacket->data %s ", jsonBuffer);

                    if (sendMetadataRequest(awsArnChannel, jsonBuffer) < 0) {
                        logging("Failed to send metadata");
                    }

                    memset(jsonBuffer, 0, 1024);
                    jsonBufferSize = 0;
                    jsonBuffer[jsonBufferSize++] = '[';
                    jsonBuffer[jsonBufferSize] = ']';
                }

                lastPtsEvent = lastDts;
            }
        }
        av_usleep(40000);
    }
    logging("Keylogger exit");
    return (void*)(NULL);
}


int key_logger(BYTE* prevKeys, BYTE* nextKeys, char* watchList, char* buffer)
{
    int bufferSize = 0;
    int hasChanged = 0;



    if (GetKeyboardState(nextKeys))
    {
        buffer[bufferSize++] = '[';
        for (int i = 0; watchList[i]; i++)
        {
            uint8_t keyIndex = watchList[i];
            if (hasChanged == 0 && (nextKeys[keyIndex] & 0x80) != (prevKeys[keyIndex] & 0x80)) {
                hasChanged = 1;
            }
            if (nextKeys[keyIndex] & 0b10000000) {
                bufferSize += sprintf(buffer + bufferSize, "%d,", keyIndex);
            }
            prevKeys[keyIndex] = nextKeys[keyIndex];
        }

        buffer[ bufferSize > 1 ? bufferSize - 1 : bufferSize++ ] = ']';

        buffer[bufferSize++] = '\0';
    }
    GetKeyState(0);
    return hasChanged;
}
