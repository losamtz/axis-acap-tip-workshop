#ifndef ZONE_H
#define ZONE_H
#include <glib.h>
typedef struct { double x,y,w,h; } Rect;
typedef struct { gboolean enabled, overlap; Rect zone; guint dwell; } Config;
typedef struct { Rect object; gboolean inside, alarm; gint64 entered; guint entries; } State;
gboolean config_apply(Config* c,const char* name,const char* value);
gboolean zone_contains(Rect zone,Rect object,gboolean overlap);
void zone_step(State* s,const Config* c,double elapsed,gint64 now);
#endif
