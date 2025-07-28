# Reead Video Stream

## Description:

This application show case the basic use of this library which is reading the stream and frames properties.

## Setup

### set video stream settings

```c

VdoMap* settings = vdo_map_new();

```

### Specify settings - Set format

```c

vdo_map_set_uint32(settings, "format", VDO_FORMAT_H264); // VDO_FORMAT_H264 from lib? 

```

### Specify settings - Set width and height

```c

vdo_map_set_uint32(settings, "width", 640);
vdo_map_set_uint32(settings, "height", 360);

```

### Create a new stream object with configured settings

```c

VdoStream stream = vdo_stream_new(settings, NULL, &error);

```

### Attch it to the vdo subsystem to start the stream

```c

vdo_stream_attach(stream, NULL, &error);

```
