
#include "panic.h"
#include "channel_utils.h"

#include "vdo-frame.h"
#include "vdo-types.h"
#include <bbox.h>

#include <math.h>
#include <signal.h>
#include <stdio.h>
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
    vdo_map_set_uint32(vdo_settings,
                   "buffer.access",
                   2u);

    // Create a vdo stream using the vdoMap filled in above
    g_autoptr(VdoStream) vdo_stream = vdo_stream_new(vdo_settings, NULL, &error);
    if (!vdo_stream) {
        panic("%s: Failed creating vdo stream: %s", __func__, error->message);
    }
    syslog(LOG_INFO, "Dump of vdo stream settings map =====");
    vdo_map_dump(vdo_settings);

    return g_steal_pointer(&vdo_stream);
}


static void inspect_dma_buffer(VdoBuffer *buffer)
{
    int fd = vdo_buffer_get_fd(buffer);

    if (fd < 0) {
        g_warning("Could not get DMA fd");
        return;
    }

    size_t size = vdo_buffer_get_capacity(buffer);

    syslog(LOG_INFO, "DMA FD: %d", fd);
    syslog(LOG_INFO, "Buffer size: %zu bytes", size);

    void *mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

    if (mapped == MAP_FAILED) {
        syslog(LOG_ERR, "mmap failed: %s", strerror(errno));
        return;
    }

    uint8_t *bytes = (uint8_t *)mapped;

    char dump[512] = {0};
    char tmp[16];

    for (int i = 99980; i < 100020; i++) {
        snprintf(tmp, sizeof(tmp), "%02X ", bytes[i]);
        strncat(dump,
            tmp,
            sizeof(dump) -
            strlen(dump) - 1);
    }

    syslog(LOG_INFO,
       "Around 100000: %s",
       dump);
    munmap(mapped, size);
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

        g_autoptr(VdoBuffer) vdo_buf = vdo_stream_get_buffer(vdo_stream, &vdo_error);
        if (!vdo_buf && g_error_matches(vdo_error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
            g_clear_error(&vdo_error);
            continue;
        }
        if (!vdo_buf) {
            return handle_vdo_failed(vdo_error);
        }

        if (vdo_buf) {

            inspect_dma_buffer(vdo_buf);
            // IMPORTANT:
            // Release borrowed frame.
            g_object_unref(vdo_buf);
        }
    }

    printf("Stopping...\n");

    g_object_unref(vdo_stream);

    return EXIT_SUCCESS;
}