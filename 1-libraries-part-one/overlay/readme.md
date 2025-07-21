# Overlay

In this chapter we will make use of axoverlay to draw boxes, text and finally set an image/logo in the stream. 

We will start with boxes creating several apps: 

Then we will draw text in the following apps:

We will go through some of the setting relevant to the cameras, mostly "normalized", that will scale in different resolutions.

Note:
It is preferable to use Palette color space for large overlays like plain boxes, to lower the memory usage. More detailed overlays like text overlays, should instead use ARGB32 color space.