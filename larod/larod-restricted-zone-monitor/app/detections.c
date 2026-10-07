#include "detections.h"
#include <math.h>

static float clamp(float value) {
    return fmaxf(0.0f, fminf(1.0f, value));
}

bool detections_decode(const float* locations, size_t location_count, const float* classes,
                       size_t class_count, const float* scores, size_t score_count,
                       float detection_count, float threshold, Detections* result) {
    *result = (Detections){.top_class = -1};
    if (!locations || !classes || !scores || !isfinite(detection_count) || detection_count < 0 ||
        detection_count > MAX_PEOPLE || floorf(detection_count) != detection_count) {
        return false;
    }
    size_t count = (size_t)detection_count;
    if (count > location_count / 4 || count > class_count || count > score_count) {
        return false;
    }
    result->raw_count = count;
    for (size_t index = 0; index < count; index++) {
        /* Keep diagnostic scores before applying the user threshold. Otherwise
         * a weak person and no person at all would look identical in the UI. */
        float label = classes[index];
        float score = scores[index];
        if (isfinite(label) && label >= 0 && label <= 1000 && floorf(label) == label &&
            isfinite(score) && score >= 0 && score <= 1) {
            if (result->top_class < 0 || score > result->top_confidence) {
                result->top_class = (int)label;
                result->top_confidence = score;
            }
            if (label == 0.0f) {
                result->person_candidates++;
                result->best_person_confidence = fmaxf(result->best_person_confidence, score);
            }
        }
        if (!isfinite(classes[index]) || !isfinite(scores[index]) || classes[index] != 0.0f ||
            scores[index] < threshold || scores[index] > 1.0f) {
            continue;
        }
        const float* box = locations + 4 * index;
        if (!isfinite(box[0]) || !isfinite(box[1]) || !isfinite(box[2]) || !isfinite(box[3])) {
            continue;
        }
        Person person = {.left = clamp(box[1]),
                         .top = clamp(box[0]),
                         .right = clamp(box[3]),
                         .bottom = clamp(box[2]),
                         .confidence = scores[index]};
        if (person.left < person.right && person.top < person.bottom) {
            result->people[result->count++] = person;
        }
    }
    return true;
}
