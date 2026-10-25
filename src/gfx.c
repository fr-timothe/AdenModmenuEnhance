#include "sigs.h"
#include "aden.h"
/* On-screen text using the game's own sead::TextWriter + debug font, driven from
   a hook on DbgTextWriter::draw (main+0x3b1bcd0), which the game calls per render layer. */
#define OFF(x) (g_main_base+(x))
typedef struct {
    uptr vtable;      /* sead::TextWriter vtable */
    uptr drawctx;     /* drawInfo+0x40 */
    uptr proj, cam;
    uptr font;
    float cx, cy;     /* cursor */
    float sx, sy;     /* scale */
    float r, g, b, a; /* color */
    u64   _48;
    void* rotbuf;
    u32   _58, _5c;
    void* fmtbuf; s32 fmtsize; u8 ended; u8 _pad[3];
    uptr  ctx2;       /* drawInfo+0x50 */
} TextWriter;

static void (*f_setup)(uptr);
static void (*f_u8to16)(u16*, int, const char*, int);
static void (*f_print)(TextWriter*, const u16*, int, int, void*);

static uptr cur_info;
static int  cur_ok;

int g_draw_layer = 30;            /* chosen UI layer, -1 = test mode */

static u32 s_prints;  /* print calls this frame */
void gfx_text(float x, float y, float scale, u32 rgba, const char* s){
    if(s_prints>=16) return;   /* hard cap: the debug font has a small per-frame uniform ring */
    s_prints++;
    if(!cur_ok) return;
    uptr info=cur_info;
    uptr* fontA=*(uptr**)SIGA(FONT_A); uptr* fontB=*(uptr**)SIGA(FONT_B);
    TextWriter tw; memset(&tw,0,sizeof tw);
    tw.vtable=*(uptr*)SIGA(TW_VT)+0x10;
    tw.drawctx=*(uptr*)(info+0x40);
    tw.font=(fontA&&*fontA)?*fontA:(fontB?*fontB:0);
    if(!tw.font) return;
    tw.cx=x; tw.cy=y; tw.sx=scale; tw.sy=scale;
    tw.r=((rgba>>24)&255)/255.f; tw.g=((rgba>>16)&255)/255.f; tw.b=((rgba>>8)&255)/255.f; tw.a=(rgba&255)/255.f;
    tw._58=0x7f800000; tw.ended=1;
    { float ls=4.0f*scale; u32 lb; memcpy(&lb,&ls,4); tw._48=(u64)lb<<32; }
    tw.ctx2=*(uptr*)(info+0x50);
    static u16 w[2048];
    f_setup(SIGA(TW_SETUP_ARG));
    f_u8to16(w,2048,s,2047);
    f_print(&tw,w,-1,1,0);
}

void aden_menu_draw(void);   /* menu.c */

/* ---- filled shapes via sead::PrimitiveDrawMgrNvn (singleton at main+0x59e74c0) ----
   vtable: [2] begin(mgr, ctx, view34*, proj44*)  [4] drawQuad(mgr, ctx, model34*, c0*, c1*)
           [13] drawDisk32(mgr, ctx, model34*, centre*, edge*). Meshes are unit size. */
typedef struct { float m[12]; } Mtx34;
typedef struct { float m[16]; } Mtx44;
typedef struct { float r,g,b,a; } Col;
static int s_prim_ok;
static uptr prim_ctx(void){ return *(uptr*)(cur_info+0x50); }
static Col col(u32 rgba){ Col c={((rgba>>24)&255)/255.f,((rgba>>16)&255)/255.f,((rgba>>8)&255)/255.f,(rgba&255)/255.f}; return c; }
void gfx_prim_begin(void){
    s_prim_ok=0;
    if(!cur_ok) return;
    u8* mgr=*(u8**)SIGA(PRIM_MGR); if(!mgr) return;
    /* we are the only user of the debug primitive drawer: start a fresh frame budget */
    *(u32*)(mgr+0x764)=*(u32*)(mgr+0x75c); mgr[0x758]=0;
    static const Mtx34 view={{1,0,0,0, 0,1,0,0, 0,0,1,0}};
    static const Mtx44 proj={{1.f/640,0,0,0, 0,1.f/360,0,0, 0,0,0,0.5f, 0,0,0,1}};
    void (*begin)(void*,uptr,const Mtx34*,const Mtx44*)=(*(void***)mgr)[2];
    begin(mgr,prim_ctx(),&view,&proj);
    s_prim_ok=1;
}
static void prim_call(int slot,float cx,float cy,float sx,float sy,u32 rgba){
    if(!s_prim_ok) return;
    u8* mgr=*(u8**)SIGA(PRIM_MGR);
    Mtx34 m={{sx,0,0,cx, 0,sy,0,cy, 0,0,1,0}}; Col c=col(rgba);
    void (*fn)(void*,uptr,const Mtx34*,const Col*,const Col*)=(*(void***)mgr)[slot];
    fn(mgr,prim_ctx(),&m,&c,&c);
}
/* rectangle by top-left corner (y up) */
void gfx_rect(float x,float y,float w,float h,u32 rgba){ prim_call(4,x+w*0.5f,y-h*0.5f,w,h,rgba); }
void gfx_disk(float cx,float cy,float r,u32 rgba){ prim_call(13,cx,cy,r*2,r*2,rgba); }
void gfx_rrect(float x,float y,float w,float h,float r,u32 rgba){
    if(r*2>h) r=h*0.5f; if(r*2>w) r=w*0.5f;
    gfx_rect(x+r,y,w-2*r,h,rgba);
    gfx_rect(x,y-r,r,h-2*r,rgba);
    gfx_rect(x+w-r,y-r,r,h-2*r,rgba);
    gfx_disk(x+r,y-r,r,rgba); gfx_disk(x+w-r,y-r,r,rgba);
    gfx_disk(x+r,y-h+r,r,rgba); gfx_disk(x+w-r,y-h+r,r,rgba);
}

/* ---- item icons: nvnCommandBufferDrawTexture on the same command buffer as the primitives ----
   Its region is in render-target pixels, so the menu space (1280x720, origin centre, y up) is
   scaled to the target size read from the viewport. Which way the window origin and the source
   units go is not known yet: mode bit 0 = upper-left origin, bit 1 = source in texels (128). */
static float s_tw=1280, s_th=720; static int s_vp_done;
static void target_size(void){
    if(s_vp_done) return; s_vp_done=1;
    const float* v=*(const float**)(cur_info+0x40);
    if(!v){ aden_log("icons: no viewport, using 1280x720"); return; }
    aden_log("icons: viewport %p: %d %d %d %d %d %d", v, (int)v[0],(int)v[1],(int)v[2],(int)v[3],(int)v[4],(int)v[5]);
    for(int i=0;i<5;i++){ float w=v[i]-(i>=2?v[i-2]:0), h=v[i+1]-(i>=2?v[i-1]:0);
        if(w>=640 && w<=4096 && h>=360 && h<=2304 && w>h){ s_tw=w; s_th=h; break; } }
    aden_log("icons: target %dx%d", (int)s_tw, (int)s_th);
}
void gfx_texture(u64 handle,float x,float y,float w,float h,int mode){
    if(!cur_ok || !handle) return;
    target_size();
    void (*draw)(void*,u64,const float*,const float*)=*(void**)(g_main_base+0x4b62bb8);
    if(!draw) return;
    float sx=s_tw/1280.f, sy=s_th/720.f, px=(x+640)*sx;
    float d[4], top=(mode&1) ? (360-y)*sy : (y+360)*sy, bot=(mode&1) ? top+h*sy : top-h*sy;
    d[0]=px; d[2]=px+w*sx; d[1]= top<bot ? top : bot; d[3]= top<bot ? bot : top;
    float u=(mode&2) ? 128.f : 1.f, s[4]={0,0,u,u};
    void* cmd=*(void**)(prim_ctx()+0xb8);
    draw(cmd,handle,d,s);
    /* Eden runs DrawTexture as a blit that leaves the Vulkan viewport and scissor on the icon's
       box without marking them stale (its BlitColor misses InvalidateState), and it re-sends
       them only when the guest registers change: the rest of the frame, and every later frame,
       was clipped to that box (beta.13: black screen while the menu was open). Writing a
       different viewport/scissor and then the full target marks them changed, so they are sent
       again. NVN pointers from the game's proc table (GOT main+0x4acfda8 / main+0x4acf290). */
    void (*vp)(void*,int,int,int,int)=**(void***)(g_main_base+0x4acfda8);
    void (*sc)(void*,int,int,int,int)=**(void***)(g_main_base+0x4acf290);
    if(vp){ vp(cmd,0,0,1,1); vp(cmd,0,0,(int)s_tw,(int)s_th); }
    if(sc){ sc(cmd,0,0,1,1); sc(cmd,0,0,(int)s_tw,(int)s_th); }
}

volatile u32 g_render_frame;   /* frames drawn: the icon loader steps once per frame */
static void (*o_dbgdraw)(void*, uptr);
/* The game records its render layers on several threads at once, so only the
   thread drawing our chosen layer may touch the shared drawing state. */
static void h_dbgdraw(void* self, uptr info){
    o_dbgdraw(self,info);
    u32 layer=*(u32*)(info+0x28);
    if((int)layer!=g_draw_layer) return;
    g_render_frame++;
    cur_info=info; cur_ok=1; s_prints=0;
    aden_menu_draw();
    cur_ok=0;
    aden_log_flush();
}
void aden_gfx_install(void){
    f_setup =(void*)SIGA(TW_SETUP);
    f_u8to16=(void*)SIGA(U8TO16);
    f_print =(void*)SIGA(TW_PRINT);
    aden_hook(SIGA(DBGDRAW),(void*)h_dbgdraw,(void**)&o_dbgdraw);
}
