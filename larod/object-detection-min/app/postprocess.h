#include <bbox.h>

// define box struct
typedef struct {
    float y_min;
    float x_min;
    float y_max;
    float x_max;
    float score;
    int label;
} box;

bbox_t* setup_bbox(uint32_t channel);
bool parse_and_postprocess_output_tensors(bbox_t* bbox,
                                          model_tensor_output_t* tensor_outputs,
                                          float confidence_threshold,
                                          char** labels,
                                          unsigned int* post_processing_ms);

