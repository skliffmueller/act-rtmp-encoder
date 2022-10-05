# IVS RTMP Timed Metadata Encoder

This is a demonstration of an Amazon IVS and attempting to sync multiple metadata cues in a video stream.

## Documents

[Technology Overview](/docs/TECH.md)
[Story behind this project](/docs/STORY.md)

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

