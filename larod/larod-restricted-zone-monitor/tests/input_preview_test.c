#include "input_preview.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    gchar* path = NULL;
    int fd = g_file_open_tmp("larod-preview-XXXXXX", &path, NULL);
    assert(fd >= 0);
    unlink(path);
    g_free(path);
    /* Nonzero fd offset plus two bytes of row padding must not become pixels. */
    const unsigned char source[] = {99, 99, 99, 99, 99,
                                    255, 0, 0, 0, 255, 0, 88, 88,
                                    0, 0, 255, 255, 255, 255};
    const unsigned char expected[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    assert(write(fd, source, sizeof(source)) == (ssize_t)sizeof(source));
    larodTensor tensor = {.fd = fd, .offset = 5, .capacity = sizeof(source),
                          .datatype = LAROD_TENSOR_DATA_TYPE_UINT8,
                          .layout = LAROD_TENSOR_LAYOUT_NHWC};
    gchar* error = NULL;
    gchar* encoded = input_preview_capture(&tensor, 2, 2, 8, &error);
    assert(encoded && !error);
    gsize length;
    guchar* decoded = g_base64_decode(encoded, &length);
    assert(length == sizeof(expected) && memcmp(decoded, expected, length) == 0);
    g_free(encoded);
    g_free(decoded);

    tensor.capacity--;
    assert(!input_preview_capture(&tensor, 2, 2, 8, &error));
    assert(error);
    g_clear_pointer(&error, g_free);
    tensor.capacity++;
    assert(!input_preview_capture(&tensor, 2, 2, 5, &error));
    g_clear_pointer(&error, g_free);
    tensor.properties = LAROD_FD_PROP_DMABUF;
    /* A normal fd cannot support DMA synchronization: fail instead of silently
     * displaying potentially incoherent data. */
    assert(!input_preview_capture(&tensor, 2, 2, 8, &error));
    assert(error);
    g_free(error);
    close(fd);
    puts("Input preview offset, row padding, bounds, and synchronization tests passed");
}
