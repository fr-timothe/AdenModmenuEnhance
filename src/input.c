#include "nn.h"
/* Controller input: we read buttons for the menu and hide them from the game while it is open. */
volatile int g_menu_open;
volatile int g_input_hold;   /* set at close: keep hiding input until the menu sees every button released */
static volatile u64 s_btn; static volatile s32 s_ly, s_lx;
typedef int (*GetFn)(NpadState*, int, const u32*);
#define HOOK(name) static GetFn o_##name; static int h_##name(NpadState* st,int c,const u32* id){ int r=o_##name(st,c,id); return filt(st,r); }
void aden_icons_tick(void);
static int filt(NpadState* st, int r){
    aden_icons_tick();   /* icon loading runs on the thread that reads the controllers, once per frame */
    if(r>0){
        __atomic_or_fetch(&s_btn, st[0].buttons, __ATOMIC_RELAXED);
        if(st[0].ly>8000||st[0].ly<-8000) s_ly=st[0].ly;
        if(st[0].lx>8000||st[0].lx<-8000) s_lx=st[0].lx;
        if(g_menu_open||g_input_hold) for(int i=0;i<r;i++){ st[i].buttons=0; st[i].lx=st[i].ly=st[i].rx=st[i].ry=0; }
    }
    return r;
}
HOOK(fk) HOOK(hh) HOOK(jd) HOOK(jl) HOOK(jr)
/* returns buttons held since the last call (polled once per frame by the menu) */
u64 aden_input_take(void){
    u64 b=__atomic_exchange_n(&s_btn,0,__ATOMIC_RELAXED);
    s32 ly=s_ly, lx=s_lx; s_ly=0; s_lx=0;
    if(ly> 8000) b|=1u<<17; if(ly<-8000) b|=1u<<19;   /* stick up/down */
    if(lx> 8000) b|=1u<<18; if(lx<-8000) b|=1u<<16;   /* stick right/left */
    return b;
}
void aden_input_install(void){
    aden_hook((uptr)&nn_hid_GetNpadStates_FullKey ,(void*)h_fk,(void**)&o_fk);
    aden_hook((uptr)&nn_hid_GetNpadStates_Handheld,(void*)h_hh,(void**)&o_hh);
    aden_hook((uptr)&nn_hid_GetNpadStates_JoyDual ,(void*)h_jd,(void**)&o_jd);
    aden_hook((uptr)&nn_hid_GetNpadStates_JoyLeft ,(void*)h_jl,(void**)&o_jl);
    aden_hook((uptr)&nn_hid_GetNpadStates_JoyRight,(void*)h_jr,(void**)&o_jr);
}
