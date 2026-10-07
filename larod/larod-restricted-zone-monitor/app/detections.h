#pragma once
#include <stdbool.h>
#include <stddef.h>

#define MAX_PEOPLE 100
typedef struct {
    float left, top, right, bottom;
    float confidence;
    bool inside;
} Person;

typedef struct {
    Person people[MAX_PEOPLE];
    size_t count;
    size_t raw_count;
    size_t person_candidates;
    float best_person_confidence;
    int top_class;
    float top_confidence;
} Detections;

/* SSD COCO has four float outputs. Capacities are in float elements, not bytes.
 * Class zero means person for the bundled Coral model (not every COCO model). */
bool detections_decode(const float* locations, size_t location_count, const float* classes,
                       size_t class_count, const float* scores, size_t score_count,
                       float detection_count, float threshold, Detections* result);
