#ifndef INSPECTION_H
#define INSPECTION_H
#include <glib.h>
#define WIDTH 160
#define HEIGHT 90
#define PIXELS (WIDTH * HEIGHT)
#define CALIBRATION_FRAMES 10
/* The sampled image and reference belong to the application, never to VDO. */
typedef struct { gboolean enabled; guint x,y,w,h,delta,percent,dwell; guint generation; } Config;
typedef struct {
    guint8 image[PIXELS], reference[PIXELS];
    guint sums[PIXELS], calibration;
    gboolean calibrating, calibrated, occupied, candidate;
    double score;
    gint64 candidate_since, occupied_since;
} Inspection;
gboolean config_apply(Config* c, const char* name, const char* value);
gboolean sample_luma(const guint8* data, gsize size, guint width, guint height, guint pitch, guint8* out);
void inspection_reset(Inspection* s);
void inspection_calibrate(Inspection* s);
void inspection_process(Inspection* s, const Config* c, gint64 now);
const char* inspection_state(const Inspection* s, const Config* c, gboolean fresh, gint64 now);
#endif
