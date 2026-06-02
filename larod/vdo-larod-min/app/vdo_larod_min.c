#include <larod.h>
#include <.glib.h>

#include <errno.h>
#include <fcntl.h>
#include <gmodule.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>


#include "vdo-error.h"
#include "vdo-frame.h"
#include "vdo-types.h"

#include <poll.h>
#include <unistd.h>


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
        syslog(LOG_ERR, "Unexpected vdo error %s", error->message);
        return EXIT_FAILURE;
    }
}

int main(int argc, char** argv) {
    char* device_name                     = argv[1];
    char* model_file                      = argv[2];
    char* image_fit                       = argv[3];
    g_autoptr(GError) error           = NULL;
    g_autoptr(VdoStream) vdo_stream       = NULL;
    g_autoptr(VdoMap) vdo_stream_info     = NULL;


    // Stop main loop at signal
    signal(SIGTERM, shutdown);
    signal(SIGINT, shutdown);

    openlog(argv[0], LOG_PID, LOG_INFO);
    syslog(LOG_INFO, "Starting %s", argv[0]);

    // 1. Connect to Larod
    larodConnection* conn;
    if(!larod_connect(&conn, &error)) {
        syslog(LOG_ERR, "Failed to connect to Larod");
        return EXIT_FAILURE;
    }

    syslog(LOG_INFO, "Connected to Larod");

    syslog(LOG_INFO, "Setting up larod connection with chip %s and model file %s", device_name, model_file);

    // 2. Get DLPU device
    larodDevice* device = larodGetDevice(conn, device_name, &error);

    // Fallback to cpu if specified device is not found
    if (!device) {
        syslog(LOG_ERR, "Failed to get device %s: %s", device_name, error->message);
        g_clear_error(&error);
        device = larodGetDevice(conn, "cpu-tflite", &error);
        
    }
    if (!device) {
        syslog(LOG_ERR, "Failed to get fallback device cpu-tflite: %s", error->message);
        return EXIT_FAILURE;
    }

    syslog(LOG_INFO, "Using device %s", larodDeviceGetName(device));

    // 3. Load model
    int larod_model_fd = open(model_file, O_RDONLY);
    if (larod_model_fd < 0) {
        syslog(LOG_ERR, "Failed to open model file %s: %s", model_file, strerror(errno));
        return EXIT_FAILURE;
    }

    larodModel* model = larodLoadModel(conn, 
        device, 
        larod_model_fd, 
        LAROD_ACCESS_PRIVATE, 
        "Video Larod Model",
        NULL, 
        &error);
        
    if (!model) {
        syslog(LOG_ERR, "Failed to load model from file %s: %s", model_file, error->message);
        return EXIT_FAILURE;    
    }

    syslog(LOG_INFO, "Model loaded successfully");

    // 4. Model Input
    size_t numInputs = 0;
    larodTensor** input_tensors = larodCreateModelInputs(model, 0, &numInputs, &error);
    if (!input_tensors) {
        syslog(LOG_ERR, "Failed to create model inputs: %s", error->message);
        return EXIT_FAILURE;    
    }

    // 5. Model Output
    size_t numOutputs = 0;
    larodTensor** output_tensors = larodCreateModelOutputs(model, 0, &numOutputs, &error);
    if (!output_tensors) {
        syslog(LOG_ERR, "Failed to create model outputs: %s", error->message);
        return EXIT_FAILURE;    
    }

    // 6.Larod Job Request

    larodJobRequest* jobRequest = larodCreateJobRequest(model, 
                                                        input_tensors, 
                                                        numInputs, 
                                                        output_tensors, 
                                                        numOutputs, 
                                                        NULL, 
                                                        &error);
    if (!jobRequest) {
        syslog(LOG_ERR, "Failed to create larod job request: %s", error->message);
        return EXIT_FAILURE;    
    }

    // 7. Run inference

    if(!larodRunJob(conn, jobRequest, &error)) {
        syslog(LOG_ERR, "Failed to run larod job: %s", error->message);
        return EXIT_FAILURE;    
    } else {
        // 8. Process output tensors
        syslog(LOG_INFO, "Person detected with confidence %.2f", (float)((uint_8)output_tensors[0].data/2.55f));
        syslog(LOG_INFO, "Car detected with confidence %.2f", (float)((uint_8)output_tensors[1].data/2.55f));
    }
    
    syslog(LOG_INFO, "Larod job ran successfully");
    syslog(LOG_INFO, "");
    closelog();
    return EXIT_SUCCESS;
}