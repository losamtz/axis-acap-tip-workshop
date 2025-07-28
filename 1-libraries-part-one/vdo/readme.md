# Video Stream API (VDO)

## Description

The VdoChannel API provides:

- query video channel types
- query supported video resolutions

The VdoStream API provides:

- video and image stream
- video and image capture
- video and image configuration

Available video compression formats through VDO
The table below shows available subformats for corresponding YUV format.

| Sub Formats | Corresponding format |
|-------------|----------------------|
| nv12        | YUV                  |
| y800        | YUV                  |

An application to start a vdo stream can be found at vdostream, where the first argument is a string describing the video compression format. It takes h264 (default), h265, jpeg, nv12, and y800 as inputs. Both nv12 and y800 correspond to YUV format of VDO.



## VdoStream: The Video Stream

Represents an active video stream (e.g., from the camera sensor or encoder).

It's the source or sink of video frames, depending on your use case:

- If you're reading frames (e.g., image processing), you get frames from the stream.
- If you're providing frames (e.g., custom image provider), you push frames to the stream.

## VdoBuffer: The Video Buffer

Represents a single video frame, including:

- Pixel data
- Metadata (e.g., timestamp, resolution, format)
- Allocated by the stream or via a VdoBufferPool.

## Relationship Between VdoStream and VdoBuffer
Here's how they typically relate in an ACAP application:

### 1. Buffer Allocation

Buffers are allocated from the stream:

```c

vdo_stream_get_buffer(stream, &buffer);

```
### 2. Buffer Filling or Rendering

You fill the buffer (e.g., draw something on it with Cairo).

 => If you’re implementing an image provider, this is where you draw the content.

### 3. Buffer Submission * not correct. To be fixed!!!
!!!!
You submit the buffer back to the stream to be rendered or sent:

```c

vdo_stream_submit_buffer(stream, buffer);
```
!!!!
## High-Level Lifecycle (Image Provider Example)

1. Axis camera starts a VdoStream.
2. The app receives a callback or notification to provide a new frame.
3. The app:

- Allocates or reuses a VdoBuffer
- Draws content (e.g., overlays, graphics, logo)
- Submits it back via vdo_stream_submit_buffer()

Axis video stack displays or encodes the buffer as a video frame.


## Optional Additions
You can use VdoBufferPool to manage buffers efficiently.
You can map the buffer memory to Cairo or OpenCV to draw or process it.

