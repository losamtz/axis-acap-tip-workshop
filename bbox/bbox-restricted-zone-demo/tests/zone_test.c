#include "zone.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 Config c={0};assert(config_apply(&c,"root.Bbox_restricted_zone.Enabled","yes"));assert(config_apply(&c,"Zone","35,25,30,50"));assert(config_apply(&c,"Rule","center"));assert(config_apply(&c,"DwellSeconds","3"));
 assert(!config_apply(&c,"Zone","90,20,20,50"));assert(!config_apply(&c,"Zone","0,0,0,10"));assert(!config_apply(&c,"Rule","invalid"));assert(!config_apply(&c,"DwellSeconds","0"));
 Rect z={.x=.5,.y=.25,.w=.25,.h=.5},o={.x=.4,.y=.4,.w=.12,.h=.2};
 assert(zone_contains(z,o,TRUE));assert(!zone_contains(z,o,FALSE));
 o=(Rect){.x=.25,.y=.4,.w=.25,.h=.2};assert(!zone_contains(z,o,TRUE)); /* touching is not positive overlap */
 o=(Rect){.x=.45,.y=.4,.w=.1,.h=.2};assert(zone_contains(z,o,FALSE)); /* center on boundary counts */
 State s={0};zone_step(&s,&c,0,1000000);assert(!s.inside);
 zone_step(&s,&c,6,2000000);assert(s.inside && s.entries==1 && !s.alarm);
 zone_step(&s,&c,6,5000000);assert(s.alarm && s.entries==1);
 zone_step(&s,&c,12,6000000);assert(!s.inside && !s.alarm);
 zone_step(&s,&c,18,7000000);assert(s.inside && s.entries==2);
 c.enabled=FALSE;zone_step(&s,&c,18,8000000);assert(!s.inside && !s.alarm);
 zone_step(&s,&c,24,9000000);assert(s.object.x>=.05 && s.object.x<.051);
 puts("PASS: config, center/overlap boundaries, dwell, exit, reentry, disable and repeat cycle");
}
