#include "nn.h"
/* Mini LayeredFS. When the game opens content:/X, we serve the file from the SD card instead if
   an enabled mod (sd:/aden/mods/<Mod>/romfs/X) or Aden Modmenu itself (sd:/aden/romfs/X) has it.
   The table is built once, at the first game file open, so mod changes apply on the next start. */
#define OWN_ROOT  "adensd:/aden/romfs"
#define MODS_ROOT "adensd:/aden/mods"
#define MODS_LIST "adensd:/aden/mods_on.txt"
#define OLD_ROOT  "adensd:/aden/mods_enabled"   /* folder used by v0.4 and earlier */
#define NBUCKET 4096
#define MAXFILES 20000
typedef struct Ent { struct Ent* next; u32 hash; const char* rel; const char* src; } Ent;
static Ent* s_bucket[NBUCKET]; static int s_nfiles; static int s_state; /* atomic: 0 none, 1 building, 2 ready */
static char* s_arena; static u32 s_arena_left;

static u32 fnv(const char* s){ u32 h=2166136261u; for(;*s;s++){ char c=*s; if(c>='A'&&c<='Z') c+=32; h=(h^(u8)c)*16777619u; } return h; }
static int ieq(const char* a,const char* b){ for(;*a&&*b;a++,b++){ char x=*a,y=*b; if(x>='A'&&x<='Z')x+=32; if(y>='A'&&y<='Z')y+=32; if(x!=y) return 0; } return *a==*b; }
static char* sdup(const char* s){
    u32 n=strlen(s)+1;
    if(n>s_arena_left){ s_arena=malloc(0x10000); if(!s_arena){ s_arena_left=0; return 0; } s_arena_left=0x10000; }
    char* p=s_arena; memcpy(p,s,n); s_arena+=n; s_arena_left-=n; return p;
}
static Ent* find(const char* rel){
    u32 h=fnv(rel);
    for(Ent* e=s_bucket[h&(NBUCKET-1)];e;e=e->next) if(e->hash==h && ieq(e->rel,rel)) return e;
    return 0;
}
static int add(const char* rel,const char* src){
    if(s_nfiles>=MAXFILES || find(rel)) return 0;        /* first one added wins */
    Ent* e=malloc(sizeof(Ent)); if(!e) return 0;
    e->rel=sdup(rel); e->src=sdup(src); if(!e->rel||!e->src) return 0;
    e->hash=fnv(rel); e->next=s_bucket[e->hash&(NBUCKET-1)]; s_bucket[e->hash&(NBUCKET-1)]=e; s_nfiles++;
    return 1;
}
static int walk(const char* dir, const char* rel, int depth){
    if(depth>12) return 0;
    FsDir d; if(nn_fs_OpenDirectory(&d,dir,3)) return 0;
    int n_added=0;
    FsDirEntry* e=malloc(sizeof(FsDirEntry)*8); s64 n=0;
    while(e && nn_fs_ReadDirectory(&n,e,d,8)==0 && n>0){
        for(int i=0;i<n;i++){
            char sub[300], srel[260];
            aden_snprintf(sub,sizeof sub,"%s/%s",dir,e[i].name);
            aden_snprintf(srel,sizeof srel,"%s%s%s",rel,rel[0]?"/":"",e[i].name);
            if(e[i].type==0) n_added+=walk(sub,srel,depth+1);
            else if(e[i].size<=0) aden_log("  skipped empty file %s", srel);   /* an empty game file crashes the game */
            else n_added+=add(srel,sub);
        }
    }
    free(e); nn_fs_CloseDirectory(d);
    return n_added;
}

/* ---- enabled mod list (shared with the Mods tab) ---- */
static char s_on[64][64]; static int s_non; static int s_list_loaded;
static void list_load(void){
    s_list_loaded=1; s_non=0;
    FsFile f;
    if(nn_fs_OpenFile(&f,MODS_LIST,FS_READ)){
        /* first run after v0.4: everything in the old enabled folder counts as on */
        FsDir d; if(nn_fs_OpenDirectory(&d,OLD_ROOT,1)) return;
        FsDirEntry* e=malloc(sizeof(FsDirEntry)*8); s64 n=0;
        while(e && nn_fs_ReadDirectory(&n,e,d,8)==0 && n>0)
            for(int i=0;i<n && s_non<64;i++){ int k=0; while(e[i].name[k]&&k<63){ s_on[s_non][k]=e[i].name[k]; k++; } s_on[s_non][k]=0; s_non++; }
        free(e); nn_fs_CloseDirectory(d);
        return;
    }
    s64 sz=0; nn_fs_GetFileSize(&sz,f); if(sz>8192) sz=8192;
    char* b=malloc(sz+1); if(!b){ nn_fs_CloseFile(f); return; }
    nn_fs_ReadFile(f,0,b,sz); nn_fs_CloseFile(f); b[sz]=0;
    char* p=b;
    while(*p && s_non<64){
        char* l=p; while(*p && *p!='\n') p++; if(*p) *p++=0;
        int k=0; while(l[k] && l[k]!='\r' && k<63){ s_on[s_non][k]=l[k]; k++; } s_on[s_non][k]=0;
        if(k) s_non++;
    }
    free(b);
}
static void list_save(void){
    char b[64*66]; int n=0;
    for(int i=0;i<s_non;i++) n+=aden_snprintf(b+n,sizeof b-n,"%s\n",s_on[i]);
    nn_fs_DeleteFile(MODS_LIST);
    if(nn_fs_CreateFile(MODS_LIST,0)) { aden_log("could not create %s", MODS_LIST); return; }
    FsFile f; if(nn_fs_OpenFile(&f,MODS_LIST,FS_WRITE|FS_APPEND)) return;
    FsWriteOption o={1}; if(n) nn_fs_WriteFile(f,0,b,n,&o); nn_fs_CloseFile(f);
}
HIDDEN int aden_mod_is_on(const char* name){
    if(!s_list_loaded) list_load();
    for(int i=0;i<s_non;i++) if(strcmp(s_on[i],name)==0) return 1;
    return 0;
}
HIDDEN void aden_mod_set(const char* name,int on){
    if(!s_list_loaded) list_load();
    int at=-1; for(int i=0;i<s_non;i++) if(strcmp(s_on[i],name)==0) at=i;
    if(on && at<0 && s_non<64){ int k=0; while(name[k]&&k<63){ s_on[s_non][k]=name[k]; k++; } s_on[s_non][k]=0; s_non++; }
    if(!on && at>=0){ for(int i=at;i<s_non-1;i++) memcpy(s_on[i],s_on[i+1],64); s_non--; }
    list_save();
    aden_log("mod %s turned %s", name, on?"on":"off");
}
HIDDEN int aden_romfs_active(const char* name){
    /* was this mod loaded at start-up? */
    (void)name; return __atomic_load_n(&s_state,__ATOMIC_ACQUIRE)==2;
}

static void build(void){
    list_load();
    /* enabled mods first (list order = priority), then Aden Modmenu's own files */
    for(int i=0;i<s_non;i++){
        char dir[200]; int got=0;
        aden_snprintf(dir,sizeof dir,"%s/%s/romfs",MODS_ROOT,s_on[i]); got=walk(dir,"",0);
        if(!got){ aden_snprintf(dir,sizeof dir,"%s/%s/romfs",OLD_ROOT,s_on[i]); got=walk(dir,"",0); }
        aden_log("mod %s: %d files", s_on[i], got);
    }
    int own=walk(OWN_ROOT,"",0);
    if(!own) aden_log("WARNING: no files in sd:/aden/romfs - the app will show as 'Room Sketch'. Copy the aden folder to the SD card.");
    aden_log("romfs overrides: %d files total (%d from Aden Modmenu)", s_nfiles, own);
}

static Result (*o_open)(FsFile*, const char*, int);
static Result h_open(FsFile* f, const char* path, int mode){
    if(path && memcmp(path,"content:/",9)==0){
        int st=__atomic_load_n(&s_state,__ATOMIC_ACQUIRE);   /* acquire: a 2 makes the built table visible */
        if(st==0 && aden_sd_ready()){
            /* one thread claims 0 -> 1 and builds; the others see 1 and use the game's file */
            int z=0;
            if(__atomic_compare_exchange_n(&s_state,&z,1,0,__ATOMIC_ACQUIRE,__ATOMIC_ACQUIRE)){ build(); __atomic_store_n(&s_state,2,__ATOMIC_RELEASE); st=2; }
            else st=z;
        }
        if(st==2 && s_nfiles){
            Ent* e=find(path+9);
            if(e){
                Result r=o_open(f,e->src,mode);
                if(r==0) return 0;
                aden_log("redirect %s failed (%x), using the game's file", path+9, r);
            }
        }
    }
    return o_open(f,path,mode);
}
void aden_romfs_install(void){
    aden_hook((uptr)&nn_fs_OpenFile,(void*)h_open,(void**)&o_open);
}
