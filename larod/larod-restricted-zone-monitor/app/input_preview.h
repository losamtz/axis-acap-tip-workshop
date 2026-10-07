#pragma once
#include <glib.h>
#include <larod.h>

/* Diagnostic only: copy an RGB UINT8 tensor into tightly packed RGB base64.
 * Does not own the tensor or its fd. Call while the input buffer is still held. */
gchar* input_preview_capture(larodTensor* tensor, unsigned int width, unsigned int height,
                             unsigned int row_pitch, gchar** message);
