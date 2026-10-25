#include "nn.h"
/* Stack 99: raise the max stack of safe item kinds to 99, switched from the Mods tab.
   Same kinds as the Stack99 romfs mod by PlebGeneral (gamebanana.com/mods/689127), but no file is
   replaced: we patch the game's own Bcsv/ItemKind.bcsv where it lands in memory.
   romfs.c tells us the handle of that file; our ReadFile hook records the buffer it is read into.
   At load the switch saved on the SD card is applied before the game parses the table, so it
   always works after a restart. A switch while playing patches the same buffer again; that only
   shows up in game if the game keeps reading the table from it. */
#define ON_FILE   "adensd:/aden/stack99_on"
#define H_LABEL   0x87BF00E8u   /* char[32] kind name */
#define H_MAXNUM  0x4C9BA961u   /* s16 MultiHoldMaxNum: 0 not stackable, -1 special (turnips, money) */
#define MAXROWS   512
#define MAXBUF    4
static const char* const KINDS[]={
    "CookingMaterial","PartyPopper","BlowBubble","FierworkHand","TreeSeedling","BushSeedling",
    "VegeSeedling","Vegetable","Weed","WeedLight","FlowerSeed","Flower","Fruit","Mushroom",
    "FishBait","PitFallSeed","Medicine","CraftMaterial","CraftRemake","Ore","Honeycomb",
    "SnowCrystal","AutumnLeaf","Sakurapetal","XmasDeco","StarPiece","ShellDrift","Fence",
    "BdayCupcake","EasterEgg","LoveCrystal","Candy","Feather","Vine","SouvenirChocolate","DreamGold" };
#define NKINDS (int)(sizeof KINDS/sizeof KINDS[0])

typedef struct { u8* p; u64 size; } Buf;
static Buf s_buf[MAXBUF]; static int s_nbuf;
static void* volatile s_kind_h;                 /* handle of the open ItemKind.bcsv, 0 when closed */
static s16 s_orig[MAXROWS]; static int s_have_orig;
static int s_on=-1;                              /* -1 not read from the SD card yet */
static volatile int s_lock;
static void lock(void){ while(__atomic_exchange_n(&s_lock,1,__ATOMIC_ACQUIRE)); }
static void unlock(void){ __atomic_store_n(&s_lock,0,__ATOMIC_RELEASE); }

/* layout of an in-memory bcsv: u32 rows, u32 row size, u16 fields, ..., fields {u32 hash, u32 offset} at 0x1C */
typedef struct { u32 n, esz; u8* rows; u32 lab, max; } Table;
static int table(const u8* p, u64 size, Table* t){
    if(size<0x1C) return 0;
    u32 n=*(const u32*)p, esz=*(const u32*)(p+4); u16 nf=*(const u16*)(p+8);
    if(!n || n>MAXROWS || esz<0x20 || esz>0x400 || !nf || nf>128) return 0;
    u64 base=0x1C+(u64)nf*8; if(base+(u64)n*esz>size) return 0;
    int got=0;
    for(int i=0;i<nf;i++){
        u32 h=*(const u32*)(p+0x1C+i*8), o=*(const u32*)(p+0x20+i*8);
        if(h==H_LABEL && o+32<=esz){ t->lab=o; got|=1; }
        if(h==H_MAXNUM && o+2<=esz){ t->max=o; got|=2; }
    }
    if(got!=3) return 0;
    t->n=n; t->esz=esz; t->rows=(u8*)p+base;
    return 1;
}
static int is_kind(const char* l){
    for(int k=0;k<NKINDS;k++) if(strcmp(l,KINDS[k])==0) return 1;
    return 0;
}
static const char* row_label(const Table* t,u32 i){
    const char* l=(const char*)(t->rows+i*t->esz+t->lab);
    for(int k=0;k<32;k++) if(!l[k]) return l;
    return 0;   /* not NUL-terminated: not a label */
}
/* the buffer must still hold the table: header and every label we touch (the game may free it after parsing) */
static int still_table(const Buf* b, Table* t){
    if(!table(b->p,b->size,t)) return 0;
    int hits=0;
    for(u32 i=0;i<t->n;i++){ const char* l=row_label(t,i); if(!l) return 0; if(is_kind(l)) hits++; }
    return hits>=NKINDS/2;
}
/* lock held: write 99 (on) or the loaded value (off) to the listed kinds of one buffer; returns rows changed */
static int apply(Buf* b, int on){
    Table t; if(!still_table(b,&t)) return -1;
    if(!s_have_orig){ for(u32 i=0;i<t.n;i++) s_orig[i]=*(s16*)(t.rows+i*t.esz+t.max); s_have_orig=t.n; }
    int n=0;
    for(u32 i=0;i<t.n && i<(u32)s_have_orig;i++){
        if(!is_kind(row_label(&t,i))) continue;
        s16* m=(s16*)(t.rows+i*t.esz+t.max); s16 v= on ? 99 : s_orig[i];
        if(*m!=v){ *m=v; n++; }
    }
    return n;
}

static int read_on(void){
    if(s_on<0 && aden_sd_ready()){ FsFile f; s_on= nn_fs_OpenFile(&f,ON_FILE,FS_READ)==0; if(s_on) nn_fs_CloseFile(f); }
    return s_on>0;
}
HIDDEN int aden_stack99_on(void){ return read_on(); }
HIDDEN int aden_stack99_found(void){ return s_nbuf>0; }
/* switch on/off and save the choice; returns rows changed in memory, or -1 when no table is in memory */
HIDDEN int aden_stack99_set(int on){
    s_on=on;
    if(on){ nn_fs_CreateFile(ON_FILE,0); } else nn_fs_DeleteFile(ON_FILE);
    lock();
    int n=-1, k=0;
    for(int i=0;i<s_nbuf;i++){ int r=apply(&s_buf[i],on); if(r>=0){ n=(n<0?0:n)+r; s_buf[k++]=s_buf[i]; } }
    s_nbuf=k;   /* drop buffers that no longer hold the table */
    unlock();
    aden_log("stack 99 turned %s: %d rows changed in memory (%d tables)", on?"on":"off", n, k);
    return n;
}

/* romfs.c: every content:/ file the game opened */
HIDDEN void aden_stack99_opened(const char* rel, FsFile f){
    static const char want[]="Bcsv/ItemKind.bcsv";
    for(int i=0;;i++){ char a=rel[i], b=want[i]; if(a>='A'&&a<='Z') a+=32; if(b>='A'&&b<='Z') b+=32; if(a!=b) return; if(!a) break; }
    s_kind_h=f.h;
    aden_log("ItemKind.bcsv opened");
}
/* the game reads files through any of the three nn::fs::ReadFile overloads (3.0.3 imports all of them;
   beta.6 hooked only the plain one and never saw the table) */
static void seen(FsFile f, s64 off, void* buf, u64 size){
    if(!s_kind_h || f.h!=s_kind_h) return;
    Table t; static int s_parts;
    /* a read of a part (header first, then rows) is not a whole table: keep watching the handle */
    if(off!=0 || !table(buf,size,&t)){ if(s_parts++<8) aden_log("ItemKind.bcsv partial read (offset %lx, %lx bytes)", off, size); return; }
    s_kind_h=0;
    lock();
    int at=-1; for(int i=0;i<s_nbuf;i++) if(s_buf[i].p==buf) at=i;
    if(at<0 && s_nbuf<MAXBUF){ at=s_nbuf++; s_buf[at]=(Buf){buf,size}; }
    int n= at>=0 && read_on() ? apply(&s_buf[at],1) : 0;
    unlock();
    aden_log("ItemKind table at %p: %u kinds, stack 99 %s (%d rows set)", buf, t.n, s_on>0?"on":"off", n);
}
static Result (*o_read)(FsFile, s64, void*, u64);
static Result h_read(FsFile f, s64 off, void* buf, u64 size){
    Result r=o_read(f,off,buf,size);
    if(!r) seen(f,off,buf,size);
    return r;
}
static Result (*o_read_out)(u64*, FsFile, s64, void*, u64);
static Result h_read_out(u64* got, FsFile f, s64 off, void* buf, u64 size){
    Result r=o_read_out(got,f,off,buf,size);
    if(!r) seen(f,off,buf,got?*got:size);
    return r;
}
static Result (*o_read_opt)(u64*, FsFile, s64, void*, u64, const void*);
static Result h_read_opt(u64* got, FsFile f, s64 off, void* buf, u64 size, const void* opt){
    Result r=o_read_opt(got,f,off,buf,size,opt);
    if(!r) seen(f,off,buf,got?*got:size);
    return r;
}
void aden_stack99_install(void){
    aden_hook((uptr)&nn_fs_ReadFile,(void*)h_read,(void**)&o_read);
    aden_hook((uptr)&nn_fs_ReadFileOut,(void*)h_read_out,(void**)&o_read_out);
    aden_hook((uptr)&nn_fs_ReadFileOpt,(void*)h_read_opt,(void**)&o_read_opt);
}
