#include "nn.h"
/* Aden Modmenu - core. Built against Animal Crossing: New Horizons 3.0.2
   (main build id FCD2BB238ABE99E9); other versions are handled by code fingerprints (sigs.c). */
#include "sigs.h"
HIDDEN uptr g_main_base, g_main_text_size;
HIDDEN u8 aden_module_runtime[0x200];   /* rtld fills this in */
extern char __module_start[];
extern void nnMain(void);               /* exported by the game's main */

static void (*orig_nnMain)(void);
static void hk_nnMain(void){
    aden_log_attach_sd();
    aden_log("nnMain reached - game is starting, Aden Modmenu is active");
    orig_nnMain();
}

void aden_module_init(void){
    aden_log("Aden Modmenu v%s loaded (module at %p)", ADEN_VERSION, __module_start);
    /* main's code segment = the executable region holding nnMain */
    MemoryInfo mi; u32 pi;
    if(svcQueryMemory(&mi,&pi,(uptr)&nnMain) || !(mi.perm&4)){ aden_log("could not locate game code - mod disabled"); return; }
    g_main_base=mi.addr; g_main_text_size=mi.size;
    aden_log("main base %lx, code size %lx, nnMain at main+%lx", g_main_base, g_main_text_size, (uptr)&nnMain-g_main_base);
    if(aden_hook_init()){ aden_log("hook setup failed - mod disabled"); return; }
    /* nnMain is found by name on every version; hooking it lets the log reach the SD card */
    aden_hook((uptr)&nnMain,(void*)hk_nnMain,(void**)&orig_nnMain);
    int bad=aden_sigs_resolve(g_main_base,g_main_text_size);
    if(bad){
        aden_log("%d hook point(s) missing in this game version - mod disabled, game runs normally.", bad);
        aden_log("Please share this log on the Aden Modmenu GameBanana page.");
        return;
    }
    extern void aden_phone_install(void); aden_phone_install();
    extern void aden_gfx_install(void); aden_gfx_install();
    extern void aden_input_install(void); aden_input_install();
    extern void aden_romfs_install(void); aden_romfs_install();
}
