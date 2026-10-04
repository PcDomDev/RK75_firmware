#define ORB_MOTION_HOST_TEST 1
#include "orb_host_shim.h"
#include "../r75_source/r75_source/features/orb_motion.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { double minx,maxx,miny,maxy,out_board,out_vis,max_dir_per_10u,max_dkappa_per_unit,min_spd,max_spd,occ[8][4],total; } stats_t;

static stats_t run(uint32_t seed, uint8_t speed, int dt, double seconds) {
    stats_t s; memset(&s,0,sizeof s); s.minx=s.miny=1e9; s.maxx=s.maxy=-1e9; s.min_spd=1e9;
    orb_motion_init(seed);
    int frames=(int)(seconds*1000/dt), outb=0,outv=0;
    double prev_dir=0; int have=0; uint32_t pk=0; int32_t kprev=0;
    #define W 20
    double wx[W], wy[W]; int wn=0;
    uint32_t pathprev = 0;
    for (int f=0; f<frames; f++) {
        orb_motion_advance(dt, speed);
        orb_xy_t h=orb_motion_head(); double x=h.x/16.0,y=h.y/16.0;
        if(x<s.minx)s.minx=x; if(x>s.maxx)s.maxx=x; if(y<s.miny)s.miny=y; if(y>s.maxy)s.maxy=y;
        if(x<0||x>224||y<0||y>64) outb++;
        if(x<-10||x>234||y<-10||y>74) outv++;
        int cx=(int)(x/224.0*8), cy=(int)(y/64.0*4); if(cx<0)cx=0; if(cx>7)cx=7; if(cy<0)cy=0; if(cy>3)cy=3; s.occ[cx][cy]++; s.total++;
        /* apparent (display-space) velocity direction from the model's exact heading */
        double th = heading/4294967296.0*2*M_PI; double dir = atan2(sin(th), cos(th));
        double disp_moved = (path_total_q12 - pathprev)/4096.0; pathprev = path_total_q12;
        if (have && disp_moved>0) { double dd=dir-prev_dir; while(dd>M_PI)dd-=2*M_PI; while(dd<-M_PI)dd+=2*M_PI; double per10 = fabs(dd)*180/M_PI*(10.0/disp_moved); if(per10>s.max_dir_per_10u) s.max_dir_per_10u=per10; }
        prev_dir=dir; have=1;
        if (f>0) { double dk=fabs((double)(kappa-kprev))/32768.0; if (disp_moved>0 && dk/disp_moved>s.max_dkappa_per_unit) s.max_dkappa_per_unit=dk/disp_moved; }
        kprev=kappa;
        /* apparent speed via 20-frame chord */
        wx[wn%W]=x; wy[wn%W]=y; wn++;
        if (wn>=W && f%W==0) { double ox=wx[(wn)%W], oy=wy[(wn)%W]; double spd=hypot(x-ox,y-oy)/(W*dt/1000.0); if(spd<s.min_spd)s.min_spd=spd; if(spd>s.max_spd)s.max_spd=spd; }
    }
    s.out_board=(double)outb/frames; s.out_vis=(double)outv/frames; return s;
}

int main(void){
    printf("R_MIN=%d tau=%d noise=%d\n",ORB_R_MIN_UNITS,ORB_KAPPA_TAU_UNITS,ORB_NOISE_AMPLITUDE);
    uint8_t speeds[3]={0,127,255}; const char*names[3]={"min","mid","max"};
    for(int si=0;si<3;si++){
        double wvis=0,wb=0,wdir=0,wdk=0,minx=1e9,maxx=-1e9,miny=1e9,maxy=-1e9,minsp=1e9,maxsp=0;
        double secs = si==0?3600:(si==1?2400:1200);
        for(uint32_t seed=1;seed<=30;seed++){
            stats_t s=run(seed*2654435761u,speeds[si],16,secs);
            if(s.out_vis>wvis)wvis=s.out_vis; if(s.out_board>wb)wb=s.out_board; if(s.max_dir_per_10u>wdir)wdir=s.max_dir_per_10u; if(s.max_dkappa_per_unit>wdk)wdk=s.max_dkappa_per_unit;
            if(s.minx<minx)minx=s.minx; if(s.maxx>maxx)maxx=s.maxx; if(s.miny<miny)miny=s.miny; if(s.maxy>maxy)maxy=s.maxy; if(s.min_spd<minsp)minsp=s.min_spd; if(s.max_spd>maxsp)maxsp=s.max_spd;
        }
        printf("[%s] extent x[%.0f..%.0f] y[%.0f..%.0f] | worst time off-keys %.1f%% | outside vis box %.3f%% | apparent speed %.0f..%.0f u/s | max apparent turn %.1f deg per 10u | max dKappa/unit %.5f\n",
           names[si],minx,maxx,miny,maxy,wb*100,wvis*100,minsp,maxsp,wdir,wdk);
    }
    stats_t s=run(12345,127,16,7200);
    printf("occupancy %% (8 cols x 4 rows), mid speed, 2h:\n");
    for(int r=0;r<4;r++){for(int c=0;c<8;c++)printf("%5.1f ",100*s.occ[c][r]/s.total);printf("\n");}
    return 0;
}
