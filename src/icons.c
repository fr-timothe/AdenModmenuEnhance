#include "nn.h"
/* Item icons for the Items grid (ACNH 3.0.3 only; research notes kept outside the repo).
   The game loads each icon as its own archive through "icon slots" (0x1a8 bytes): request with an
   item and an icon kind, step the slot once per frame until the archive is loaded, then read its
   agl::TextureData. We own ICON_N slots and drive them from the controller hook (main thread);
   the render hook only draws ready icons with nvnCommandBufferDrawTexture.
   Lazy: only the tiles on screen are requested (at most REQ_PER_TICK new ones per frame), and
   icons that left the screen are released when their slot is needed again.
   Every game address is checked against the 3.0.3 code before anything runs; on any mismatch,
   or after a session that crashed while drawing icons, the grid keeps its coloured disks. */
#define ICON_N        40
#define SLOT_SIZE     0x1a8
#define REQ_PER_TICK  4
#define LOAD_TIMEOUT  600      /* ticks (frames) before a load counts as failed */
#define KEEP_TICKS    8        /* an icon off screen this long may be released (GPU frames in flight) */
#define TRY_FILE  "adensd:/aden/icons_trying"   /* exists while icons start (first 300 frames) and first draw */
#define OFF_FILE  "adensd:/aden/icons_off"      /* icons turned off (by the user or after a crash) */
#define MODE_FILE "adensd:/aden/icon_mode"      /* draw variant 0-3 picked in the About tab */

/* main+offset in 3.0.3 */
enum { A_REQ=0x3312d40, A_CTOR=0x3363544, A_STEP=0x3363948, A_FINISH=0x717520, A_GETTEX=0x3363bdc,
       A_TDREL=0x37fd0e0, A_SMP_CTOR=0x380cee0, A_SMP_UPD=0x380d37c,
       D_DRAWTEX=0x4b62bb8, D_GETHANDLE=0x4b61cf0, D_GFX=0x59e77d8, D_EMPTYSTR=0x4b4b980, D_STR64_VT=0x4ab2bc0,
       };
static const struct { u32 off, w0, w1; } CHECK[]={
    {A_REQ,0xd106c3ff,0xa9157bfd},{A_CTOR,0xd10143ff,0xa9017bfd},{A_STEP,0xa9be7bfd,0xa9014ff4},
    {A_FINISH,0xa9bc7bfd,0xf9000bf7},{A_GETTEX,0xa9be7bfd,0xa9014ff4},{A_TDREL,0xb9400008,0x3100051f},
    {A_SMP_CTOR,0xa9bc7bfd,0xf9000bf7},{A_SMP_UPD,0xd101c3ff,0xa9037bfd} };
#define AT(o) (g_main_base+(o))

typedef struct { volatile u32 key; volatile int st; volatile u64 handle; u32 seen, ticks; u16 tw, th; u8 td[0xa0] __attribute__((aligned(16))); } Icon;
enum { I_FREE, I_LOAD, I_READY, I_FAIL };
static Icon s_ic[ICON_N];
static u8 s_slot[ICON_N][SLOT_SIZE] __attribute__((aligned(16)));
static u8 s_sampler[0x100] __attribute__((aligned(16)));
static int s_state;            /* 0 not started, 1 running, -1 this game version is not supported */
static int s_off=-1;           /* user switch (OFF_FILE); -1 not read yet */
static int s_reqlog;
static int s_sampler_id; static u32 s_tick;
static volatile u32 s_want[ICON_N]; static volatile int s_nwant; static volatile u32 s_want_frame;
extern volatile u32 g_render_frame;
static u32 s_last_frame;
static u32 s_draws; static int s_trying;
static int s_mode=-1;
static int s_logged;           /* first few icons are logged step by step */

/* A tiny sead::Heap of our own for the slots' state tables: the constructor only calls
   tryAlloc (vtable +0x30). Bump allocation from a static pool, never freed (the slots live
   for the whole session). Any other heap call is logged and fails. */
#define POOL_SIZE 0x4000
static u8 s_pool[POOL_SIZE] __attribute__((aligned(16))); static u32 s_pool_used;
static void* heap_alloc(void* self,u64 size,int align){
    (void)self; if(align<8) align=8;
    u32 at=(s_pool_used+align-1)&~(u32)(align-1);
    if(at+size>POOL_SIZE){ aden_log("icons: heap pool full (%lu bytes asked)", size); return 0; }
    s_pool_used=at+(u32)size; return s_pool+at;
}
static u64 heap_other(void){ aden_log("icons: unexpected heap call"); return 0; }
static void* s_heap_vt[32]; static void* s_heap[0x40];
static void* own_heap(void){
    for(int i=0;i<32;i++) s_heap_vt[i]=(void*)heap_other;
    s_heap_vt[6]=(void*)heap_alloc;
    s_heap[0]=s_heap_vt;
    return s_heap;
}
static int file_exists(const char* p){ FsFile f; if(nn_fs_OpenFile(&f,p,FS_READ)) return 0; nn_fs_CloseFile(f); return 1; }
static u64 tls(void){ u64 v; __asm__ volatile("mrs %0, tpidrro_el0":"=r"(v)); return v; }
/* each first call into the game is logged and written to the SD card before it runs,
   so a log that ends here names the call the game stopped in (beta.10 crashed with no clue) */
static u32 s_staged;
static void stage(int bit,const char* what){
    if(s_staged&(1u<<bit)) return; s_staged|=1u<<bit;
    aden_log("icons: %s (thread %lx)", what, tls()); aden_log_flush();
}

/* the icon kind the game uses for an item: furniture art, clothing, recipe card, or the pocket icon */
static u32 kind_of(u16 id,char fam){ return id==0x16A2 ? 11 : fam=='F' ? 1 : fam=='C' ? 10 : 0; }
/* an icon is keyed by item id (bits 0-15), kind (16-23) and remake variant (24-31): the variant
   goes in the requested item's count field, as for an item in the pockets */
static u32 key_of(u16 id,char fam,u16 var){ return id|kind_of(id,fam)<<16|(u32)(var&0xFF)<<24; }
#define KIND(k) ((k)>>16&0xFF)

/* the user switch, read once: a session that stopped during its first icon draws turns icons off */
static int icons_off(void){
    if(s_off<0 && aden_sd_ready()){
        if(file_exists(TRY_FILE)){
            nn_fs_DeleteFile(TRY_FILE); nn_fs_CreateFile(OFF_FILE,0);
            aden_log("icons: the last session stopped while loading or drawing icons - icons turned off (About tab turns them on)");
        }
        s_off=file_exists(OFF_FILE);
        if(s_off) aden_log("icons: off (sdmc/aden/icons_off)");
    }
    return s_off!=0;
}
static int start(void){
    nn_fs_CreateFile(TRY_FILE,0);   /* removed after 300 frames without a crash */
    stage(0,"start: checking game code");
    for(unsigned i=0;i<sizeof CHECK/sizeof CHECK[0];i++){
        const u32* w=(const u32*)AT(CHECK[i].off);
        if(w[0]!=CHECK[i].w0 || w[1]!=CHECK[i].w1){ aden_log("icons: game code differs at main+%x - icons off (3.0.3 only)", CHECK[i].off); return -1; }
    }
    if(!*(u64*)AT(D_DRAWTEX) || !*(u64*)AT(D_GETHANDLE) || !*(u64*)AT(D_GFX)){ aden_log("icons: graphics functions not loaded - icons off"); return -1; }
    /* one sampler for every icon */
    stage(1,"sampler: constructor");
    ((void(*)(void*))AT(A_SMP_CTOR))(s_sampler);
    stage(2,"sampler: descriptor");
    ((void(*)(void*,int))AT(A_SMP_UPD))(s_sampler,0x3e);
    /* a slot allocates its state table from the heap it is given, else from the calling thread's
       current sead heap. The controller thread has none (beta.11 stopped in the first constructor,
       which then writes through a null table), the root heap read as 0 (beta.12), and scene heaps
       die on scene changes: give the slots a heap of our own. */
    void* heap=own_heap();
    stage(3,"slots: constructor");
    s_sampler_id=*(s16*)(s_sampler+0xda);
    for(int i=0;i<ICON_N;i++){
        u8* s=s_slot[i]; memset(s,0,SLOT_SIZE);
        ((void(*)(void*,void*))AT(A_CTOR))(s,heap);
        if(!*(u64*)(s+0x128)){ aden_log("icons: slot %d got no state table - icons off", i); return -1; }
        /* +0x148 FixedSafeString<64>, set up by the slot's owner (as the game's UI scenes do) */
        *(u64*)(s+0x148)=*(u64*)AT(D_STR64_VT)+0x10; *(u64*)(s+0x150)=(u64)(s+0x15c); *(u32*)(s+0x158)=0x40; s[0x15c]=0;
        *(u32*)(s+0x1a0)=0;
        s_ic[i].st=I_FREE; s_ic[i].key=0;
    }
    aden_log("icons: %u bytes of our heap used", s_pool_used);
    aden_log("icons: ready, %d slots, sampler %d, main thread %lx", ICON_N, s_sampler_id, tls());
    return 1;
}
static int slot_idle(int i){ u8* s=s_slot[i]; return *(u32*)(s+0x1a0)==0 && !(s[0x18]&2); }
/* mirror of the game's slot release: the archive is unloaded two steps later */
static void slot_release(int i){
    u8* s=s_slot[i];
    if(s_ic[i].st==I_READY) ((void(*)(void*))AT(A_TDREL))(s_ic[i].td+0x50);
    **(char**)(s+0x60)=0; s[0xf0]=0;
    if(*(int*)(s+0x100)!=1 && (s[0x18]&2) && *(int*)(s+0x140)==-1) *(int*)(s+0x140)=1;
    *(u32*)(s+0x1a0)=0;
    s_ic[i].st=I_FREE; s_ic[i].key=0; s_ic[i].handle=0;
}
static void request(int i,u32 key){
    u8* s=s_slot[i]; u32 item[2]={key&0xFFFF,key>>24};
    stage(4,"slot: first request");
    s_ic[i].key=key; s_ic[i].ticks=0; s_ic[i].seen=s_tick;
    ((void(*)(void*,void*,const void*,u32))AT(A_REQ))(s,(void*)AT(D_EMPTYSTR),item,KIND(key));
    s_ic[i].st=I_LOAD;
    if(s_reqlog++<4 || key>>24) aden_log("icons: request %04X kind %u variant %u in slot %d", key&0xFFFF, KIND(key), key>>24, i);
}
static void ready(int i){
    u8* s=s_slot[i]; u8* td=s_ic[i].td;
    memset(td,0,sizeof s_ic[i].td); *(int*)(td+0x50)=-1; *(const char**)(td+0x90)="agl::TextureData"; *(u32*)(td+0x98)=0x10000;
    stage(7,"slot: first texture read");
    if(!((u8(*)(void*,void*,void*))AT(A_GETTEX))(s,s+0x148,td) || *(int*)(td+0x50)<0){
        s_ic[i].st=I_FAIL; aden_log("icons: %04X loaded but has no texture", s_ic[i].key&0xFFFF); return; }
    void* dev=*(void**)(*(u64*)AT(D_GFX)+0x30);
    stage(8,"first texture handle");
    u64 h=((u64(*)(void*,int,int))*(u64*)AT(D_GETHANDLE))(dev,*(int*)(td+0x50),s_sampler_id);
    /* the size matters: clothing icons are bigger than 128x128, and the draw reads texels */
    s_ic[i].tw=*(u16*)(td+0x30); s_ic[i].th=*(u16*)(td+0x32);
    static u32 s_sizes_seen;
    if(s_ic[i].tw!=128 && !(s_sizes_seen&(1u<<(KIND(s_ic[i].key)&31)))){ s_sizes_seen|=1u<<(KIND(s_ic[i].key)&31);
        aden_log("icons: kind %u is %ux%u (%04X)", KIND(s_ic[i].key), s_ic[i].tw, s_ic[i].th, s_ic[i].key&0xFFFF); }
    s_ic[i].handle=h; __atomic_store_n(&s_ic[i].st,I_READY,__ATOMIC_RELEASE);
    if(s_logged<4){ s_logged++; aden_log("icons: %04X ready after %u frames: %ux%u, texture %d, handle %lx",
        s_ic[i].key&0xFFFF, s_ic[i].ticks, *(u16*)(td+0x30), *(u16*)(td+0x32), *(int*)(td+0x50), h); }
}
/* controller hook, once per rendered frame */
HIDDEN void aden_icons_tick(void){
    u32 f=g_render_frame; if(f==s_last_frame || s_state<0) return; s_last_frame=f;
    int nw= f-s_want_frame<30 && !icons_off() ? s_nwant : 0;   /* the menu stopped asking: nothing is on screen */
    if(!s_state){ if(!nw) return; int r=start(); if(!r) return; s_state=r; if(r<0) return; }
    s_tick++;
    if(s_tick==300 && !s_draws){ nn_fs_DeleteFile(TRY_FILE); aden_log("icons: 300 frames of loading, no crash"); }
    for(int i=0;i<ICON_N;i++){
        if(s_ic[i].st==I_FREE && slot_idle(i)) continue;
        u8* s=s_slot[i];
        stage(5,"slot: first step");
        ((void(*)(void*))AT(A_STEP))(s);
        if(s_ic[i].st==I_LOAD){
            s_ic[i].ticks++;
            if(*(u32*)(s+0x1a0)==1 && (s[0x18]&2) && *(int*)(s+0x100)!=1 && (stage(6,"slot: first load check"),((u8(*)(void*))AT(A_FINISH))(s))){ *(u32*)(s+0x1a0)=2; ready(i); }
            else if(s_ic[i].ticks>LOAD_TIMEOUT){ s_ic[i].st=I_FAIL; aden_log("icons: %04X kind %u variant %u did not load", s_ic[i].key&0xFFFF, KIND(s_ic[i].key), s_ic[i].key>>24); }
        }
        /* a failed load that completes late still holds its archive: let it go */
        if(s_ic[i].st==I_FAIL && *(u32*)(s+0x1a0)==1 && (s[0x18]&2) && *(int*)(s+0x100)!=1 && ((u8(*)(void*))AT(A_FINISH))(s)) *(u32*)(s+0x1a0)=2;
    }
    /* menu closed: give the archives and textures back, one per frame (never one still loading);
       the slots are stepped until the game has unloaded them, then left alone */
    extern volatile int g_menu_open;
    if(!nw && !g_menu_open) for(int i=0;i<ICON_N;i++) if(s_ic[i].st==I_READY || s_ic[i].st==I_FAIL){ slot_release(i); break; }
    int asked=0;
    for(int w=0;w<nw;w++){
        u32 key=s_want[w]; if(!key) continue;
        int at=-1; for(int i=0;i<ICON_N;i++) if(s_ic[i].key==key && s_ic[i].st!=I_FREE){ at=i; break; }
        if(at>=0){ s_ic[at].seen=s_tick; continue; }
        if(asked>=REQ_PER_TICK) continue;
        /* a free slot, else the icon unseen the longest (never one still loading) */
        int pick=-1; u32 old=0;
        for(int i=0;i<ICON_N && pick<0;i++) if(s_ic[i].st==I_FREE && slot_idle(i)) pick=i;
        if(pick<0) for(int i=0;i<ICON_N;i++){
            if(s_ic[i].st==I_LOAD || s_ic[i].st==I_FREE) continue;
            u32 age=s_tick-s_ic[i].seen;
            if(age>=KEEP_TICKS && age>old){ old=age; pick=i; }
        }
        if(pick<0) break;
        if(s_ic[pick].st!=I_FREE){ slot_release(pick); continue; }   /* reusable a few frames later */
        request(pick,key); asked++;
    }
}

/* ---- render side ---- */
/* vars: remake variant of each icon, or 0 for none */
HIDDEN void aden_icons_want(int n,const u16* ids,const char* fams,const u16* vars){
    if(n>ICON_N) n=ICON_N;
    for(int i=0;i<n;i++) s_want[i]=key_of(ids[i],fams[i],vars?vars[i]:0);
    s_nwant=n; s_want_frame=g_render_frame;
}
HIDDEN int aden_icons_on(void){ return s_state>=0 && !icons_off(); }
HIDDEN int aden_icon_mode(void){
    if(s_mode<0){ s_mode=3;   /* Eden reads the source region in texels; upper-left origin is the likely one */ FsFile f; if(aden_sd_ready() && !nn_fs_OpenFile(&f,MODE_FILE,FS_READ)){ char c=0; nn_fs_ReadFile(f,0,&c,1); nn_fs_CloseFile(f); if(c>='0'&&c<='3') s_mode=c-'0'; } }
    return s_mode;
}
HIDDEN void aden_icon_mode_set(int m){
    s_mode=m&3; char c='0'+s_mode; FsFile f;
    nn_fs_DeleteFile(MODE_FILE); nn_fs_CreateFile(MODE_FILE,1);
    if(!nn_fs_OpenFile(&f,MODE_FILE,FS_WRITE)){ FsWriteOption o={1}; nn_fs_WriteFile(f,0,&c,1,&o); nn_fs_CloseFile(f); }
    aden_log("icons: draw variant %d", s_mode);
}
/* icons back on after the user asks (About tab) */
HIDDEN void aden_icons_enable(void){ nn_fs_DeleteFile(OFF_FILE); nn_fs_DeleteFile(TRY_FILE); s_off=0; aden_log("icons: turned on"); }
HIDDEN void aden_icons_disable(void){ nn_fs_CreateFile(OFF_FILE,0); s_off=1; aden_log("icons: turned off"); }
int gfx_icon(void* sampler,const void* td,float x,float y,float w,float h);   /* gfx.c */
/* 1 when the icon of id/fam/var is loaded and icons are on */
HIDDEN int aden_icon_ready(u16 id,char fam,u16 var){
    if(s_state<=0 || s_off) return 0;
    u32 key=key_of(id,fam,var);
    for(int i=0;i<ICON_N;i++) if(s_ic[i].key==key && __atomic_load_n(&s_ic[i].st,__ATOMIC_ACQUIRE)==I_READY) return 1;
    return 0;
}
/* draws the icon of id/fam/var in the box (menu space, top-left corner, y up); 0 if it is not ready */
HIDDEN int aden_icon_draw(u16 id,char fam,u16 var,float x,float y,float w,float h,int mode){
    if(s_state<=0 || s_off) return 0;
    u32 key=key_of(id,fam,var);
    for(int i=0;i<ICON_N;i++) if(s_ic[i].key==key && __atomic_load_n(&s_ic[i].st,__ATOMIC_ACQUIRE)==I_READY){
        /* the first draws of a session are guarded: a crash leaves the marker and turns icons off */
        if(!s_draws){ nn_fs_CreateFile(TRY_FILE,0); s_trying=1; stage(9,"first draw"); }
        (void)mode;
        if(!gfx_icon(s_sampler,s_ic[i].td,x,y,w,h)) return 0;
        if(s_trying && ++s_draws>300){ nn_fs_DeleteFile(TRY_FILE); s_trying=0; aden_log("icons: 300 draws done, no crash"); }
        else if(!s_trying) s_draws++;
        return 1;
    }
    return 0;
}
