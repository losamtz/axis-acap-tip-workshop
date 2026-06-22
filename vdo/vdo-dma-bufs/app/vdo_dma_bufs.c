
#include "panic.h"
#include "channel_utils.h"

#include "vdo-frame.h"
#include "vdo-types.h"
#include <bbox.h>

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <syslog.h>
#include <poll.h>

#define APP_NAME "vdo_dma_buffers"

#define MODEL_INPUT_W 640
#define MODEL_INPUT_H 640

volatile sig_atomic_t running = 1;

static void shutdown(int status) {
    (void)status;
    running = 0;
}
static int handle_vdo_failed(GError* error) {
    // Maintenance/Installation in progress (e.g. Global-Rotation)
    if (vdo_error_is_expected(&error)) {
        syslog(LOG_INFO, "Expected vdo error %s", error->message);
        return EXIT_SUCCESS;
    } else {
        panic("Unexpected vdo error %s", error->message);
    }
    return EXIT_FAILURE;
}

static VdoStream* create_new_vdo_stream(unsigned int channel,
                                        double framerate) {

    g_autoptr(VdoMap) vdo_settings = vdo_map_new();
    g_autoptr(GError) error        = NULL;

    if (!vdo_settings) {
        panic("%s: Failed to create vdo_map", __func__);
    }

    vdo_map_set_uint32(vdo_settings, "channel", channel);
    // format is the image format that is supplied from vdo
    vdo_map_set_uint32(vdo_settings, "format", VDO_FORMAT_YUV);
    // Set initial framerate
    vdo_map_set_double(vdo_settings, "framerate", framerate);

    VdoPair32u resolution = {
        .w = MODEL_INPUT_W,
        .h = MODEL_INPUT_H,
    };
    vdo_map_set_pair32u(vdo_settings, "resolution", resolution);
    // Make it possible to change the framerate for the stream after it is started
    vdo_map_set_boolean(vdo_settings, "dynamic.framerate", true);
    // It is not needed to set buffer.strategy since VDO_BUFFER_STRATEGY_INFINITE is default
    // vdo_map_set_uint32(vdo_settings, "buffer.strategy", VDO_BUFFER_STRATEGY_INFINITE);

    // The number of buffers that vdo will allocate for this stream
    // Normally two buffers are enough and using too many buffers will use
    // more memory in the product
    vdo_map_set_uint32(vdo_settings, "buffer.count", 2);
    // The vdo_stream_get_buffer is non blocking and will return immediately
    // Then we need to poll instead when it is ok to get a buffer
    vdo_map_set_boolean(vdo_settings, "socket.blocking", false);
    vdo_map_set_string(vdo_settings, "image.fit", "scale");
    /*
     * This sample only reads camera frames. VDO owns the buffers and exposes
     * them to the app as file descriptors. The default consumer access is
     * enough for demonstrating DMA-BUF inspection.
     */

    // Create a vdo stream using the vdoMap filled in above
    g_autoptr(VdoStream) vdo_stream = vdo_stream_new(vdo_settings, NULL, &error);
    if (!vdo_stream) {
        panic("%s: Failed creating vdo stream: %s", __func__, error->message);
    }
    syslog(LOG_INFO, "Dump of vdo stream settings map =====");
    vdo_map_dump(vdo_settings);

    return g_steal_pointer(&vdo_stream);
}

static void log_stream_info(VdoStream* stream) {
    g_autoptr(GError) error = NULL;
    g_autoptr(VdoMap) info = vdo_stream_get_info(stream, &error);

    if (!info) {
        panic("%s: Failed to get vdo stream info: %s", __func__, error->message);
    }

    const char* buffer_type = vdo_map_get_string(info, "buffer.type", NULL, "unknown");
    syslog(LOG_INFO,
           "Stream info: %ux%u pitch=%u format=%u buffer.type=%s buffers=%u",
           vdo_map_get_uint32(info, "width", 0),
           vdo_map_get_uint32(info, "height", 0),
           vdo_map_get_uint32(info, "pitch", 0),
           vdo_map_get_uint32(info, "format", 0),
           buffer_type,
           vdo_map_get_uint32(info, "buffer.count", 0));
}

static void inspect_dma_buffer(VdoBuffer* buffer) {
    int fd = vdo_buffer_get_fd(buffer);

    if (fd < 0) {
        syslog(LOG_WARNING, "VDO buffer has no fd");
        return;
    }

    int64_t offset = vdo_buffer_get_offset(buffer);
    size_t capacity = vdo_buffer_get_capacity(buffer);
    VdoFrame* frame = vdo_buffer_get_frame(buffer);
    size_t frame_size = frame ? vdo_frame_get_size(frame) : 0;

    syslog(LOG_INFO,
           "DMA-BUF fd=%d offset=%" G_GINT64_FORMAT " capacity=%zu frame_size=%zu",
           fd,
           offset,
           capacity,
           frame_size);

    void* mapped = mmap(NULL, capacity, PROT_READ, MAP_SHARED, fd, 0);

    if (mapped == MAP_FAILED) {
        syslog(LOG_ERR, "mmap failed: %s", strerror(errno));
        return;
    }

    uint8_t* bytes = (uint8_t*)mapped;
    size_t start = offset >= 0 ? (size_t)offset : 0u;
    size_t bytes_to_dump = 32u;

    if (start >= capacity) {
        syslog(LOG_WARNING, "Buffer offset is outside capacity");
        munmap(mapped, capacity);
        return;
    }
    if (start + bytes_to_dump > capacity) {
        bytes_to_dump = capacity - start;
    }

    char dump[128] = {0};
    char tmp[16];

    for (size_t i = 0; i < bytes_to_dump; i++) {
        snprintf(tmp, sizeof(tmp), "%02X ", bytes[start + i]);
        strncat(dump,
                tmp,
                sizeof(dump) - strlen(dump) - 1);
    }

    syslog(LOG_INFO, "First %zu bytes at image offset: %s", bytes_to_dump, dump);
    munmap(mapped, capacity);
}


int main(int argc, char** argv) {

    (void)argc;
    (void)argv;
    g_autoptr(GError) vdo_error = NULL;
    g_autoptr(VdoStream) vdo_stream = NULL;
    // Stop main loop at signal
    signal(SIGTERM, shutdown);
    signal(SIGINT, shutdown);

    openlog(APP_NAME, LOG_PID | LOG_CONS, LOG_USER);
    // Set to a framerate that is sufficient for inference
    double vdo_stream_framerate = 30.0;
    // The vdo channel to be used
    unsigned int vdo_channel = 1;
    
    
    
    vdo_stream = create_new_vdo_stream(vdo_channel,
                                       vdo_stream_framerate);
    if (!vdo_stream) {
        return handle_vdo_failed(vdo_error);
    }
    int fd = vdo_stream_get_fd(vdo_stream, &vdo_error);
    if (fd < 0) {
        return handle_vdo_failed(vdo_error);
    }
    log_stream_info(vdo_stream);

    struct pollfd fds = {
        .fd     = fd,
        .events = POLL_IN,
    };

    if (!vdo_stream_start(vdo_stream, &vdo_error)) {
        return handle_vdo_failed(vdo_error);
    }
    syslog(LOG_INFO, "Start fetching video frames from VDO");
    printf("Stream started\n");

    while (running) {
        int status = 0;
        do {
            // If poll returns -1 then errno is set
            // if the errno is set to EINTR then just
            // continue this loop
            status = poll(&fds, 1, -1);
        } while (status == -1 && errno == EINTR);

        if (status < 0) {
            panic("Failed to poll with status %d", status);
        }

        VdoBuffer* vdo_buf = vdo_stream_get_buffer(vdo_stream, &vdo_error);
        if (!vdo_buf && g_error_matches(vdo_error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
            g_clear_error(&vdo_error);
            continue;
        }
        if (!vdo_buf) {
            return handle_vdo_failed(vdo_error);
        }

        if (vdo_buf) {

            inspect_dma_buffer(vdo_buf);
            /*
             * Return the buffer to VDO. Do not read the mapped pointer or
             * VdoBuffer after this point; VDO may reuse the buffer for a new
             * frame immediately.
             */
            if (!vdo_stream_buffer_unref(vdo_stream, &vdo_buf, &vdo_error)) {
                return handle_vdo_failed(vdo_error);
            }
        }
    }

    printf("Stopping...\n");

    g_object_unref(vdo_stream);

    return EXIT_SUCCESS;
}
