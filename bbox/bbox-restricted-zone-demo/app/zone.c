#include "zone.h"
#include <math.h>
#include <string.h>
gboolean config_apply(Config* c,const char* name,const char* value) {
    const char* dot=strrchr(name,'.'); name=dot?dot+1:name; Config n=*c;
    if (!strcmp(name,"Enabled")) {
        if (strcmp(value,"yes") && strcmp(value,"no")) return FALSE;
        n.enabled=!strcmp(value,"yes");
    } else if (!strcmp(name,"Rule")) {
        if (strcmp(value,"center") && strcmp(value,"overlap")) return FALSE;
        n.overlap=!strcmp(value,"overlap");
    } else if (!strcmp(name,"DwellSeconds")) {
        guint64 v; if(!g_ascii_string_to_unsigned(value,10,1,60,&v,NULL)) return FALSE; n.dwell=v;
    } else if (!strcmp(name,"Zone")) {
        gchar** parts=g_strsplit(value,",",-1); guint64 v[4]; gboolean valid=g_strv_length(parts)==4;
        for(guint i=0;valid && i<4;i++) valid=g_ascii_string_to_unsigned(parts[i],10,0,100,&v[i],NULL);
        g_strfreev(parts);
        if(!valid || !v[2] || !v[3] || v[0]+v[2]>100 || v[1]+v[3]>100) return FALSE;
        n.zone=(Rect){v[0]/100.0,v[1]/100.0,v[2]/100.0,v[3]/100.0};
    } else return FALSE;
    *c=n; return TRUE;
}
gboolean zone_contains(Rect z,Rect o,gboolean overlap) {
    if(overlap) return o.x<z.x+z.w && o.x+o.w>z.x && o.y<z.y+z.h && o.y+o.h>z.y;
    double x=o.x+o.w/2,y=o.y+o.h/2;
    return x>=z.x && x<=z.x+z.w && y>=z.y && y<=z.y+z.h;
}
void zone_step(State* s,const Config* c,double elapsed,gint64 now) {
    /* 24-second out-and-back trip; independent of callback cadence. */
    double phase=fmod(MAX(elapsed,0),24.0)/12.0;
    s->object=(Rect){0.05+0.78*(phase<=1 ? phase : 2-phase),0.4,0.12,0.2};
    gboolean inside=c->enabled && zone_contains(c->zone,s->object,c->overlap);
    if(inside && !s->inside) { s->entered=now; s->entries++; }
    if(!inside) s->entered=0;
    s->inside=inside;
    s->alarm=inside && now-s->entered>=(gint64)c->dwell*G_USEC_PER_SEC;
}
