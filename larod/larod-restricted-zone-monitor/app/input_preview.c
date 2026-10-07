#include "input_preview.h"
#include <errno.h>
#include <linux/dma-buf.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

/* DMA buffers need explicit CPU access synchronization even after preprocessing
 * has completed. Ordinary shared-memory tensor buffers do not need this ioctl. */
static gboolean synchronize(int fd, uint64_t flags) {
    struct dma_buf_sync synchronization = {.flags = flags};
    int result;
    do {
        result = ioctl(fd, DMA_BUF_IOCTL_SYNC, &synchronization);
    } while (result < 0 && errno == EINTR);
    return result == 0;
}

gchar* input_preview_capture(larodTensor* tensor, unsigned int width, unsigned int height,
                             unsigned int row_pitch, gchar** message) {
    *message = NULL;
    uint32_t properties = 0;
    size_t capacity = 0;
    if (width == 0 || height == 0 || width > 1024 || height > 1024 ||
        row_pitch < width * 3 || row_pitch > 1024 * 1024 ||
        larodGetTensorDataType(tensor, NULL) != LAROD_TENSOR_DATA_TYPE_UINT8 ||
        larodGetTensorLayout(tensor, NULL) != LAROD_TENSOR_LAYOUT_NHWC ||
        !larodGetTensorFdProps(tensor, &properties, NULL) ||
        !larodGetTensorFdSize(tensor, &capacity, NULL)) {
        *message = g_strdup("Preview requires a bounded interleaved RGB UINT8 tensor");
        return NULL;
    }
    int fd = larodGetTensorFd(tensor, NULL);
    int64_t offset = larodGetTensorFdOffset(tensor, NULL);
    size_t bytes_needed = (size_t)(height - 1) * row_pitch + width * 3;
    if (fd < 0 || offset < 0 || (uint64_t)offset > capacity ||
        bytes_needed > capacity - (size_t)offset || capacity > 32 * 1024 * 1024) {
        *message = g_strdup("Input tensor buffer is too small or cannot be mapped safely");
        return NULL;
    }
    void* mapping = mmap(NULL, capacity, PROT_READ, MAP_SHARED, fd, 0);
    if (mapping == MAP_FAILED) {
        *message = g_strdup_printf("Cannot map model input: %s", g_strerror(errno));
        return NULL;
    }
    gboolean dma_buffer = (properties & LAROD_FD_PROP_DMABUF) != 0;
    if (dma_buffer && !synchronize(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ)) {
        *message = g_strdup_printf("Cannot synchronize model input: %s", g_strerror(errno));
        munmap(mapping, capacity);
        return NULL;
    }
    size_t packed_size = (size_t)width * height * 3;
    guchar* packed = g_malloc(packed_size);
    const guchar* pixels = (const guchar*)mapping + offset;
    for (unsigned int row = 0; row < height; row++) {
        memcpy(packed + (size_t)row * width * 3, pixels + (size_t)row * row_pitch, width * 3);
    }
    gboolean synchronized = !dma_buffer || synchronize(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
    munmap(mapping, capacity);
    gchar* encoded = NULL;
    if (synchronized) {
        encoded = g_base64_encode(packed, packed_size);
    } else {
        *message = g_strdup("Could not finish CPU access to model input");
    }
    g_free(packed);
    return encoded;
}
