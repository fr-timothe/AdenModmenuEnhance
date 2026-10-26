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

static void (*f_setup)(uptr,uptr);   /* GraphicsContext::apply(state, draw context) */
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
    f_setup(SIGA(TW_SETUP_ARG),*(uptr*)(info+0x50));
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

/* ---- item icons: agl's "texture" shader program on a unit quad (3.0.3) ----
   nvnCommandBufferDrawTexture was a blit on Eden: no alpha (black icon backgrounds), state the
   emulator lost track of (black screens), and a large frame-rate cost. The game draws its own
   textured quads this way (main+0x38114c0): the alpha-blend GraphicsContext it uses for debug
   text, the sampler bound to the icon's agl::TextureData, the "texture" program (ShaderHolder
   program 5) and the DevTools quad drawer. The quad is a unit square centred on the origin,
   placed with the same projection as the boxes. Research notes are kept outside the repo. */
enum { A_GC_APPLY=0x37e4d90, A_PROG_ACT=0x3807810, A_SMP_APPLY=0x380d060, A_TEXQUAD=0x38466e0,
       D_HOLDER_GOT=0x4abdd18 };
static const struct { u32 off, w[3]; u8 third; } TQ_CHECK[]={   /* third word at +0x08, or +0x18 for look-alike prologues */
    {A_TEXQUAD,{0xd10283ff,0xa9047bfd,0xf9002bfb},0x08}, {A_GC_APPLY,{0xd10203ff,0xa9027bfd,0xa9036ffc},0x08},
    {A_PROG_ACT,{0xa9bd7bfd,0xa90157f6,0x91004016},0x18}, {A_SMP_APPLY,{0xa9bd7bfd,0xa90157f6,0xaa0003f4},0x18},
    {0x38114c0,{0x90009568,0xf9468d08,0},0} };
static int s_tq_ok;   /* 0 not checked, 1 code matches, -1 it does not */
static int tq_check(void){
    if(s_tq_ok) return s_tq_ok;
    s_tq_ok=1;
    for(unsigned i=0;i<sizeof TQ_CHECK/sizeof TQ_CHECK[0];i++){
        const u32* c=(const u32*)(g_main_base+TQ_CHECK[i].off);
        if(c[0]!=TQ_CHECK[i].w[0] || c[1]!=TQ_CHECK[i].w[1] || (TQ_CHECK[i].third && c[TQ_CHECK[i].third/4]!=TQ_CHECK[i].w[2])){
            aden_log("icons: quad drawer code differs at main+%x - icons not drawn", TQ_CHECK[i].off); s_tq_ok=-1; break; }
    }
    if(s_tq_ok>0) aden_log("icons: drawing with the game's texture program");
    return s_tq_ok;
}
/* draws the icon (sampler: agl::TextureSampler, td: its agl::TextureData) in the box given by its
   top-left corner, menu space (1280x720, origin centre, y up); 0 if it could not */
int gfx_icon(void* sampler,const void* td,float x,float y,float w,float h){
    if(!cur_ok || tq_check()<0) return 0;
    uptr ctx=prim_ctx();
    u8** hp=*(u8***)(g_main_base+D_HOLDER_GOT); u8* holder= hp ? *hp : 0;
    if(!holder || *(u32*)(holder+8)<6) return 0;
    void* prog=(*(void***)(holder+0x10))[5];
    if(!prog) return 0;
    ((void(*)(uptr,uptr))(g_main_base+A_GC_APPLY))(SIGA(TW_SETUP_ARG),ctx);   /* straight alpha, no culling, no depth */
    ((void(*)(void*,const void*))(g_main_base+A_SMP_APPLY))(sampler,td);
    ((void(*)(void*,uptr))(g_main_base+A_PROG_ACT))(prog,ctx);
    Mtx34 m={{w,0,0,x+w*0.5f, 0,h,0,y-h*0.5f, 0,0,1,0}};
    static const Mtx44 proj={{1.f/640,0,0,0, 0,1.f/360,0,0, 0,0,0,0.5f, 0,0,0,1}};   /* as gfx_prim_begin */
    static const Col white={1,1,1,1};
    ((void(*)(uptr,void*,void*,const Mtx34*,const Mtx44*,const Col*))(g_main_base+A_TEXQUAD))(ctx,prog,sampler,&m,&proj,&white);
    return 1;
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
