#include "nn.h"
/* Player data (ACNH 3.0.2). Layout found by memory analysis:
     P-0xB8 : pocket 2, 20 slots (only the first size-20 are unlocked by the bag upgrades)
     P      : pocket 1, 20 slots of { u16 id; u8 sys; u8 add; u32 param }  (empty id = 0xFFFE)
     P+0xA0 : u32 pocket size (20, 30 or 40), then 5 x 0xFFFFFFFF
              slots 0-19 are at P, slots 20..size-1 at P-0xB8
     P+0xB8 : wallet Bells as EncryptedInt32 { u32 enc; u16 adjust; u8 shift; u8 checksum }
   The game keeps several copies (live + save buffers); we update all of them.
   Copies are kept in address order, so the cap must stay well above what the game makes:
   a dropped copy may be the live one. */
#define MAXCOPIES 16
static uptr s_copy[MAXCOPIES]; static int s_ncopies;
#define PERS_P      0x37CA8   /* pocket anchor offset inside the personal save */
#define PERS_ID_PID (0xC248+0x1C)
#define MAXCAND 48
#define MAXPLAYERS 8
#define DEV_MIN     0x74A40   /* PERS_SIZE: smallest DeviceShared region the second pass scans */
/* memory the bulk scans read first: not Locked (0x1) nor DeviceShared (0x4, GPU-mapped). IpcLocked (0x2) is fine.
   Avoid GPU-mapped pages when possible: on Eden one CPU read makes reactive flushing copy
   that buffer back from the GPU every frame for the rest of the session. */
#define MI_PLAIN(mi) (((mi).attr&5)==0)
/* second pass, only when the first finds nothing: DeviceShared heap that is not Locked.
   On Eden 3.0.3 the save data sits there (beta.3 log: 505 MB skipped, no player found).
   Regions smaller than a personal save cannot hold one. */
#define MI_DEV(mi)   (((mi).attr&5)==4)
static int scan_ok(const MemoryInfo* mi, int dev){
    return (mi->perm&3)==3 && mi->type==5 && (dev ? MI_DEV(*mi) && mi->size>=DEV_MIN : MI_PLAIN(*mi) && mi->size>=0x1000);
}
static u64 s_skip_mb[8]; static int s_skip_n[8];   /* rw heap the player scan skipped, by attr&7 (logged per search) */
/* targeted reads (a few bytes at a known address) only need the page not to be Locked, as in v0.5.3:
   the save buffers themselves can sit in IPC/device-attributed pages, and MI_PLAIN there hid every player */
#define MI_RW(mi)    (((mi).attr&1)==0)
static int s_attr_rej; static u32 s_attr_last;   /* base_ok rejects caused by attributes (logged per search) */
/* searches share the CPU with the game: nap at every SCAN_CHUNK boundary (~1k naps per 4 GB: few, since
   Eden can round a guest sleep up to 1-15 ms), then re-check the region, since the game may remap memory while we sleep */
#define SCAN_CHUNK  0x400000
#define SCAN_NAP    1000000    /* ns */
#define SEARCH_PRIO 24         /* below the game's default 16, so its threads preempt ours */
static int scan_nap(uptr a, uptr* end, int dev){
    nn_os_SleepThread(SCAN_NAP);
    MemoryInfo mi; u32 pi;
    if(svcQueryMemory(&mi,&pi,a) || (mi.perm&3)!=3 || mi.type!=5 || (dev ? !MI_DEV(mi) : !MI_PLAIN(mi))) return 0;
    if(mi.addr+mi.size<*end) *end=mi.addr+mi.size;   /* region split: stop at its new end, the caller resumes there */
    return 1;
}
typedef struct { u32 pid; int n, drop; uptr P[MAXCOPIES]; char name[24]; } Player;
static Player s_players[MAXPLAYERS]; static int s_nplayers, s_active;

volatile int g_player_state;   /* 0 idle, 1 searching, 2 ready, 3 not found */
volatile int g_island_state;   /* 0 idle, 1 searching, 2 ready, 3 not found */
/* island thread: s_th2_run is 1 from start until the thread's last store; s_island_stale marks
   the result in flight (or held) as obsolete. Only the island thread moves the state off 1. */
static volatile int s_th2_run, s_island_stale;
static void island_drop(void);
static OsThread s_th; static u8 s_stack[0x8000] __attribute__((aligned(0x1000)));

typedef struct { u32 enc; u16 adjust; u8 shift; u8 check; } EncInt;
#define ENC_CONST 0x80E32B11u
static u8 enc_ck(u32 e){ return (u8)((e&255)+((e>>8)&255)+((e>>16)&255)+(e>>24)-0x2D); }
static int enc_valid(const EncInt* v){ return v->enc && v->shift<29 && enc_ck(v->enc)==v->check; }
static u32 enc_get(const EncInt* v){ u32 s=(3+v->shift)&31; u32 r=s?((v->enc>>s)|(v->enc<<(32-s))):v->enc; return r+(ENC_CONST-v->adjust); }
static void enc_set(EncInt* v,u32 val){ u32 s=(3+v->shift)&31; u32 x=val-(ENC_CONST-v->adjust); v->enc=s?((x<<s)|(x>>(32-s))):x; v->check=enc_ck(v->enc); }

static int slot_ok(const u8* p){
    u16 id=*(const u16*)p;
    if(id==0xFFFE) return *(const u16*)(p+2)==0 && *(const u32*)(p+4)==0;
    return id<0xFFFE;   /* newer ids may be missing from our name list */
}
static int sig_ok(uptr P){
    if(*(u32*)(P-0x18)!=0) return 0;
    for(int i=0;i<5;i++) if(*(u32*)(P-0x14+i*4)!=0xFFFFFFFF || *(u32*)(P+0xA4+i*4)!=0xFFFFFFFF) return 0;
    u32 n=*(u32*)(P+0xA0); if(n!=20 && n!=30 && n!=40) return 0;
    for(int i=0;i<20;i++) if(!slot_ok((u8*)(P+i*8))) return 0;
    return enc_valid((EncInt*)(P+0xB8));
}
static int has_stuff(uptr P){
    if(enc_get((EncInt*)(P+0xB8))) return 1;
    for(int i=0;i<20;i++) if(*(u16*)(P+i*8)!=0xFFFE) return 1;
    return 0;
}
/* ---- extra encrypted stats found relative to each player copy P ----
   Each player record repeats 0x74A60 bytes later, so every offset is tried at P and P+0x74A60.
   Nook Miles: P-0x25590 (49,430 in the test save).   Bank: P+0x2D5D4 (300,150).
   Only candidates that are mapped RW, pass the checksum and agree on one value are kept. */
#define STRIDE 0x74A60
#define MAXF (2*MAXCOPIES)   /* two offsets per copy */
typedef struct { const char* name; s64 off; int n; uptr a[MAXF]; } Field;
static Field s_fields[3]={ {"miles",-0x25590,0,{0}}, {"bank",0x2D5D4,0,{0}}, {"poki",0x3C608,0,{0}} };
static int addr_rw(uptr a){
    MemoryInfo mi; u32 pi;
    if(svcQueryMemory(&mi,&pi,a)) return 0;
    return (mi.perm&3)==3 && MI_RW(mi) && mi.type!=0 && a+8<=mi.addr+mi.size;
}
static void find_fields(void){
    for(int f=0;f<3;f++){
        Field* F=&s_fields[f]; uptr cand[MAXF]; u32 val[MAXF]; int nc=0;
        for(int i=0;i<s_ncopies;i++) for(int k=0;k<2;k++){
            uptr a=s_copy[i]+F->off+(k?STRIDE:0);
            if(nc>=MAXF || !addr_rw(a) || !enc_valid((EncInt*)a)) continue;
            if(s_nplayers){ uptr pidp=s_copy[i]+(k?STRIDE:0)-PERS_P+PERS_ID_PID;
                if(!addr_rw(pidp) || *(u32*)pidp!=s_players[s_active].pid) continue; }
            int dup=0; for(int j=0;j<nc;j++) if(cand[j]==a) dup=1; if(dup) continue;
            cand[nc]=a; val[nc]=enc_get((EncInt*)a); nc++;
        }
        /* keep the value most candidates agree on */
        int best=-1,bc=0;
        for(int i=0;i<nc;i++){ int c=0; for(int j=0;j<nc;j++) if(val[j]==val[i]) c++; if(c>bc){bc=c;best=i;} }
        F->n=0;
        if(best>=0 && val[best]<=999999999u) for(int i=0;i<nc;i++) if(val[i]==val[best]) F->a[F->n++]=cand[i];
        aden_log("%s: %d locations, value %u", F->name, F->n, F->n?val[best]:0);
    }
}
static int field_live(int f){
    Field* F=&s_fields[f]; int k=0;
    for(int i=0;i<F->n;i++) if(addr_rw(F->a[i]) && enc_valid((EncInt*)F->a[i])) F->a[k++]=F->a[i];
    F->n=k; return k;
}
int aden_stat_ok(int f){ return g_player_state==2 && f>=0 && f<3 && s_fields[f].n>0; }
/* cached addresses are re-checked before every read: memory can move after loading screens */
u32 aden_stat_get(int f){ return aden_stat_ok(f) && addr_rw(s_fields[f].a[0]) ? enc_get((EncInt*)s_fields[f].a[0]) : 0; }
void aden_stat_set(int f,u32 v){
    if(!aden_stat_ok(f) || !field_live(f)) return;
    for(int i=0;i<s_fields[f].n;i++) enc_set((EncInt*)s_fields[f].a[i],v);
    aden_log("%s set to %u in %d places", s_fields[f].name, v, s_fields[f].n);
}
static void find_personal_bases(void);
static void find_fields(void);
/* Every player's save (up to 8 residents) can sit in memory, often more than once
   (live data + save buffers). Copies are grouped by PlayerID; only the selected
   player's copies are ever written. */
static uptr s_cand[MAXCAND]; static int s_ncand;
static int base_ok(uptr P){
    /* the personal save around this pocket must be mapped and hold a valid Nook Miles value */
    uptr base=P-PERS_P; MemoryInfo mi; u32 pi;
    if(base>P) return 0;
    if(svcQueryMemory(&mi,&pi,base) || (mi.perm&3)!=3 || mi.type==0) return 0;
    if(!MI_RW(mi)){ s_attr_rej++; s_attr_last=mi.attr; return 0; }
    if(svcQueryMemory(&mi,&pi,base+0x74A40-8) || (mi.perm&3)!=3 || mi.type==0) return 0;
    if(!MI_RW(mi)){ s_attr_rej++; s_attr_last=mi.attr; return 0; }
    return enc_valid((EncInt*)(base+0x12718));
}
static void utf16_name(char* out,int cap,const u16* s){ int k=0; for(int i=0;i<10 && s[i] && k<cap-1;i++) out[k++]= s[i]<128?(char)s[i]:'?'; out[k]=0; }
static int s_bad;   /* consecutive checks that found a bad copy */
static void use_player(int i){
    s_active=i; s_ncopies=0; s_bad=0;
    for(int k=0;k<s_players[i].n && k<MAXCOPIES;k++) s_copy[s_ncopies++]=s_players[i].P[k];
    find_fields(); find_personal_bases();
}
/* one pass over the heap; dev=0 plain memory, dev=1 DeviceShared memory (see MI_DEV) */
static void scan_players(int dev, int* empty){
    uptr a=0; MemoryInfo mi; u32 pi;
    while(a<0x8000000000ul){
        if(svcQueryMemory(&mi,&pi,a)||mi.size==0) break;
        uptr end=mi.addr+mi.size;
        if(!dev && (mi.perm&3)==3 && mi.type==5 && !MI_PLAIN(mi)){ s_skip_n[mi.attr&7]++; s_skip_mb[mi.attr&7]+=mi.size; }
        if(scan_ok(&mi,dev)){
            for(uptr P=mi.addr+0x20; P+0xC0<end; P+=8){
                if(!(P&(SCAN_CHUNK-1)) && (!scan_nap(P,&end,dev) || P+0xC0>=end)) break;
                if(*(u32*)(P-4)!=0xFFFFFFFF || *(u32*)(P-0x18)!=0) continue;
                if(!sig_ok(P)) continue;
                if(base_ok(P)){
                    u32 pid=*(u32*)(P-PERS_P+PERS_ID_PID);
                    u16 c0=*(u16*)(P-PERS_P+PERS_ID_PID+4);
                    if(!pid || !c0){ (*empty)++; continue; }     /* unused resident slot (no id / no name) */
                    int g=-1; for(int j=0;j<s_nplayers;j++) if(s_players[j].pid==pid) g=j;
                    if(g<0 && s_nplayers<MAXPLAYERS){ g=s_nplayers++; s_players[g].pid=pid; s_players[g].n=0; s_players[g].drop=0;
                        utf16_name(s_players[g].name,sizeof s_players[g].name,(const u16*)(P-PERS_P+PERS_ID_PID+4)); }
                    if(g>=0 && s_players[g].n<MAXCOPIES){ s_players[g].P[s_players[g].n++]=P;
                        aden_log("player %s (id %x) copy at %lx, wallet %u", s_players[g].name, pid, P, enc_get((EncInt*)(P+0xB8))); }
                    else if(g>=0){ s_players[g].drop++; aden_log("player %s copy at %lx dropped: %d-copy cap", s_players[g].name, P, MAXCOPIES); }
                } else if(s_ncand<MAXCAND) s_cand[s_ncand++]=P;   /* pocket found but layout unknown (kept for fallback only) */
            }
        }
        a=end;
    }
}
static void find_thread(void* arg){
    (void)arg;
    s_ncopies=0; s_ncand=0; s_nplayers=0; s_active=0;
    int empty=0; u64 t0=nn_os_GetSystemTick(); s_attr_rej=0; memset(s_skip_mb,0,sizeof s_skip_mb); memset(s_skip_n,0,sizeof s_skip_n);
    scan_players(0,&empty);
    aden_log("search: %d players, %d empty slots, %d other pocket matches, %lu ms", s_nplayers, empty, s_ncand, (nn_os_GetSystemTick()-t0)/19200);
    if(s_attr_rej) aden_log("search: %d pockets skipped for memory attributes (last attr %x)", s_attr_rej, s_attr_last);
    for(int i=0;i<8;i++) if(s_skip_n[i]) aden_log("search: skipped heap with attr %x: %d regions, %lu MB", i, s_skip_n[i], s_skip_mb[i]>>20);
    if(!s_nplayers){
        /* nothing in plain memory: read the GPU-mapped heap too (may lower FPS on Eden with Reactive Flushing on) */
        t0=nn_os_GetSystemTick(); empty=0;
        scan_players(1,&empty);
        aden_log("search (DeviceShared heap): %d players, %d empty slots, %d other pocket matches, %lu ms", s_nplayers, empty, s_ncand, (nn_os_GetSystemTick()-t0)/19200);
    }
    if(s_nplayers){
        /* default to the player with the most copies in memory: that is the one being played.
           A tie goes to the lowest PlayerID, so the guess does not depend on where memory landed. */
        int best=0, tie=0;
        for(int j=0;j<s_nplayers;j++){ const Player *p=&s_players[j], *b=&s_players[best];
            aden_log("player %s (id %x): %d copies, %d dropped", p->name, p->pid, p->n, p->drop);
            if(p->n>b->n || (p->n==b->n && p->pid<b->pid)) best=j; }
        for(int j=0;j<s_nplayers;j++) if(j!=best && s_players[j].n==s_players[best].n) tie++;
        use_player(best);
        aden_log("editing player %s (%d copies)%s", s_players[best].name, s_ncopies, tie?", tied on copies: lowest id picked, use the resident picker to switch":"");
    } else {
        /* fallback: old behaviour, any pocket that has something in it */
        for(int i=0;i<s_ncand && s_ncopies<MAXCOPIES;i++) if(has_stuff(s_cand[i])) s_copy[s_ncopies++]=s_cand[i];
        aden_log("fallback: %d pocket copies from %d matches (cap %d)", s_ncopies, s_ncand, MAXCOPIES);
        find_fields(); find_personal_bases();
    }
    g_player_state = s_ncopies ? 2 : 3;
    aden_log("player search done: %d copies", s_ncopies);
}
int aden_player_count(void){ return s_nplayers; }
int aden_player_active(void){ return s_active; }
const char* aden_player_name(int i){ return (i>=0 && i<s_nplayers) ? s_players[i].name : ""; }
void aden_player_select(int i){
    if(g_player_state!=2 || i<0 || i>=s_nplayers || i==s_active) return;
    use_player(i);
    island_drop();   /* friendship is per player */
    aden_log("switched to player %s (%d copies)", s_players[i].name, s_ncopies);
}
static int s_th_used, s_th2_used;
void aden_player_find(void){
    if(g_player_state==1||g_player_state==2) return;
    g_player_state=1;
    /* a previous search thread must be cleaned up before its slot is reused */
    if(s_th_used){ nn_os_WaitThread(&s_th); nn_os_DestroyThread(&s_th); s_th_used=0; }
    if(nn_os_CreateThread(&s_th,find_thread,0,s_stack,sizeof s_stack,SEARCH_PRIO)){ g_player_state=3; aden_log("could not start search thread"); return; }
    s_th_used=1;
    nn_os_StartThread(&s_th);
}
/* re-check cached copies (memory can move after loading screens). One bad copy means the
   others may be old save buffers too: stop writing at once and, if it lasts (not just a
   write caught half-way), drop everything derived from the copies and search again. */
static uptr s_pb[MAXF]; static int s_npb;          /* personal save bases */
static uptr s_mb[4]; static int s_nmb;             /* island (main) save bases */
#define STALE_CHECKS 8
static int range_rw(uptr a, uptr len);
static int heap_rw(uptr a, uptr len);
static int still_ok(void){
    /* an unmapped copy counts as bad too: sig_ok would fault on it (it reads P-0xB8..P+0xC0) */
    for(int i=0;i<s_ncopies;i++) if(!range_rw(s_copy[i]-0xB8,0x178) || !sig_ok(s_copy[i])){
        if(++s_bad<STALE_CHECKS) return 0;
        aden_log("player copy %d at %lx went stale, searching again", i, s_copy[i]);
        s_ncopies=0; s_npb=0; s_bad=0; for(int f=0;f<3;f++) s_fields[f].n=0;
        if(!__atomic_load_n(&s_th2_run,__ATOMIC_ACQUIRE)){ s_nmb=0; g_island_state=0; }   /* a running island search keeps its result */
        g_player_state=0;
        return 0;
    }
    s_bad=0; return s_ncopies;
}
int aden_player_ready(void){ return g_player_state==2 && still_ok(); }
int aden_player_copies(void){ return s_ncopies; }
u32 aden_wallet_get(void){ return s_ncopies && heap_rw(s_copy[0]+0xB8,8) ? enc_get((EncInt*)(s_copy[0]+0xB8)) : 0; }
void aden_wallet_set(u32 v){
    if(v>99999) v=99999;
    if(!aden_player_ready()) return;   /* checks every copy is still mapped and valid */
    for(int i=0;i<s_ncopies;i++) enc_set((EncInt*)(s_copy[i]+0xB8),v);
    aden_log("wallet set to %u in %d copies", v, s_ncopies);
}
/* >= 0 items placed, -1 player data not ready, -2 pockets look wrong (nothing written) */
int aden_pocket_add(u16 id, int qty, int stack){
    if(!aden_player_ready()) return -1;   /* also checks every copy is still mapped and valid */
    uptr P=s_copy[0]; u32 size=*(u32*)(P+0xA0);
    /* a copy may still hold the size from before an upgrade: never fill a slot locked in any copy */
    for(int c=1;c<s_ncopies;c++){ u32 z=*(u32*)(s_copy[c]+0xA0); if(z<size) size=z; }
    /* sig_ok covers slots 0-19; the unlocked part of pocket 2 must look like items too */
    for(int c=0;c<s_ncopies;c++) for(u32 i=20;i<size && i<40;i++) if(!slot_ok((u8*)(s_copy[c]-0xB8+(i-20)*8))){
        aden_log("pocket copy %d slot %u looks wrong, not touching pockets", c, i); return -2; }
    int placed=0;
    while(qty>0){
        int slot=-1;
        /* copies can differ: a slot is free only if it is empty in every copy we write */
        for(u32 i=0;i<size && i<40 && slot<0;i++){ int c=0;
            while(c<s_ncopies && *(u16*)(i<20 ? s_copy[c]+i*8 : s_copy[c]-0xB8+(i-20)*8)==0xFFFE) c++;
            if(c==s_ncopies) slot=i; }
        if(slot<0) break;
        int n = stack>1 ? (qty>stack?stack:qty) : 1;
        for(int c=0;c<s_ncopies;c++){
            uptr Q=s_copy[c]; uptr s= slot<20 ? Q+slot*8 : Q-0xB8+(slot-20)*8;
            *(u32*)(s+4) = stack>1 ? (u32)(n-1) : 0;
            *(u16*)(s+2) = 0;
            *(u16*)s = id;
        }
        qty-=n; placed+=n;
    }
    aden_log("pocket add %04X: placed %d", id, placed);
    return placed;
}

/* =====================================================================
   Whole-save features. The game keeps each save file in memory with the
   same layout as the file on disk, so NHSE's 3.0 offsets apply directly.
   P (our pocket anchor) sits at PERS_P inside the personal save.
   ===================================================================== */
#define PERS_SIZE    0x74A40
#define PERS_ID      0xC248     /* TownId{u32,u16[10]} then PlayerId at +0x1C{u32,u16[10]} */
#define PERS_MILES   0x12718
#define PERS_RECIPES 0x6528C    /* 4 x 0x100-byte bit arrays: known, made, new, favourite */
#define PERS_MANPU   0x4A85C    /* 256 reaction ids, then 8 wheel slots, then 256 new flags */
#define PERS_CHEST   0x37D6C    /* home storage, 9000 items */
#define CHEST_N      9000
#define MAIN_SIZE    0x9B0E90
#define MAIN_VILL    0x120
#define VILL_SIZE    0x13230
#define VILL_N       10
#define MEM_SIZE     0x5F0
#define MAIN_KABU    (0x110+0x48d920+0x2d40)
extern const u8 g_recipe_valid[0x100]; extern const int g_nrecipes;
extern const u8 g_reactions[]; extern const int g_nreactions;
typedef struct{u8 sp,var;const char* name;} VName;
extern const VName g_vnames[]; extern const int g_nvnames;

static int pers_ok(void){ return g_player_state==2 && s_npb; }   /* personal bases only hold while the player data is ready */

static int range_chk(uptr a, uptr len, int heap_only){
    /* every page in [a, a+len) must be plain read/write memory (and game heap if heap_only) */
    uptr e=a+len;
    while(a<e){ MemoryInfo mi; u32 pi; if(svcQueryMemory(&mi,&pi,a) || mi.size==0) return 0;
        if((mi.perm&3)!=3 || mi.type==0 || !MI_RW(mi) || (heap_only && mi.type!=5)) return 0; a=mi.addr+mi.size; }
    return 1;
}
static int range_rw(uptr a, uptr len){ return range_chk(a,len,0); }
static int heap_rw(uptr a, uptr len){ return range_chk(a,len,1); }
static void find_personal_bases(void){
    s_npb=0;
    for(int i=0;i<s_fields[0].n && s_npb<MAXF;i++){
        uptr b=s_fields[0].a[i]-PERS_MILES;
        if(range_rw(b,PERS_SIZE)) s_pb[s_npb++]=b;
    }
    if(!s_npb) for(int i=0;i<s_ncopies && s_npb<MAXF;i++){ uptr b=s_copy[i]-PERS_P; if(range_rw(b,PERS_SIZE)) s_pb[s_npb++]=b; }
    aden_log("personal saves: %d", s_npb);
}
static int kabu_ok(uptr k){
    const u32* v=(const u32*)k; for(int i=0;i<15;i++) if(v[i]>1000) return 0;
    return v[15]<4;
}
/* taken from s_pb[0] by the render thread before the island thread starts: the player search may rewrite s_pb meanwhile */
static u32 s_town[6], s_ph[4];
static void island_drop(void){
    /* drop the island result, or mark a running search obsolete: it then ends in state 0 and
       the next aden_island_find starts again. A flag set just after the thread checked it is
       caught by aden_island_ready. */
    __atomic_store_n(&s_island_stale,1,__ATOMIC_SEQ_CST);
    if(!__atomic_load_n(&s_th2_run,__ATOMIC_SEQ_CST)){ s_nmb=0; g_island_state=0; }
}
static void find_main(void){
    const u8* town=(const u8*)s_town; u32 tid=s_town[0];
    uptr mb[4]; int nmb=0;
    /* if the personal save carries the 3.0 file header, the island save must too */
    const u32* ph=s_ph;
    int need_hdr = ph[0]==0xA0002 && ph[1]==0xA0028;
    aden_log("personal header %x %x %x %x -> %s", ph[0],ph[1],ph[2],ph[3], need_hdr?"using header check":"no header, using layout check");
    int hits=0; u64 t0=nn_os_GetSystemTick();
    /* plain heap first, then the DeviceShared heap if no island save was found (as in the player search) */
    for(int dev=0; dev<2 && !nmb; dev++){
        uptr a=0; MemoryInfo mi; u32 pi;
        while(a<0x8000000000ul && nmb<4){
            if(svcQueryMemory(&mi,&pi,a)||mi.size==0) break;
            uptr end=mi.addr+mi.size;
            /* only ordinary game heap (type 5), same as the player search; other memory
               (graphics, shared, transfer buffers) can disappear under us */
            if(scan_ok(&mi,dev)){
                for(uptr x=mi.addr; x+24<=end; x+=4){
                    if(!(x&(SCAN_CHUNK-1)) && (!scan_nap(x,&end,dev) || x+24>end)) break;
                    if(*(u32*)x!=tid || memcmp((void*)x,town,24)) continue;
                    hits++;
                    uptr M=x-(MAIN_VILL+4);
                    if(!heap_rw(M,MAIN_SIZE)) continue;
                    u8 sp=*(u8*)(M+MAIN_VILL);
                    if(sp>35 || !kabu_ok(M+MAIN_KABU)) continue;
                    if(need_hdr){ const u32* h=(const u32*)M; if(h[0]!=ph[0]||h[1]!=ph[1]||h[3]!=ph[3]) continue; }
                    else {
                        /* no header: the slot before villager 0 must not look like a villager,
                           and villager 0's own record must start with a valid species/variant */
                        uptr prev=M+MAIN_VILL-VILL_SIZE+4;
                        if(heap_rw(prev,24) && memcmp((void*)prev,town,24)==0) continue;
                        if(*(u8*)(M+MAIN_VILL+1)>40) continue;
                        if(*(u32*)(M+MAIN_KABU)==0 && *(u32*)(M+MAIN_KABU+4)==0) continue;
                    }
                    int dup=0; for(int j=0;j<nmb;j++) if(mb[j]==M) dup=1; if(dup) continue;
                    mb[nmb++]=M;
                    const u8* h=(const u8*)M;
                    aden_log("island save %d at %lx, header %02x%02x%02x%02x %02x%02x%02x%02x, turnips buy %u", nmb, M, h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7], *(u32*)(M+MAIN_KABU));
                    if(nmb>=4) break;
                }
            }
            a=end;
        }
    }
    aden_log("island search: %d town-id hits, %d saves, %lu ms", hits, nmb, (nn_os_GetSystemTick()-t0)/19200);
    /* the render thread leaves s_mb alone while we run, so we are its only writer here */
    int st = nmb ? 2 : 3;
    if(__atomic_exchange_n(&s_island_stale,0,__ATOMIC_SEQ_CST)){ aden_log("island search obsolete, dropped"); nmb=0; st=0; }
    for(int i=0;i<nmb;i++) s_mb[i]=mb[i];
    s_nmb=nmb; __atomic_store_n(&g_island_state,st,__ATOMIC_RELEASE);
}
static void island_thread(void* arg){ (void)arg; aden_log("island search started"); find_main();
    __atomic_store_n(&s_th2_run,0,__ATOMIC_SEQ_CST); }   /* last store: it publishes s_mb and the state */
static OsThread s_th2; static u8 s_stack2[0x10000] __attribute__((aligned(0x1000)));
void aden_island_find(void){
    if(g_island_state==1||g_island_state==2) return;
    if(g_player_state!=2) return;
    /* never wait on a running thread from the render thread: retry on a later frame once it has exited */
    if(__atomic_load_n(&s_th2_run,__ATOMIC_ACQUIRE)) return;
    if(s_th2_used){ nn_os_WaitThread(&s_th2); nn_os_DestroyThread(&s_th2); s_th2_used=0; }
    if(!pers_ok() || !range_rw(s_pb[0],16) || !range_rw(s_pb[0]+PERS_ID,24)){ s_nmb=0; g_island_state=3; return; }
    memcpy(s_town,(const void*)(s_pb[0]+PERS_ID),24); memcpy(s_ph,(const void*)s_pb[0],16);
    if(!s_town[0]){ aden_log("no town id"); s_nmb=0; g_island_state=3; return; }
    __atomic_store_n(&s_island_stale,0,__ATOMIC_SEQ_CST);
    g_island_state=1;
    __atomic_store_n(&s_th2_run,1,__ATOMIC_SEQ_CST);
    if(nn_os_CreateThread(&s_th2,island_thread,0,s_stack2,sizeof s_stack2,SEARCH_PRIO)){
        __atomic_store_n(&s_th2_run,0,__ATOMIC_SEQ_CST); g_island_state=3; aden_log("could not start island thread"); return; }
    s_th2_used=1;
    nn_os_StartThread(&s_th2);
}
int aden_island_ready(void){
    if(__atomic_load_n(&g_island_state,__ATOMIC_ACQUIRE)!=2 || !aden_player_ready()) return 0;   /* villagers are matched to the personal save */
    /* obsoleted just after the thread checked: search again */
    if(__atomic_load_n(&s_island_stale,__ATOMIC_SEQ_CST) && !__atomic_load_n(&s_th2_run,__ATOMIC_SEQ_CST)){
        __atomic_store_n(&s_island_stale,0,__ATOMIC_SEQ_CST); s_nmb=0; g_island_state=0; return 0; }
    int k=0; for(int i=0;i<s_nmb;i++) if(heap_rw(s_mb[i],0x1000) && kabu_ok(s_mb[i]+MAIN_KABU)) s_mb[k++]=s_mb[i];
    s_nmb=k; if(!k) g_island_state=0; return k;
}

/* ---- names ---- */
static void utf16_ascii(char* out,int cap,const u16* s,int n){
    int k=0; for(int i=0;i<n && s[i] && k<cap-1;i++) out[k++]= s[i]<128 ? (char)s[i] : '?';
    out[k]=0;
}
void aden_player_names(char* player,char* town,int cap){
    player[0]=town[0]=0; if(!pers_ok()) return;
    uptr id=s_pb[0]+PERS_ID; if(!range_rw(id,0x34)) return;
    utf16_ascii(town,cap,(const u16*)(id+4),10);
    utf16_ascii(player,cap,(const u16*)(id+0x20),10);
}

/* ---- recipes ---- */
int aden_recipes_learn_all(void){
    if(!pers_ok()) return -1;
    int learned=0;
    for(int c=0;c<s_npb;c++){
        u8* R=(u8*)(s_pb[c]+PERS_RECIPES);
        if(!range_rw((uptr)R,0x300)){ aden_log("recipes: copy %d not mapped, skipped", c); continue; }
        int bad=0; for(int i=0;i<0x800;i++) if((R[i>>3]>>(i&7)&1) && !(g_recipe_valid[i>>3]>>(i&7)&1)) bad++;
        if(bad>16){ aden_log("recipes: copy %d looks wrong (%d odd bits), skipped", c, bad); continue; }
        int n=0;
        for(int i=0;i<0x800;i++){
            if(!(g_recipe_valid[i>>3]>>(i&7)&1) || (R[i>>3]>>(i&7)&1)) continue;
            R[i>>3]|=1<<(i&7); R[0x200+(i>>3)]|=1<<(i&7); n++;
        }
        if(c==0) learned=n;
    }
    aden_log("recipes: learned %d new in %d copies", learned, s_npb);
    return learned;
}
int aden_recipes_known(void){
    if(!pers_ok() || !range_rw(s_pb[0]+PERS_RECIPES,0x100)) return 0; const u8* R=(const u8*)(s_pb[0]+PERS_RECIPES); int n=0;
    for(int i=0;i<0x800;i++) if((R[i>>3]>>(i&7)&1) && (g_recipe_valid[i>>3]>>(i&7)&1)) n++;
    return n;
}
int aden_recipes_total(void){ return g_nrecipes; }

/* ---- reactions ---- */
int aden_reactions_learn_all(void){
    if(!pers_ok()) return -1;
    int added=0;
    for(int c=0;c<s_npb;c++){
        u8* M=(u8*)(s_pb[c]+PERS_MANPU);
        if(!range_rw((uptr)M,256+8+256)){ aden_log("reactions: copy %d not mapped, skipped", c); continue; }
        int bad=0; for(int i=0;i<256;i++) if(M[i]>120) bad++;
        if(bad){ aden_log("reactions: copy %d looks wrong, skipped", c); continue; }
        int n=0;
        for(int r=0;r<g_nreactions;r++){
            u8 id=g_reactions[r]; int have=0, empty=-1;
            for(int i=0;i<256;i++){ if(M[i]==id){have=1;break;} if(M[i]==0 && empty<0) empty=i; }
            if(have||empty<0) continue;
            M[empty]=id; M[256+8+empty]=1; n++;   /* mark as new so the game shows the badge */
        }
        if(c==0) added=n;
    }
    aden_log("reactions: added %d in %d copies", added, s_npb);
    return added;
}
int aden_reactions_known(void){
    if(!pers_ok() || !range_rw(s_pb[0]+PERS_MANPU,256)) return 0; const u8* M=(const u8*)(s_pb[0]+PERS_MANPU); int n=0;
    for(int i=0;i<256;i++) if(M[i] && M[i]<=120) n++;
    return n;
}

/* ---- home storage ---- */
/* copies can differ: a slot is free only if it is empty in every copy we write */
static int chest_free(int i){ for(int c=0;c<s_npb;c++) if(*(u16*)(s_pb[c]+PERS_CHEST+i*8)!=0xFFFE) return 0; return 1; }
/* NHSE (Personal.ItemChestCount): the u32 right after the 9000 slots is how many slots the
   storage upgrades unlocked. The game never shows slots past it, so stay below the smallest
   count of all copies. A count of 0 or above 9000 means the base is wrong.
   Returns the usable slot count, -1 not ready, -2 storage looks wrong. */
#define CHEST_CNT    (PERS_CHEST+CHEST_N*8)
static int chest_cap(void){
    if(!aden_player_ready() || !s_npb) return -1;   /* also catches stale copies before reading */
    int cap=CHEST_N;
    for(int c=0;c<s_npb;c++){
        uptr S=s_pb[c]+PERS_CHEST; u32 n;
        if(!range_rw(S,CHEST_N*8+4)) return -1;     /* memory moved: the stale check will search again */
        n=*(u32*)(s_pb[c]+CHEST_CNT);
        if(!n || n>CHEST_N){ aden_log("storage copy %d: count %u looks wrong, not touching it", c, n); return -2; }
        if((int)n<cap) cap=(int)n;
        for(int i=0;i<64;i++) if(!slot_ok((u8*)(S+i*8))){ aden_log("storage copy %d looks wrong, not touching it", c); return -2; }
    }
    return cap;
}
/* >= 0 items placed, -1 player data not ready, -2 storage looks wrong (nothing written) */
int aden_storage_add(u16 id,int qty,int stack){
    int cap=chest_cap(), placed=0, slot=0;
    if(cap<0) return cap;
    while(qty>0){
        while(slot<cap && !chest_free(slot)) slot++;
        if(slot>=cap) break;
        int n = stack>1 ? (qty>stack?stack:qty) : 1;
        for(int c=0;c<s_npb;c++){
            uptr s=s_pb[c]+PERS_CHEST+slot*8;
            *(u32*)(s+4) = stack>1 ? (u32)(n-1) : 0; *(u16*)(s+2)=0; *(u16*)s=id;
        }
        qty-=n; placed+=n; slot++;
    }
    aden_log("storage add %04X: placed %d (%d usable slots)", id, placed, cap);
    return placed;
}
int aden_storage_free(void){
    int cap=chest_cap(), n=0;
    for(int i=0;i<cap;i++) if(chest_free(i)) n++;
    return n;
}

/* ---- turnips ---- */
u32 aden_turnip_sell(void){ return s_nmb && heap_rw(s_mb[0]+MAIN_KABU,8) ? *(u32*)(s_mb[0]+MAIN_KABU+4) : 0; }
u32 aden_turnip_buy(void){ return s_nmb && heap_rw(s_mb[0]+MAIN_KABU,8) ? *(u32*)(s_mb[0]+MAIN_KABU) : 0; }
void aden_turnip_set(int sell,u32 v){
    for(int c=0;c<s_nmb;c++){ u32* k=(u32*)(s_mb[c]+MAIN_KABU); if(!heap_rw((uptr)k,15*4)) continue;
        if(sell) for(int i=1;i<15;i++) k[i]=v; else k[0]=v; }
    aden_log("turnip %s price set to %u", sell?"sell":"buy", v);
}

/* ---- villagers ---- */
static uptr vill(int c,int k){ return s_mb[c]+MAIN_VILL+(uptr)k*VILL_SIZE; }
static int my_mem(uptr V){
    if(!pers_ok() || !range_rw(s_pb[0]+PERS_ID+0x1C,4)) return -1; u32 me=*(u32*)(s_pb[0]+PERS_ID+0x1C);
    for(int j=0;j<8;j++) if(*(u32*)(V+4+j*MEM_SIZE+0x1C)==me) return j;
    return -1;
}
int aden_villager_info(int k,const char** name,int* friendship,int* moving){
    if(!s_nmb || k<0 || k>=VILL_N) return 0;
    uptr V=vill(0,k); if(!heap_rw(V,VILL_SIZE)) return 0;
    u8 sp=*(u8*)V, var=*(u8*)(V+1);
    if(sp>=35) return 0;
    *name="(unknown)";
    for(int i=0;i<g_nvnames;i++) if(g_vnames[i].sp==sp && g_vnames[i].var==var){ *name=g_vnames[i].name; break; }
    int j=my_mem(V); *friendship = j<0 ? -1 : *(u8*)(V+4+j*MEM_SIZE+0x38+10);
    *moving = (*(u8*)(V+0x1267a)&2)!=0;
    return 1;
}
void aden_villager_friend(int k,u8 v){
    if(k<0 || k>=VILL_N) return;
    for(int c=0;c<s_nmb;c++){ uptr V=vill(c,k); if(!heap_rw(V,VILL_SIZE) || *(u8*)V>=35) continue; int j=my_mem(V); if(j>=0) *(u8*)(V+4+j*MEM_SIZE+0x38+10)=v; }
    aden_log("villager %d friendship set to %u", k, v);
}
void aden_villager_moving(int k,int on){
    if(k<0 || k>=VILL_N) return;
    for(int c=0;c<s_nmb;c++){ if(!heap_rw(vill(c,k),VILL_SIZE)) continue; u8* m=(u8*)(vill(c,k)+0x1267a); *m=(u8)((*m&~2)|(on?2:0)); }
    aden_log("villager %d moving out = %d", k, on);
}
