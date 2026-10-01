#include "inspection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
gboolean config_apply(Config* c, const char* name, const char* value) {
    const char* dot = strrchr(name, '.'); name = dot ? dot + 1 : name;
    Config n = *c;
    if (!strcmp(name,"Enabled")) {
        if (strcmp(value,"yes") && strcmp(value,"no")) return FALSE;
        n.enabled = !strcmp(value,"yes");
    } else if (!strcmp(name,"Zone")) {
        gchar** parts=g_strsplit(value,",",-1);
        guint64 v[4];
        gboolean valid=g_strv_length(parts)==4;
        for (guint i=0; valid && i<4; i++)
            valid=g_ascii_string_to_unsigned(parts[i],10,0,100,&v[i],NULL);
        g_strfreev(parts);
        if (!valid || v[0]>99 || v[1]>98 || v[2]<1 || v[3]<2 || v[2]>100-v[0] || v[3]>100-v[1]) return FALSE;
        n.x=v[0]; n.y=v[1]; n.w=v[2]; n.h=v[3];
    } else {
        guint64 v;
        guint max = !strcmp(name,"PixelDelta") ? 255 : !strcmp(name,"OccupiedPercent") ? 100 : 3600;
        if (!g_ascii_string_to_unsigned(value,10,1,max,&v,NULL)) return FALSE;
        if (!strcmp(name,"PixelDelta")) n.delta=v;
        else if (!strcmp(name,"OccupiedPercent")) n.percent=v;
        else if (!strcmp(name,"DwellSeconds")) n.dwell=v;
        else return FALSE;
    }
    n.generation++; *c=n; return TRUE;
}
gboolean sample_luma(const guint8* data, gsize size, guint width, guint height, guint pitch, guint8* out) {
    if (!data || !out || !width || !height || pitch<width || height>4096 || width>4096 ||
        (gsize)(height-1)*pitch+width>size) return FALSE;
    for (guint y=0;y<HEIGHT;y++) for (guint x=0;x<WIDTH;x++)
        out[y*WIDTH+x]=data[(gsize)((guint64)y*height/HEIGHT)*pitch+(guint64)x*width/WIDTH];
    return TRUE;
}
void inspection_reset(Inspection* s) { memset(s,0,sizeof(*s)); }
void inspection_calibrate(Inspection* s) {
    inspection_reset(s); s->calibrating=TRUE;
}
void inspection_process(Inspection* s,const Config* c,gint64 now) {
    if (s->calibrating) {
        for (guint i=0;i<PIXELS;i++) s->sums[i]+=s->image[i];
        if (++s->calibration==CALIBRATION_FRAMES) {
            for (guint i=0;i<PIXELS;i++) s->reference[i]=s->sums[i]/CALIBRATION_FRAMES;
            s->calibrating=FALSE; s->calibrated=TRUE;
        }
        return;
    }
    if (!s->calibrated || !c->enabled) return;
    guint changed=0,total=0;
    guint x0=c->x*WIDTH/100,y0=c->y*HEIGHT/100;
    guint x1=(c->x+c->w)*WIDTH/100,y1=(c->y+c->h)*HEIGHT/100;
    for (guint y=y0;y<y1;y++) for (guint x=x0;x<x1;x++) {
        guint i=y*WIDTH+x; total++;
        if ((guint)abs((int)s->image[i]-s->reference[i])>=c->delta) changed++;
    }
    s->score=total ? 100.0*changed/total : 0;
    gboolean target=s->score >= (s->occupied ? c->percent*0.6 : c->percent);
    if (target!=s->candidate) { s->candidate=target; s->candidate_since=now; }
    if (target!=s->occupied && now-s->candidate_since>=G_USEC_PER_SEC) {
        s->occupied=target; s->occupied_since=target ? now : 0;
    }
}
const char* inspection_state(const Inspection* s,const Config* c,gboolean fresh,gint64 now) {
    if (!fresh) return "Unknown";
    if (!c->enabled) return "Disabled";
    if (s->calibrating) return "Calibrating";
    if (!s->calibrated) return "Needs calibration";
    if (!s->occupied) return "Clear";
    return now-s->occupied_since >= (gint64)c->dwell*G_USEC_PER_SEC ? "Dwell exceeded" : "Occupied";
}
