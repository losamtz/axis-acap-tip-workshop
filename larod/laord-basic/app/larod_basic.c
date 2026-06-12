/**
 * minimal_larod.c
 *
 * The simplest possible VDO + larod application.
 * Blocking VDO, no preprocessing, no tensor tracking, no poll().
 * ~100 lines of actual logic.
 *
 * Only works on backends that accept RGB directly (e.g. a9-dlpu-tflite).
 * VDO delivers RGB at the model's resolution, frames go straight to inference.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <syslog.h>
#include <unistd.h>

#include "larod.h"
#include "vdo-buffer.h"
#include "vdo-error.h"
#include "vdo-frame.h"
#include "vdo-map.h"
#include "vdo-stream.h"
#include "vdo-types.h"

#include <glib.h>

#define DEVICE_NAME  "a9-dlpu-tflite"
#define MODEL_PATH   "/usr/local/packages/minimal_larod/model/model.tflite"

static volatile sig_atomic_t running = 1;
static void on_signal(int s) { (void)s; running = 0; }

int main(void) {
    larodConnection* conn  = NULL;
    larodError*      error = NULL;

    openlog("minimal_larod", LOG_PID | LOG_CONS, LOG_USER);
    signal(SIGTERM, on_signal);
    signal(SIGINT,  on_signal);

    /* ── 1. Connect to larod ── */
    if (!larodConnect(&conn, &error)) {
        syslog(LOG_ERR, "larodConnect: %s", error->msg);
        return EXIT_FAILURE;
    }

    /* ── 2. Load model ── */
    int model_fd = open(MODEL_PATH, O_RDONLY);
    const larodDevice* device = larodGetDevice(conn, DEVICE_NAME, 0, &error);
    larodModel* model = larodLoadModel(conn, model_fd, device,
                                       LAROD_ACCESS_PRIVATE, "", NULL, &error);
    if (!model) {
        syslog(LOG_ERR, "larodLoadModel: %s", error->msg);
        return EXIT_FAILURE;
    }
    syslog(LOG_INFO, "Model loaded");

    /* ── 3. Get model input size ── */
    size_t num_in = 0;
    larodTensor** tmp_in = larodAllocModelInputs(conn, model, 0, &num_in, NULL, &error);
    const larodTensorDims* dims = larodGetTensorDims(tmp_in[0], &error);
    unsigned int h = dims->dims[1];
    unsigned int w = dims->dims[2];
    syslog(LOG_INFO, "Model input: %ux%u", w, h);
    larodDestroyTensors(conn, &tmp_in, num_in, &error);

    /* ── 4. Allocate output tensors + mmap ── */
    size_t num_out = 0;
    larodTensor** out_tensors = larodAllocModelOutputs(conn, model,
                                    LAROD_FD_PROP_READWRITE | LAROD_FD_PROP_MAP,
                                    &num_out, NULL, &error);
    void* out_data[2] = {NULL, NULL};
    for (size_t i = 0; i < num_out && i < 2; i++) {
        int fd = larodGetTensorFd(out_tensors[i], &error);
        size_t sz = 0;
        larodGetTensorFdSize(out_tensors[i], &sz, &error);
        out_data[i] = mmap(NULL, sz, PROT_READ, MAP_SHARED, fd, 0);
    }

    /* ── 5. Create VDO stream (blocking, RGB, model resolution) ── */
    VdoMap* settings = vdo_map_new();
    vdo_map_set_uint32(settings, "format", VDO_FORMAT_RGB);
    vdo_map_set_uint32(settings, "buffer.count", 2);
    vdo_map_set_double(settings, "framerate", 2.0);
    vdo_map_set_string(settings, "image.fit", "scale");
    VdoPair32u res = { .w = w, .h = h };
    vdo_map_set_pair32u(settings, "resolution", res);
    /* socket.blocking defaults to true — vdo_stream_get_buffer will block */

    GError* vdo_err = NULL;
    VdoStream* stream = vdo_stream_new(settings, NULL, &vdo_err);
    g_object_unref(settings);
    vdo_stream_start(stream, &vdo_err);
    syslog(LOG_INFO, "VDO stream started (blocking, RGB %ux%u)", w, h);

    /* ── 6. Allocate input tensors (one per buffer) ── */
    larodTensor** in_tensors[2] = {NULL, NULL};
    int duped_fds[2] = {-1, -1};
    int tracked_vdo_fds[2] = {-1, -1};

    /* ── 7. Inference job (created lazily) ── */
    larodJobRequest* job = NULL;

    /* ── 8. Main loop ── */
    while (running) {
        VdoBuffer* buf = vdo_stream_get_buffer(stream, &vdo_err);  /* blocks */
        if (!buf) continue;

        int vdo_fd = vdo_buffer_get_fd(buf);

        /* Find or create tracked slot for this buffer */
        int slot = -1;
        for (int i = 0; i < 2; i++) {
            if (tracked_vdo_fds[i] == vdo_fd) { slot = i; break; }
        }
        if (slot == -1) {
            /* First time seeing this buffer — set up tensor */
            for (int i = 0; i < 2; i++) {
                if (tracked_vdo_fds[i] == -1) { slot = i; break; }
            }
            size_t n = 0;
            in_tensors[slot] = larodAllocModelInputs(conn, model, 0, &n, NULL, &error);
            larodSetTensorFdProps(in_tensors[slot][0],
                                 LAROD_FD_PROP_MAP | LAROD_FD_PROP_DMABUF, &error);

            int64_t offset = vdo_buffer_get_offset(buf);
            size_t cap     = vdo_buffer_get_capacity(buf);
            int duped      = dup(vdo_fd);

            larodSetTensorFd(in_tensors[slot][0], duped, &error);
            larodSetTensorFdOffset(in_tensors[slot][0], offset, &error);
            larodSetTensorFdSize(in_tensors[slot][0], cap, &error);
            larodTrackTensor(conn, in_tensors[slot][0], &error);

            tracked_vdo_fds[slot] = vdo_fd;
            duped_fds[slot] = duped;
            syslog(LOG_INFO, "Tracked buffer slot %d", slot);
        }

        /* Create or update job */
        if (!job) {
            job = larodCreateJobRequest(model,
                                        in_tensors[slot], 1,
                                        out_tensors, num_out,
                                        NULL, &error);
        } else {
            larodSetJobRequestInputs(job, in_tensors[slot], 1, &error);
        }

        /* Run inference */
        if (larodRunJob(conn, job, &error)) {
            uint8_t* person = (uint8_t*)out_data[0];
            uint8_t* car    = (uint8_t*)out_data[1];
            syslog(LOG_INFO, "Person: %.1f%% — Car: %.1f%%",
                   *person / 2.55f, *car / 2.55f);
        }

        vdo_stream_buffer_unref(stream, &buf, &vdo_err);
    }

    /* ── 9. Cleanup ── */
    larodDestroyJobRequest(&job);
    for (int i = 0; i < 2; i++) {
        if (in_tensors[i]) larodDestroyTensors(conn, &in_tensors[i], 1, &error);
        if (duped_fds[i] >= 0) close(duped_fds[i]);
    }
    larodDestroyTensors(conn, &out_tensors, num_out, &error);
    larodDestroyModel(&model);
    larodDisconnect(&conn, &error);
    vdo_stream_stop(stream);
    g_object_unref(stream);
    close(model_fd);

    syslog(LOG_INFO, "Done");
    closelog();
    return EXIT_SUCCESS;
}