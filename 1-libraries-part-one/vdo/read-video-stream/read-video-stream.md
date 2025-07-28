# Read Video Stream

## Description:

This application show case the basic use of this library which is reading the stream and frames properties.

## Setup
---
#### Set video stream settings

```c

VdoMap* settings = vdo_map_new();

```

#### Specify settings - Set format

H264 format for recording:
```c
vdo_map_set_uint32(settings, "format", VDO_FORMAT_H264); // H264
```

NV12 format for DL models input
```c
vdo_map_set_uint32(settings, "format", VDO_FORMAT_YUV);  // YUV - NV12
vdo_map_set_string(settings, "subformat", "NV12");

```


#### Specify settings - Set width and height

```c

vdo_map_set_uint32(settings, "width", 640);
vdo_map_set_uint32(settings, "height", 360);

```

#### Create a new stream object with configured settings

```c

VdoStream stream = vdo_stream_new(settings, NULL, &error);

```

At this point you can retrieve information from the created object

```c

VdoMap* info = vdo_stream_get_info(stream, &error);

vdo_map_get_uint32(info, "format", 0);
vdo_map_get_uint32(info, "width", 0);
vdo_map_get_uint32(info, "height", 0);
vdo_map_get_uint32(info, "framerate", 0);

```
---
## Start the stream

---
#### Start

```c

vdo_stream_start(stream, &error); // return boolean

```

#### Create a buffer for each frame

```c

VdoBuffer* buffer = vdo_stream_get_buffer(stream, &error);
VdoFrame* frame   = vdo_buffer_get_frame(buffer);

```
---

## Actions after starting the stream

---
#### Get frame info 

Check last frame is available - Tests whether this frame is last buffer

```c

vdo_frame_get_is_last_buffer(frame); 

```

Get type of frame: I, P, yuv

```c

vdo_frame_get_frame_type(frame);

```

Get frame sequence number:

```c
vdo_frame_get_sequence_nbr(frame);

```

Get Frame size:

```c

vdo_frame_get_size(frame);

```

#### Get buffer data (the frame) for recording 

Pointer to underlaying buffer

```c
gpointer data = vdo_buffer_get_data(buffer);

```

Write data to sd card or networkshare (in this case to /dev/null - noware)

```c
gchar* output_file = "/dev/null";
FILE* dest_f = fopen(output_file, "wb");
fwrite(data, vdo_frame_get_size(frame), 1, dest_f);

```

Release buffer

```c

vdo_stream_buffer_unref(stream, &buffer, &error);

```
---

## Build

```bash
docker build --tag read-video-stream --build-arg ARCH=aarch64 .
```
```bash
docker cp $(docker read-video-stream):/opt/app ./build
```

