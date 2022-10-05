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


#define		ID3v2_FRAME_FLAG_READ_ONLY			0b1 << 5

const char *ID3v2FileIdentifier = "ID3";
const char *ID3v2VersionCode = "\x03\x00";
const char *ID3v2ExperimentalFlag = 0b00100000;
const char *ID3v2PRIVFrameId = "PRIV";

int priv_frame_append_size(uint8_t *buffer, uint64_t size, int length, int bitspace)
{
	const char mask = 0xff >> (bitspace - 8);

	for (int i = 0; i < length; i++)
	{
		buffer[i] = size & mask;
		size = size >> bitspace;
	}
	return 0;
}

int priv_frame_append_data(uint8_t *buffer, uint8_t *data, const char *frameOwner)
{
	int dataSize = 0;

	while (data[dataSize] != 0x00)
		dataSize++;


}

int priv_frame_write_data(uint8_t* buffer, uint8_t* data, int size, const char* frameOwner)
{
	int i = 0;
	int privFrameSize = 10 + size + sizeof(frameOwner);
	int id3HeaderSize = 10;
	int bufferSize = privFrameSize + id3HeaderSize;
	buffer = av_mallocz(bufferSize);

	// ID3 Header Identifier
	buffer[i++] = "I";
	buffer[i++] = "D";
	buffer[i++] = "#";

	// Version Code
	buffer[i++] = 0x03;
	buffer[i++] = 0x00;

	// Experimental Flag
	buffer[i++] = 0b00100000;

	// ID3 Packet Size
	for (int j = 3; j >= 0; j--)
	{
		buffer[i + j] = privFrameSize & 0b01111111;
		privFrameSize = privFrameSize >> 7;
	}
	i += 4;

	// PRIV Frame header
	buffer[i++] = "P";
	buffer[i++] = "R";
	buffer[i++] = "I";
	buffer[i++] = "V";

	// PRIV frame data size
	int dataSize = size;
	for (int j = 3; j >= 0; j--)
	{
		buffer[i + j] = dataSize & 0b11111111;
		dataSize = dataSize >> 8;
	}
	i += 4;

	// PRIV Read Only Flag
	buffer[i++] = 0b00100000;

	for (int j = 0; j < sizeof(frameOwner); j++)
	{
		buffer[i++] = frameOwner[j];
	}

	buffer[i++] = 0x00;

	for (int j = 0; j < size; j++)
	{
		buffer[i++] = data[j];
	}

	return bufferSize;
}

int priv_frame_write_data(uint8_t* buffer, uint8_t* data, int size, const char* frameOwner)
{
	int i = 0;
	int frameOwnerSize = 0;
	while (frameOwner[frameOwnerSize++] != 0x00) {}

	int privFrameSize = 10 + size + frameOwnerSize;
	int id3HeaderSize = 10;
	int bufferSize = privFrameSize + id3HeaderSize;
	uint8_t* newBuffer = av_mallocz(bufferSize);

	memcpy(newBuffer, "ID3\x03\x00\x00", 6); // ID3 header // Version Code // Experimental Flag
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
		newBuffer[i + j] = dataSize & 0b11111111;
		dataSize = dataSize >> 8;
	}
	i += 4;

	// PRIV Read Only Flag
	newBuffer[i++] = 0b00100000;

	memcpy(newBuffer + i, frameOwner, frameOwnerSize);
	i += frameOwnerSize;

	newBuffer[i++] = 0x00;

	memcpy(newBuffer + i, data, size);
	i += size;

	newBuffer[i++] = 0x00;

	av_realloc(buffer, bufferSize);

	memcpy(buffer, newBuffer, bufferSize);

	return bufferSize;
}