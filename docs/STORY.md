# Story Time

This is a diary of the attempts made so you don't have too! I still learned a lot through this journey, and I hope my experience can be valuable to you.

## Attempt Log


 - Inject Timed Metadata into RTMP stream
    - Unsuccessful, amazon does not support this directly with IVS Live Stream. 
    - Would require AWS Elemental Media services. soon?^tm
 - FFmpeg encoding to hls
    - Unsuccessful, buggy, timed metadata cues would spam the event listener in Amazon IVS player.
    - Suspect this is a pts/dts timing issue, cannot decode wasm to diagnose problem correctly
 - FFmpeg + AWS C++ SDK (IVS PutMetadata)
    - Success? Was able to create a working product
    - Still a pts timing issue.

### Injecting Timed Metadata into RTMP stream

This was the first attemped. Amazon IVS service provides all the escentials to broadcast and record a live stream with the quickest setup. tl;dr You can connect to an rtmp link provided by Amazon, using broadcast software like [OBS Studio](https://obsproject.com/). But there is no software out there that even hints at "Timed Metadata". Except for some snippets of code in ffmpeg libraries.

The theory, is to use ffmpeg to inject metadata packets into the rtmp stream. Something like this:
```
[Broadcast Software] ---(RTMP)---> [ffmpeg] ---(RTMP)---> [Amazon IVS]
                                      ^
                                      |
                               [Timed Metadata]
```

In the RTMP stream, the media streams are encoded in flv format. The flv muxer encodes video/audio/metadata packets with AMF headers described [here](https://rtmp.veriskope.com/pdf/amf3-file-format-spec.pdf). And some of the FLV specs described [here](https://rtmp.veriskope.com/pdf/video_file_format_spec_v10.pdf)

Now keep in mind "Timed Metadata" and "Metadata" are two different things. In these documents "Metadata" in it's original context is data that pertains to information about the media stream. It is information that typically gets passed along a keyframe to describe things like timecode translations, framerates, encoder information, color processing, or descriptive information like authors, copyrights, album covers.

"Timed Metadata" exists outside the context of "Metadata". The only relation it has to "Metadata" is that it uses the same packet headers and formating to send and receive messages, only that Timed Metadata is it's own packet injected at the precise moment of the event. Timed Metadata more closely resembles Closed Caption cues.

So within the flv stream you can send "onTextData" and/or your own AMF packet of data when using the ffmpeg flv encoder.

https://git.ffmpeg.org/gitweb/ffmpeg.git/blob/refs/heads/release/4.4:/libavformat/flvenc.c#l1000
```c
     if (par->codec_type == AVMEDIA_TYPE_DATA ||
         par->codec_type == AVMEDIA_TYPE_SUBTITLE ) {
         int data_size;
         int64_t metadata_size_pos = avio_tell(pb);
         if (par->codec_id == AV_CODEC_ID_TEXT) {
             // legacy FFmpeg magic?
             avio_w8(pb, AMF_DATA_TYPE_STRING);
             put_amf_string(pb, "onTextData");
             avio_w8(pb, AMF_DATA_TYPE_MIXEDARRAY);
             avio_wb32(pb, 2);
             put_amf_string(pb, "type");
             avio_w8(pb, AMF_DATA_TYPE_STRING);
             put_amf_string(pb, "Text");
             put_amf_string(pb, "text");
             avio_w8(pb, AMF_DATA_TYPE_STRING);
             put_amf_string(pb, pkt->data);
             put_amf_string(pb, "");
             avio_w8(pb, AMF_END_OF_OBJECT);
         } else {
             // just pass the metadata through
             avio_write(pb, data ? data : pkt->data, size);
         }
         /* write total size of tag */
         data_size = avio_tell(pb) - metadata_size_pos;
         avio_seek(pb, metadata_size_pos - 10, SEEK_SET);
         avio_wb24(pb, data_size);
         avio_seek(pb, data_size + 10 - 3, SEEK_CUR);
         avio_wb32(pb, data_size + 11);
```

Unfortunantly Amazon IVS doesn't recognize any of these patterns within the stream, and this idea was thrown out. I even went as far as to compose my own AMF packet and inject it into the stream.

```c
static int create_amf_metadata_buffer(uint8_t* buffer, const char* text)
{
    int i = 0;
    size_t len = strlen(text);

    buffer[i++] = AMF_DATA_TYPE_STRING;
    amf_write_str(buffer + i, "onTextData"); // Tried different configurations of this field
    i += 10;
    buffer[i++] = AMF_DATA_TYPE_MIXEDARRAY;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x00;
    buffer[i++] = 0x02;
    amf_write_str(buffer + i, "type");
    i += 4;
    buffer[i++] = AMF_DATA_TYPE_STRING;
    amf_write_str(buffer + i, "Metadata"); // Tried different configurations of this field
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
```

I even attempted to inject an ID3 framed packet in the rtmp flv stream with no success...


### FFmpeg encoding to HLS

The previous experiment failed, but I really wanted to have these Timed Metadata cues in sync with the packet streams. After a deep dive into hls stream encoding, and backwards engineering of ffmpeg, I saw a real possibility to inject ID3 timed metadata into an hls stream. More specifically in the mpegts muxer. HLS by default, uses mpegts to encode mpeg2/hvec/h264 video. You may use mp4, but I did not see a clean way of injecting metadata, and AWS is using mpegts, so why not keep to the standard.

The flow now looks something like this:
```
[Broadcast Software] ---(RTMP)---> [ffmpeg] ---(HLS)---> [HD Storage/Upload Service]
                                      ^
                                      |
                               [Timed Metadata]
```

I found a lot of useful information in this document [Integration of Timed Metadata in the HTTP Streaming Architecture by Politecnico Di Torino](https://webthesis.biblio.polito.it/16655/1/tesi.pdf)

The findings I've taken from the document, ID3 metadata frames can be injected in an mpegts stream wrapped in a PES frame. This theory seems correct when analyzing the packets in one of Amazon's test streams.

![DVBinscpector ffmpeg hls mpegts example](/docs/DVB_Inspector_example_stream.png)

The stream is sending an ID3 metadata encoded buffer with a single PRIV frame, containing owner, and text json data.

In the ffmpeg mpegts encoder (libavformat/mpegtsenc.c) It does seem like it accepts any packet payload to be written to the stream, and it'll wrap the PES frame around the data for you.
mpegts_write_packet_internal
[ffmpeg:libavformat/mpegtsenc.c:1954](https://git.ffmpeg.org/gitweb/ffmpeg.git/blob/refs/heads/release/4.4:/libavformat/mpegtsenc.c#l1954)
```
 static int mpegts_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
 {
     ...
     if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO || size > ts->pes_payload_size) {
         av_assert0(!ts_st->payload_size);
         // for video and subtitle, write a single pes packet
         mpegts_write_pes(s, st, buf, size, pts, dts,
                          pkt->flags & AV_PKT_FLAG_KEY, stream_id);
         ts_st->opus_queued_samples = 0;
         av_free(data);
         return 0;
     }
     ...
```
Following the path further into the mpegts_write_pes function, we have some logic
[ffmpeg:libavformat/mpegtsenc.c:1432](https://git.ffmpeg.org/gitweb/ffmpeg.git/blob/refs/heads/release/4.4:/libavformat/mpegtsenc.c#l1432)
```
static void mpegts_write_pes(AVFormatContext *s, AVStream *st,
                             const uint8_t *payload, int payload_size,
                             int64_t pts, int64_t dts, int key, int stream_id)
{
    ...
             } else if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA &&
                        st->codecpar->codec_id == AV_CODEC_ID_TIMED_ID3) {
                 *q++ = STREAM_ID_PRIVATE_STREAM_1;
             } else if (st->codecpar->codec_type == AVMEDIA_TYPE_DATA) {
     ...
        // PES packet encoding stuff, and submit packet only with pes + packet buffer

```

So it looks like we will still have to build the ID3 + PRIV frame, but we won't have to worry about the PES frame. ID3 frame and PRIV usage specifications can be found at [id3v2.4.0-structure - ID3.org](https://id3.org/id3v2.4.0-structure) This became more useful when I was able to have DVBinspector and these docs side by side to understand the makeup of an ID3 frame.

You can find the referenced source code for encoding an ID3 PRIV frame at [./experiments/id3enc.c](./experiments/id3enc.c)

You can also find some reminence of code used in the [experiments folder](./experiments)

It did work... technically. I was able to produce an hls file stream into a directory, I made a simple nginx server to host the folder, and I got video playback. I also got Timed metadata! Alittle... too much. It was spamming the browser causing the tab to crash. It was as if it was spamming the same event over and over. From my admittedly janky code above, I feared it was something wrong with my code, and spent quite a few days trying to debug things. But I had no change in effect. And every time the PES ID3 Metadata frame would only appear once every couple seconds according to DVBinspector. I can't really tell the difference other than maybe the pts timecodes are processed differently? I can't be sure. But I threw in the towel on this idea...

![DVBinscpector ffmpeg hls mpegts example](/docs/DVB_Inspector_ffmpeg_hls.png)



### FFmpeg + AWS C++ SDK (IVS PutMetadata)

This would be a more traditional approach, and at this rate one could ask, couldn't I just make some node.js or shell script, and just PutMetadata to the IVS API? Yes... but that doesn't achieve the goals I set out initially, and maybe this project can be used as a framework for injecting Timed Metadata into the stream once Amazon offers the ability *wink*

I ressurected the original RTMP code, and remade it to capture the pts timecode of the video packets in the stream. So the flow now looks something like this:
```
[Broadcast Software] ---(RTMP)---> [ffmpeg] ---(RTMP)---> [Amazon IVS]
                                       |
                             (&videoPacketPointer)
                                       |
                                      \/
                               [Timed Metadata Thread]--->[Interval Push]--->[Amazon IVS Put Metadata]
```

The hopes being I could have multiple json objects in an array, each with an offset pts, submit to AWS API the json data once a second, then on the player side I could sync the pts with the video stream.

Well... It worked, but there is still no reliable way to sync the pts with the video stream given by Amazon IVS accurately. You can get close with time delays, but without a pts value coming from the player, there isn't much one can hope for.

## Shoutouts

https://github.com/leandromoreira/ffmpeg-libav-tutorial#learn-ffmpeg-libav-the-hard-way
