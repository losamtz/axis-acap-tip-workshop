#include "inspection.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static void fill(Inspection* s,guint8 value) { memset(s->image,value,PIXELS); }
int main(void) {
    Config c={0};
    assert(config_apply(&c,"root.Vdo_inspection_zone.Enabled","yes"));
    assert(config_apply(&c,"Zone","0,0,100,100"));
    assert(config_apply(&c,"PixelDelta","25"));
    assert(config_apply(&c,"OccupiedPercent","10"));
    assert(config_apply(&c,"DwellSeconds","2"));
    assert(!config_apply(&c,"Zone","90,0,20,20"));
    assert(!config_apply(&c,"Zone","0,0,0,20"));
    assert(!config_apply(&c,"Zone","0,0,100,100x"));
    assert(!config_apply(&c,"Zone","-1,0,10,10"));
    assert(!config_apply(&c,"PixelDelta","0"));
    assert(!config_apply(&c,"OccupiedPercent","101"));
    guint8 padded[8]={1,2,99,99,3,4,99,99}, out[PIXELS];
    assert(sample_luma(padded,8,2,2,4,out));
    assert(out[0]==1 && out[159]==2 && out[89*160]==3 && out[PIXELS-1]==4);
    assert(!sample_luma(padded,5,2,2,4,out));
    assert(!sample_luma(padded,8,2,2,1,out));
    assert(!sample_luma(NULL,8,2,2,4,out));
    Inspection s={0};
    assert(!strcmp(inspection_state(&s,&c,TRUE,0),"Needs calibration"));
    inspection_calibrate(&s);
    for (guint i=0;i<10;i++) { fill(&s,100+i*2); inspection_process(&s,&c,i*200000); }
    assert(s.calibrated && s.reference[0]==109 && !s.occupied);
    fill(&s,110); inspection_process(&s,&c,2000000); assert(s.score==0);
    memset(s.image,160,PIXELS/5); inspection_process(&s,&c,3000000);
    assert(s.score==20 && !s.occupied);
    inspection_process(&s,&c,4000000); assert(s.occupied);
    assert(!strcmp(inspection_state(&s,&c,TRUE,5000000),"Occupied"));
    assert(!strcmp(inspection_state(&s,&c,TRUE,6000000),"Dwell exceeded"));
    fill(&s,109); memset(s.image,160,PIXELS*8/100); inspection_process(&s,&c,7000000);
    inspection_process(&s,&c,8000000); assert(s.occupied); /* Hysteresis holds at 8%. */
    fill(&s,109); inspection_process(&s,&c,9000000); assert(s.occupied);
    inspection_process(&s,&c,10000000); assert(!s.occupied);
    assert(!strcmp(inspection_state(&s,&c,FALSE,11000000),"Unknown"));
    inspection_reset(&s); assert(!s.calibrated && !s.occupied);
    c.enabled=FALSE; assert(!strcmp(inspection_state(&s,&c,TRUE,0),"Disabled"));
    c.enabled=TRUE; c.x=50;c.w=50;
    inspection_calibrate(&s);
    for(guint i=0;i<10;i++){fill(&s,100);inspection_process(&s,&c,i*200000);}
    for(guint y=0;y<HEIGHT;y++) memset(s.image+y*WIDTH,200,WIDTH/2);
    inspection_process(&s,&c,3000000); assert(s.score==0); /* Outside ROI ignored. */
    puts("PASS: config, stride bounds, calibration averaging, ROI, debounce, hysteresis, dwell and unknown state");
}
