#pragma once

#include <windows.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>

#ifndef KEYLOGGER_THREAD_H_   /* Include guard */
#define KEYLOGGER_THREAD_H_

typedef struct KeyLoggerContext {
    AVPacket* pOutputPacket;
    AVRational* ptsTimebase;
} KeyLoggerContext;

void* keyLoggerThread(void** vargp);

int key_logger(BYTE* prevKeys, BYTE* nextKeys, char* watchList, char* buffer);

#endif // KEYLOGGER_THREAD_H_