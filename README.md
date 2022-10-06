# IVS RTMP Timed Metadata Encoder

This is a demonstration of an Amazon IVS and attempting to sync multiple metadata cues in a video stream.

[Check out the produced video](http://act-player-video-archive.s3-website-us-east-1.amazonaws.com/)

Also checkout my pairing project [act-keyboard-player](https://github.com/skliffmueller/act-keyboard-player) Which is the Amazon IVS player, playing the stream.

## Documents

[Technology Overview](/docs/TECH.md)

[Story behind this project](/docs/STORY.md)

## ffmpeg vcpkg patch

The issue is discussed in [this ticket](https://trac.ffmpeg.org/ticket/6471), along with the solution in the comments. Below is an implementation of that solution. 

This is required, as there is a bug with the socket hanging up on the rtmps connection. More specifically the TLS socket layer hangs.

In your vcpkg install directory create the following file `<vcpkg_install_dir>/ports/ffmpeg/nonblock-tls.patch`

In that file place this text, it is a raw diff patch, that will be applied on installation of ffmpeg.
```
diff --git a/libavformat/tls_schannel.c b/libavformat/tls_schannel.c
index d4959f7..4c10dad 100644
--- a/libavformat/tls_schannel.c
+++ b/libavformat/tls_schannel.c
@@ -415,8 +415,16 @@ static int tls_read(URLContext *h, uint8_t *buf, int len)
             }
         }
 
-        ret = ffurl_read(s->tcp, c->enc_buf + c->enc_buf_offset,
-                         c->enc_buf_size - c->enc_buf_offset);
+		int set_flag_nonblock = 0;
+		if (h->flags&AVIO_FLAG_NONBLOCK && !(s->tcp->flags&AVIO_FLAG_NONBLOCK)) {
+		  s->tcp->flags |= AVIO_FLAG_NONBLOCK;
+		  set_flag_nonblock = 1;
+		}
+		ret = ffurl_read(s->tcp, c->enc_buf + c->enc_buf_offset,
+						 c->enc_buf_size - c->enc_buf_offset);
+		if (set_flag_nonblock)
+		  s->tcp->flags &= ~AVIO_FLAG_NONBLOCK;				 
+						 			 
         if (ret == AVERROR_EOF) {
             c->connection_closed = 1;
             ret = 0;
diff --git a/libavformat/tls_securetransport.c b/libavformat/tls_securetransport.c
index f6a1a5e..518100c 100644
--- a/libavformat/tls_securetransport.c
+++ b/libavformat/tls_securetransport.c
@@ -198,7 +198,16 @@ static OSStatus tls_read_cb(SSLConnectionRef connection, void *data, size_t *dat
     URLContext *h = (URLContext*)connection;
     TLSContext *c = h->priv_data;
     size_t requested = *dataLength;
+
+	int set_flag_nonblock = 0;
+	if (h->flags&AVIO_FLAG_NONBLOCK && !(c->tls_shared.tcp->flags&AVIO_FLAG_NONBLOCK)) {
+		c->tls_shared.tcp->flags |= AVIO_FLAG_NONBLOCK;
+		set_flag_nonblock = 1;
+	}
     int read = ffurl_read(c->tls_shared.tcp, data, requested);
+	if (set_flag_nonblock)
+		c->tls_shared.tcp->flags &= ~AVIO_FLAG_NONBLOCK;	
+	
     if (read <= 0) {
         *dataLength = 0;
         switch(AVUNERROR(read)) {

```

Now you must update file `<vcpkg_install_dir>/ports/ffmpeg/portfile.cmake` Open this file in your favorite text editor, and you will find something like this at the top:
```
if(VCPKG_TARGET_IS_WINDOWS)
    set(PATCHES 0017-Patch-for-ticket-9019-CUDA-Compile-Broken-Using-MSVC.patch)  # https://trac.ffmpeg.org/ticket/9019
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ffmpeg/ffmpeg
    REF n4.4.1
    SHA512 a53e617937f9892c5cfddb00896be9ad8a3e398dc7cf3b6c893b52ff38aff6ff0cbc61a44cd5f93d9a28f775e71ae82996a5e2b699a769c1de8f882aab34c797
    HEAD_REF master
    PATCHES
        0001-create-lib-libraries.patch
        0003-fix-windowsinclude.patch
        0004-fix-debug-build.patch
        0006-fix-StaticFeatures.patch
        0007-fix-lib-naming.patch
        0009-Fix-fdk-detection.patch
        0010-Fix-x264-detection.patch
        0011-Fix-x265-detection.patch
        0012-Fix-ssl-110-detection.patch
        0013-define-WINVER.patch
        0014-avfilter-dependency-fix.patch  # https://ffmpeg.org/pipermail/ffmpeg-devel/2021-February/275819.html
        0015-Fix-xml2-detection.patch
        0016-configure-dnn-needs-avformat.patch  # https://ffmpeg.org/pipermail/ffmpeg-devel/2021-May/279926.html
        ${PATCHES}
        0018-libaom-Dont-use-aom_codec_av1_dx_algo.patch
        0019-libx264-Do-not-explicitly-set-X264_API_IMPORTS.patch
        0020-fix-aarch64-libswscale.patch
        0021-fix-sdl2-version-check.patch
)
```

In that list under `PATCHES` we will want to add our patch like so:
```
if(VCPKG_TARGET_IS_WINDOWS)
    set(PATCHES 0017-Patch-for-ticket-9019-CUDA-Compile-Broken-Using-MSVC.patch)  # https://trac.ffmpeg.org/ticket/9019
endif()
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ffmpeg/ffmpeg
    REF n4.4.1
    SHA512 a53e617937f9892c5cfddb00896be9ad8a3e398dc7cf3b6c893b52ff38aff6ff0cbc61a44cd5f93d9a28f775e71ae82996a5e2b699a769c1de8f882aab34c797
    HEAD_REF master
    PATCHES
        0001-create-lib-libraries.patch
        0003-fix-windowsinclude.patch
        0004-fix-debug-build.patch
        0006-fix-StaticFeatures.patch
        0007-fix-lib-naming.patch
        0009-Fix-fdk-detection.patch
        0010-Fix-x264-detection.patch
        0011-Fix-x265-detection.patch
        0012-Fix-ssl-110-detection.patch
        0013-define-WINVER.patch
        0014-avfilter-dependency-fix.patch  # https://ffmpeg.org/pipermail/ffmpeg-devel/2021-February/275819.html
        0015-Fix-xml2-detection.patch
        0016-configure-dnn-needs-avformat.patch  # https://ffmpeg.org/pipermail/ffmpeg-devel/2021-May/279926.html
        ${PATCHES}
        0018-libaom-Dont-use-aom_codec_av1_dx_algo.patch
        0019-libx264-Do-not-explicitly-set-X264_API_IMPORTS.patch
        0020-fix-aarch64-libswscale.patch
        0021-fix-sdl2-version-check.patch
		nonblock-tls.patch
)
```

If you have not installed the ffmpeg packages yet, install them now, the patch should take effect. If you have it installed already, you will want to remove ffmpeg `vcpkg remove ffmpeg:x64-windows`, and install again.

## Build

This requires Visual Studio 2022, and vcpkg.

You must first install the following packages:
```
vcpkg -S ffmpeg:x64-windows pthread:x64-windows aws-sdk-cpp[ivs]:x64-windows
```

Then you can build in Visual Studio.

## Run

Make sure your AWS access credentials are setup in the environment variables.

```
AWS_ACCESS_KEY_ID=<keyid> AWS_SECRET_ACCESS_KEY=<secret>
act-rtmp-encoder.exe <input_url> <output_url> <awsArnChannel>
act-rtmp-encoder.exe rtmp://127.0.0.1:1934/stream rtmps://<aws_stream_url> arn:aws:ivs:us-east-1:<your_arn>
```

`input_url` This is the listen server url to bind to.
`output_url` This is the rtmp push live stream to connect to (Amazon IVS)
`awsArnChannel` The arn channel metadata is to be broadcasted too

The broadcast will connect to AWS services once it detects a connection on the local rtmp stream. The local rtmp stream `rtmp://127.0.0.1:1934/stream` is a listen server, so you can use something like OBS Studio and connect to the stream.

![OBS Stream Configuration Dialog](/docs/OBS_Configuration.png)

## Debug

Make sure that your AWS environment variables (AWS_ACCESS_KEY_ID,AWS_SECRET_ACCESS_KEY) are setup correctly. You will also have to add to the "Command Arguments" the input_url output_url and awsArnChannel arguments.

