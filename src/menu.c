#include "nn.h"
/* Aden Modmenu - on-screen UI. Coordinates: 1280x720, origin at screen centre, y up. */
void gfx_text(float x, float y, float scale, u32 rgba, const char* s);
void gfx_prim_begin(void);
void gfx_rect(float x,float y,float w,float h,u32 rgba);
void gfx_rrect(float x,float y,float w,float h,float r,u32 rgba);
static int s_hl_row=-1, s_hl_key=-1;   /* highlight pill: content row, keyboard column */
u64  aden_input_take(void);
int aden_player_ready(void); int aden_player_copies(void); void aden_player_find(void);
int aden_stat_ok(int); u32 aden_stat_get(int); void aden_stat_set(int,u32);
void aden_player_names(char*,char*,int);
int aden_player_count(void); int aden_player_active(void); const char* aden_player_name(int); void aden_player_select(int);
int aden_recipes_learn_all(void); int aden_recipes_known(void); int aden_recipes_total(void);
int aden_reactions_learn_all(void); int aden_reactions_known(void);
int aden_storage_add(u16,int,int); int aden_storage_free(void);
extern volatile int g_island_state; void aden_island_find(void); int aden_island_ready(void);
u32 aden_turnip_sell(void); u32 aden_turnip_buy(void); void aden_turnip_set(int,u32);
int aden_villager_info(int,const char**,int*,int*); void aden_villager_friend(int,u8); void aden_villager_moving(int,int);
u32 aden_wallet_get(void); void aden_wallet_set(u32); int aden_pocket_add(u16 id,int qty,int stack);
extern volatile int g_player_state;
extern volatile int g_menu_open, g_input_hold;

enum { B_A=1<<0, B_B=1<<1, B_X=1<<2, B_Y=1<<3, B_L=1<<6, B_R=1<<7, B_ZL=1<<8, B_ZR=1<<9, B_PLUS=1<<10, B_MINUS=1<<11,
       B_LEFT=1<<12, B_UP=1<<13, B_RIGHT=1<<14, B_DOWN=1<<15, S_LEFT=1<<16, S_UP=1<<17, S_RIGHT=1<<18, S_DOWN=1<<19 };

/* colours - Nook Phone app style */
#define C_TITLE  0xFFFFFFFF
#define C_TEXT   0x5B4A3AFF
#define C_DIM    0xA39380FF
#define C_SEL    0x5B4A3AFF
#define C_WARN   0xD0503FFF
#define C_PANEL  0xF7F2E2FF
#define C_HEAD   0x5DC2ABFF
#define C_HEADDK 0x3E9C88FF
#define C_PILL   0xFBDD75FF
#define C_TABOFF 0xE6FFF8FF
#define C_LINE   0xE7DEC6FF

/* ---------- batched text grid ----------
   Each cell = (row, column, colour). At the end of the frame every (column, colour)
   pair becomes ONE multi-line print, keeping draw calls to a handful per frame. */
#define PX 8.0f
#define LH 20.0f
#define MAXROWS 32
#define MAXCELLS 96
typedef struct { u8 row; u8 col; u32 color; char s[72]; } Cell;
static Cell s_cells[MAXCELLS]; static int s_ncells;
static const float COLX[4]={0,330,180,300};
static void put(int row,int col,u32 c,const char* f,...){
    if(s_ncells>=MAXCELLS||row>=MAXROWS) return;
    Cell* e=&s_cells[s_ncells++]; e->row=row; e->col=col; e->color=c;
    __builtin_va_list ap; __builtin_va_start(ap,f); aden_vsnprintf(e->s,sizeof e->s,f,ap); __builtin_va_end(ap);
}
static void flush(float x,float y){
    static char buf[MAXROWS*74];
    u8 done[MAXCELLS]; memset(done,0,sizeof done);
    for(int i=0;i<s_ncells;i++){
        if(done[i]) continue;
        int col=s_cells[i].col; u32 c=s_cells[i].color;
        const char* rows[MAXROWS]; memset(rows,0,sizeof rows); int maxr=0;
        for(int j=i;j<s_ncells;j++) if(!done[j]&&s_cells[j].col==col&&s_cells[j].color==c){ done[j]=1; rows[s_cells[j].row]=s_cells[j].s; if(s_cells[j].row>maxr)maxr=s_cells[j].row; }
        int n=0;
        for(int r=0;r<=maxr;r++){ if(rows[r]){ for(const char* q=rows[r];*q && n<(int)sizeof buf-2;q++) buf[n++]=*q; } if(n<(int)sizeof buf-2) buf[n++]='\n'; }
        buf[n]=0;
        gfx_text(x+COLX[col],y,1.0f,c,buf);
    }
    s_ncells=0;
}
static void text(int row,u32 c,const char* s){ put(row,0,c,"%s",s); }

/* ---------- input with key repeat ---------- */
static u64 s_prev; static int s_hold;
static u64 s_now, s_press;
static void poll(void){
    s_now=aden_input_take();
    s_press=s_now&~s_prev;
    u64 dirs=B_UP|B_DOWN|B_LEFT|B_RIGHT|S_UP|S_DOWN|S_LEFT|S_RIGHT;
    if(s_now&dirs){ s_hold++; if(s_hold>18 && (s_hold&3)==0) s_press|=s_now&dirs; } else s_hold=0;
    s_prev=s_now;
}
static int pr(u64 m){ return (s_press&m)!=0; }
static int up(void){ return pr(B_UP|S_UP); } static int down(void){ return pr(B_DOWN|S_DOWN); }
static int left(void){ return pr(B_LEFT|S_LEFT); } static int right(void){ return pr(B_RIGHT|S_RIGHT); }

/* ---------- item database (sd:/aden/items.txt) ---------- */
typedef struct { u16 id; u16 stack; const char* cat; const char* name; } Item;
static Item* s_items; static int s_nitems; static int s_items_loaded; static char* s_itembuf;
static void load_items(void){
    s_items_loaded=1;
    FsFile f; if(nn_fs_OpenFile(&f,"adensd:/aden/items.txt",FS_READ)){ aden_log("items.txt missing"); return; }
    s64 sz=0; nn_fs_GetFileSize(&sz,f);
    s_itembuf=malloc(sz+1); if(!s_itembuf){ nn_fs_CloseFile(f); return; }
    nn_fs_ReadFile(f,0,s_itembuf,sz); nn_fs_CloseFile(f); s_itembuf[sz]=0;
    int lines=0; for(s64 i=0;i<sz;i++) if(s_itembuf[i]=='\n') lines++;
    s_items=malloc(sizeof(Item)*(lines+1));
    char* p=s_itembuf;
    while(*p){
        char* nl=p; while(*nl && *nl!='\n') nl++;
        char* line=p; p=*nl?nl+1:nl; *nl=0;
        if(nl>line && nl[-1]=='\r') nl[-1]=0;   /* CRLF files */
        if(line[0]=='#'||!line[0]) continue;
        char* t1=line; while(*t1 && *t1!='\t') t1++; if(!*t1) continue; *t1++=0;
        char* t2=t1; while(*t2 && *t2!='\t') t2++; if(!*t2) continue; *t2++=0;
        char* t3=t2; while(*t3 && *t3!='\t') t3++; int st=1; if(*t3){ *t3++=0; st=0; while(*t3>='0'&&*t3<='9') st=st*10+(*t3++-'0'); if(st<1) st=1; }
        u32 id=0; for(char* h=line;*h;h++){ id<<=4; id|= (*h<='9')?*h-'0':((*h|32)-'a'+10); }
        s_items[s_nitems++]=(Item){(u16)id,(u16)st,t1,t2};
    }
    aden_log("loaded %d items", s_nitems);
}
/* search */
static char s_query[24]; static int s_qlen;
static int* s_match; static int s_nmatch;
static int ieq(char a,char b){ if(a>='A'&&a<='Z')a+=32; if(b>='A'&&b<='Z')b+=32; return a==b; }
static int contains(const char* h,const char* n){
    if(!*n) return 1;
    for(;*h;h++){ const char* a=h; const char* b=n; while(*a&&*b&&ieq(*a,*b)){a++;b++;} if(!*b) return 1; }
    return 0;
}
/* items whose count field holds a recipe ID: spawned with 0 they come out broken */
static const char* needs_param(u16 id){
    if(id==0x16A2) return "DIY recipe cards need a recipe ID - learn them in the Player tab";
    if(id==0x16A1) return "Message bottles need a recipe ID - nothing was added";
    return 0;
}
static void refilter(void){
    if(!s_match) s_match=malloc(sizeof(int)*(s_nitems+1));
    s_nmatch=0;
    for(int i=0;i<s_nitems;i++) if(contains(s_items[i].name,s_query)) s_match[s_nmatch++]=i;
}

/* ---------- mods: folders in sd:/aden/mods, on/off kept in sd:/aden/mods_on.txt ---------- */
int aden_mod_is_on(const char*); void aden_mod_set(const char*,int);
typedef struct { char name[64]; int on, on_at_start, romfs, exefs; } Mod;
static Mod s_mods[64]; static int s_nmods; static char s_modmsg[96]; static int s_mods_scanned;
static int dir_exists(const char* p){ FsDir d; if(nn_fs_OpenDirectory(&d,p,3)) return 0; nn_fs_CloseDirectory(d); return 1; }
static void list_dir(const char* root){
    FsDir d; if(nn_fs_OpenDirectory(&d,root,1)) return;
    FsDirEntry* e=malloc(sizeof(FsDirEntry)*16); s64 n=0;
    while(e && nn_fs_ReadDirectory(&n,e,d,16)==0 && n>0){
        for(int i=0;i<n && s_nmods<64;i++){
            if(e[i].type!=0) continue;
            int dup=0; for(int j=0;j<s_nmods;j++) if(strcmp(s_mods[j].name,e[i].name)==0) dup=1;
            if(dup) continue;
            Mod* m=&s_mods[s_nmods++]; int k=0; while(e[i].name[k]&&k<63){ m->name[k]=e[i].name[k]; k++; } m->name[k]=0;
            char p[200];
            aden_snprintf(p,sizeof p,"%s/%s/romfs",root,m->name); m->romfs=dir_exists(p);
            aden_snprintf(p,sizeof p,"%s/%s/exefs",root,m->name); m->exefs=dir_exists(p);
            m->on=m->on_at_start=aden_mod_is_on(m->name);
        }
    }
    free(e); nn_fs_CloseDirectory(d);
}
static void scan_mods(void){
    s_nmods=0; s_mods_scanned=1;
    nn_fs_CreateDirectory("adensd:/aden/mods");
    list_dir("adensd:/aden/mods");
    list_dir("adensd:/aden/mods_enabled");     /* folder used by v0.4 */
    aden_log("mods tab: %d mods", s_nmods);
}

/* ---------- state ---------- */
enum { TAB_ITEMS, TAB_PLAYER, TAB_ISLAND, TAB_MODS, TAB_ABOUT, NTABS };
static const char* TABNAME[NTABS]={"Items","Player","Island","Mods","About"};
static int s_tab, s_cur[NTABS], s_top[NTABS];
static int s_qty=1; static int s_to_storage;
static int s_typing; static int s_kx, s_ky;
static const char* KEYS[4]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ -'.","0123456789"};
static char s_status[96];
static int s_was_open, s_armed;
#define ROW0 0
#define LIST 18
#define MODLIST 17   /* mod rows that fit above the message line */

static void tab_items(void){
    if(!s_items_loaded) load_items();
    if(!s_match) refilter();
    if(g_player_state==0) aden_player_find();   /* spawning needs the player data */
    if(!s_nitems){ text(ROW0,C_WARN,"items.txt not found on SD card"); text(ROW0+1,C_DIM,"put it in sdmc/aden/items.txt"); return; }
    int* cur=&s_cur[TAB_ITEMS]; int* top=&s_top[TAB_ITEMS];
    put(ROW0,0,C_TEXT,"Search: %s%s      (%d found)", s_query, s_typing?"_":"", s_nmatch);
    if(s_typing){
        for(int r=0;r<4;r++){
            char b[72]; int n=0;
            for(int c=0;c<10;c++){ char ch=KEYS[r][c]; if(ch==' ')ch='_';
                n+=aden_snprintf(b+n,sizeof b-n," %c ",ch); }
            put(ROW0+2+r,0,C_TEXT,"%s",b);
            if(r==s_ky){ s_hl_row=ROW0+2+r; s_hl_key=s_kx; }
        }
        text(ROW0+7,C_DIM,"A type   Y delete   X clear   B done");
        if(up()) s_ky=(s_ky+3)%4; if(down()) s_ky=(s_ky+1)%4;
        if(left()) s_kx=(s_kx+9)%10; if(right()) s_kx=(s_kx+1)%10;
        if(pr(B_A) && s_qlen<22){ s_query[s_qlen++]=KEYS[s_ky][s_kx]; s_query[s_qlen]=0; refilter(); *cur=*top=0; }
        if(pr(B_Y) && s_qlen>0){ s_query[--s_qlen]=0; refilter(); *cur=*top=0; }
        if(pr(B_X)){ s_qlen=0; s_query[0]=0; refilter(); *cur=*top=0; }
        if(pr(B_B)||pr(B_PLUS)){ s_typing=0; s_press&=~(u64)(B_B|B_PLUS); }  /* B only leaves the keyboard */
        return;
    }
    if(up() && *cur>0) (*cur)--; if(down() && *cur<s_nmatch-1) (*cur)++;
    if(left()) { *cur-=LIST; if(*cur<0)*cur=0; } if(right()) { *cur+=LIST; if(*cur>s_nmatch-1)*cur=s_nmatch-1; }
    /* stackables go up to 40 full slots, the rest to 99; s_qty keeps the asked amount across items */
    Item* si= s_nmatch ? &s_items[s_match[*cur]] : 0;
    int mx= si && si->stack>1 ? 40*si->stack : 99, q= s_qty>mx ? mx : s_qty;
    if(pr(B_ZL) && q>1)  s_qty = q>100 ? q-100 : q>10 ? q-10 : q-1;
    if(pr(B_ZR) && q<mx){ s_qty = q>=100 ? q+100 : q>=10 ? q+10 : q+1; if(s_qty>mx) s_qty=mx; }
    q= s_qty>mx ? mx : s_qty;
    if(pr(B_Y)) { s_typing=1; return; }
    if(*cur<*top) *top=*cur; if(*cur>=*top+LIST) *top=*cur-LIST+1;
    for(int i=0;i<LIST && *top+i<s_nmatch;i++){
        Item* it=&s_items[s_match[*top+i]]; int sel=(*top+i==*cur);
        /* one print per row: name padded to a column, then id and category */
        char nm[40]; int k=0; for(;it->name[k]&&k<38;k++) nm[k]=it->name[k]; for(;k<38;k++) nm[k]=' '; nm[38]=0;
        put(ROW0+2+i,0,C_TEXT,"%s %04X %s",nm,it->id,it->cat);
        if(sel) s_hl_row=ROW0+2+i;
    }
    if(si && needs_param(si->id)) text(ROW0+LIST+3,C_DIM,"Needs a recipe ID - cannot be spawned here");
    else put(ROW0+LIST+3,0,C_TEXT,"Amount: %d (max %d)   Send to: %s",q,mx,s_to_storage?"home storage":"pockets");
    text(26,C_DIM,"A spawn  X pockets/storage  Y search  ZL/ZR amount  - rescan");
    /* who receives spawned items; Minus redoes the player search (not while the island thread reads the copies) */
    if(pr(B_MINUS) && g_player_state!=1 && g_island_state!=1){ s_status[0]=0; g_player_state=0; g_island_state=0; aden_player_find(); }
    if(g_player_state==2){ const char* nm=aden_player_name(aden_player_active());
        put(ROW0+LIST+5,0,C_DIM,"Items go to: %s   (%d copies%s)",nm[0]?nm:"your character",aden_player_copies(),aden_player_count()>1?", change in Player tab":""); }
    if(g_player_state<2) text(ROW0+LIST+5,C_DIM,"Finding your player data...");
    if(g_player_state==3){ text(ROW0+LIST+5,C_WARN,"Player data not found - press A to search again");
        if(pr(B_A)){ s_status[0]=0; aden_player_find(); return; } }   /* A retries instead of spawning */
    if(pr(B_X)) s_to_storage=!s_to_storage;
    if(pr(B_A) && s_nmatch){
        Item* it=si; const char* np=needs_param(it->id);
        if(np){ aden_snprintf(s_status,sizeof s_status,"%s",np); return; }
        int r= s_to_storage ? aden_storage_add(it->id,q,it->stack) : aden_pocket_add(it->id,q,it->stack);
        /* -1 player data not ready, -2 data looks wrong (nothing written) */
        if(r==-1) aden_snprintf(s_status,sizeof s_status,s_to_storage?"Home storage not ready yet - try again in a moment":"Player data not ready yet - try again in a moment");
        else if(r<0) aden_snprintf(s_status,sizeof s_status,s_to_storage?"Home storage looks wrong - nothing was added":"Pockets look wrong - nothing was added");
        else if(s_to_storage) aden_snprintf(s_status,sizeof s_status,r==0?"Home storage is full":r<q?"Sent %d x %s (home storage now full)":"Sent %d x %s to home storage",r,it->name);
        else if(r==0) aden_snprintf(s_status,sizeof s_status,"Your pockets are full");
        else if(r<q) aden_snprintf(s_status,sizeof s_status,"Added %d x %s (pockets now full)",r,it->name);
        else aden_snprintf(s_status,sizeof s_status,"Added %d x %s to your pockets",r,it->name);
    }
}
static void tab_player(void){
    int* cur=&s_cur[TAB_PLAYER];
    if(g_player_state==0) aden_player_find();
    if(g_player_state==1){ text(ROW0,C_TEXT,"Character stats"); text(ROW0+2,C_DIM,"Finding your player data..."); return; }
    if(g_player_state==3){ text(ROW0,C_TEXT,"Character stats"); text(ROW0+2,C_WARN,"Could not find player data."); text(ROW0+3,C_DIM,"Press A to search again."); if(pr(B_A)) g_player_state=0; return; }
    if(!aden_player_ready()){ text(ROW0+2,C_DIM,"Finding your player data..."); return; }
    char pn[24],tn[24]; aden_player_names(pn,tn,sizeof pn);
    if(pn[0]) put(ROW0,0,C_TEXT,"%s of %s",pn,tn); else text(ROW0,C_TEXT,"Character stats");
    enum { R_BELLS, R_MILES, R_BANK, R_POKI, R_RECIPES, R_REACT, R_BAGS, NROWS };
    /* islands with several residents: choose who to edit (row -1 = player picker) */
    int np=aden_player_count(), minrow = np>1 ? -1 : 0;
    if(*cur<minrow) *cur=minrow;
    if(up()&&*cur>minrow)(*cur)--; if(down()&&*cur<NROWS-1)(*cur)++;
    if(np>1){
        int ap=aden_player_active();
        put(ROW0+1,0,C_TEXT,"Editing: < %s >",aden_player_name(ap));
        put(ROW0+1,1,C_DIM,"%d residents",np);
        if(*cur==-1){
            s_hl_row=ROW0+1;
            if(left())  aden_player_select((ap+np-1)%np);
            if(right()) aden_player_select((ap+1)%np);
            text(ROW0+11,C_DIM,"Left/Right picks which resident to edit.");
            text(ROW0+12,C_DIM,"Items you spawn go to this resident too.");
        }
    }
    if(*cur>=0) s_hl_row=ROW0+2+*cur;
    static const char* LBL[4]={"Bells in wallet","Nook Miles","Bank savings","Poki"};
    for(int i=0;i<4;i++){
        put(ROW0+2+i,0,C_TEXT,"%s",LBL[i]);
        if(i==0) put(ROW0+2+i,1,C_TEXT,"%u",aden_wallet_get());
        else if(aden_stat_ok(i-1)) put(ROW0+2+i,1,C_TEXT,"%u",aden_stat_get(i-1));
        else put(ROW0+2+i,1,C_DIM,"not found");
    }
    put(ROW0+2+R_RECIPES,0,C_TEXT,"Learn every DIY recipe"); put(ROW0+2+R_RECIPES,1,C_TEXT,"%d / %d",aden_recipes_known(),aden_recipes_total());
    put(ROW0+2+R_REACT,0,C_TEXT,"Learn every reaction");    put(ROW0+2+R_REACT,1,C_TEXT,"%d known",aden_reactions_known());
    put(ROW0+2+R_BAGS,0,C_TEXT,"Fill pockets with 99,000 Bell bags");
    int H=ROW0+11;
    if(*cur<0){ }
    else if(*cur<=R_POKI){
        static const s32 STEP[4]={1000,1000,100000,100}, BIG[4]={10000,10000,1000000,1000};
        static const u32 CAP[4]={99999,999999,999999999,999999};
        int f=*cur-1;
        if(*cur>0 && !aden_stat_ok(f)){ text(H,C_DIM,"This value was not found in memory."); }
        else {
            s64 old=*cur==0?aden_wallet_get():aden_stat_get(f), v=old;
            if(left())  v-=STEP[*cur]; if(right()) v+=STEP[*cur];
            if(pr(B_ZL)) v-=BIG[*cur];  if(pr(B_ZR)) v+=BIG[*cur];
            if(pr(B_X)) v=CAP[*cur]; if(pr(B_Y)) v=0;
            if(v<0) v=0; if(v>(s64)CAP[*cur]) v=CAP[*cur];
            if(v!=old){ if(*cur==0) aden_wallet_set((u32)v); else aden_stat_set(f,(u32)v); }
            put(H,0,C_DIM,"Left/Right +-%d   ZL/ZR +-%d",STEP[*cur],BIG[*cur]);
            put(H+1,0,C_DIM,"X max (%u)   Y zero",CAP[*cur]);
            if(*cur==R_POKI) text(H+2,C_DIM,"Poki is the Happy Home Paradise currency.");
        }
    } else if(*cur==R_RECIPES){
        text(H,C_DIM,"A adds every DIY recipe to your recipe book.");
        text(H+1,C_DIM,"New ones show up marked as new.");
        if(pr(B_A)){ int n=aden_recipes_learn_all(); aden_snprintf(s_status,sizeof s_status,n<0?"Recipe book not found":(n?"Learned %d new recipes":"You already know every recipe"),n); }
    } else if(*cur==R_REACT){
        text(H,C_DIM,"A adds every reaction to your reaction list.");
        if(pr(B_A)){ int n=aden_reactions_learn_all(); aden_snprintf(s_status,sizeof s_status,n<0?"Reactions not found":(n?"Learned %d new reactions":"You already know every reaction"),n); }
    } else {
        text(H,C_DIM,"A fills every empty pocket slot.");
        if(pr(B_A)){ int n=aden_pocket_add(0x08A4,40,1); aden_snprintf(s_status,sizeof s_status,n>0?"Added %d Bell bags":n==0?"Your pockets are full":n==-1?"Player data not ready yet - try again in a moment":"Pockets look wrong - nothing was added",n); }
    }
}
static void tab_island(void){
    int* cur=&s_cur[TAB_ISLAND];
    text(ROW0,C_TEXT,"Island");
    if(g_player_state==0) aden_player_find();
    if(g_player_state!=2){ text(ROW0+2,C_DIM,"Finding your player data first..."); return; }
    if(g_island_state==0) aden_island_find();
    if(g_island_state==1){ text(ROW0+2,C_DIM,"Finding your island data..."); return; }
    if(g_island_state==3){ text(ROW0+2,C_WARN,"Could not find island data."); text(ROW0+3,C_DIM,"Press A to search again."); if(pr(B_A)) g_island_state=0; return; }
    if(!aden_island_ready()){ text(ROW0+2,C_DIM,"Finding your island data..."); return; }
    int vk[10], nv=0; const char* vn[10]; int vf[10], vm[10];
    for(int k=0;k<10;k++) if(aden_villager_info(k,&vn[nv],&vf[nv],&vm[nv])) vk[nv++]=k;
    int NROWS=3+nv;
    if(up()&&*cur>0)(*cur)--; if(down()&&*cur<NROWS-1)(*cur)++;
    s_hl_row=ROW0+2+*cur + (*cur>=3 ? 1 : 0);
    put(ROW0+2,0,C_TEXT,"Turnip sell price (all week)"); put(ROW0+2,1,C_TEXT,"%u",aden_turnip_sell());
    put(ROW0+3,0,C_TEXT,"Turnip buy price (Daisy Mae)"); put(ROW0+3,1,C_TEXT,"%u",aden_turnip_buy());
    put(ROW0+4,0,C_TEXT,"Max friendship with everyone");
    put(ROW0+5,0,C_DIM,"Villagers (%d)",nv); put(ROW0+5,1,C_DIM,"friendship");
    for(int i=0;i<nv;i++){
        put(ROW0+6+i,0,C_TEXT,"%s%s",vn[i],vm[i]?"  (moving out)":"");
        if(vf[i]<0) put(ROW0+6+i,1,C_DIM,"-"); else put(ROW0+6+i,1,C_TEXT,"%d",vf[i]);
    }
    int H=ROW0+17;
    if(*cur<2){
        int sell=*cur==0; s64 old=sell?aden_turnip_sell():aden_turnip_buy(), v=old;
        if(left()) v-=10; if(right()) v+=10; if(pr(B_ZL)) v-=100; if(pr(B_ZR)) v+=100;
        if(pr(B_X)) v= sell?660:90; if(pr(B_Y)) v=0;
        if(v<0) v=0; if(v>999) v=999;
        if(v!=old) aden_turnip_set(sell,(u32)v);
        text(H,C_DIM,"Left/Right +-10   ZL/ZR +-100");
        text(H+1,C_DIM,sell?"X best natural price (660)   Y zero":"X cheapest natural price (90)   Y zero");
        text(H+2,C_DIM,"Sell prices apply to every half-day this week.");
    } else if(*cur==2){
        text(H,C_DIM,"A sets every villager's friendship to max (255).");
        if(pr(B_A)){ for(int i=0;i<nv;i++) aden_villager_friend(vk[i],255); aden_snprintf(s_status,sizeof s_status,"Every villager is now your best friend"); }
    } else {
        int i=*cur-3;
        text(H,C_DIM,"A max friendship   Y reset friendship");
        text(H+1,C_DIM,"X toggles moving out (they leave after a save)");
        if(pr(B_A)){ aden_villager_friend(vk[i],255); aden_snprintf(s_status,sizeof s_status,"%s is now your best friend",vn[i]); }
        if(pr(B_Y)){ aden_villager_friend(vk[i],25); aden_snprintf(s_status,sizeof s_status,"%s friendship reset",vn[i]); }
        if(pr(B_X)){ aden_villager_moving(vk[i],!vm[i]); aden_snprintf(s_status,sizeof s_status,vm[i]?"%s is staying":"%s is now moving out",vn[i]); }
    }
}
static void tab_mods(void){
    if(!s_mods_scanned) scan_mods();
    int* cur=&s_cur[TAB_MODS]; int* top=&s_top[TAB_MODS];
    text(ROW0,C_TEXT,"Mod manager");
    text(ROW0+1,C_DIM,"Mods go in sdmc/aden/mods/<name>/romfs/...");
    text(ROW0+2,C_DIM,"A turns a mod on/off. Applies when the game restarts.");
    if(s_nmods==0){ text(ROW0+4,C_TEXT,"No mods yet. Copy a romfs mod folder into"); text(ROW0+5,C_TEXT,"sdmc/aden/mods, then reopen this tab."); }
    if(up()&&*cur>0)(*cur)--; if(down()&&*cur<s_nmods-1)(*cur)++;
    if(*cur<*top) *top=*cur; if(*cur>=*top+MODLIST) *top=*cur-MODLIST+1;
    for(int i=0;i<MODLIST && *top+i<s_nmods;i++){
        Mod* m=&s_mods[*top+i];
        const char* kind = m->romfs ? (m->exefs?"romfs only":"") : (m->exefs?"code mod: use Eden":"no romfs folder");
        put(ROW0+4+i,0,C_TEXT,"[%s] %s",m->on?"ON ":"off",m->name);
        if(m->on!=m->on_at_start) put(ROW0+4+i,1,C_WARN,"restart needed");
        else if(kind[0]) put(ROW0+4+i,1,C_DIM,"%s",kind);
        if(*top+i==*cur) s_hl_row=ROW0+4+i;
    }
    if(pr(B_A) && s_nmods){
        Mod* m=&s_mods[*cur];
        if(!m->romfs){ aden_snprintf(s_modmsg,sizeof s_modmsg, m->exefs?"%s is a code mod - turn it on in Eden instead":"%s has no romfs folder",m->name); }
        else {
            m->on=!m->on; aden_mod_set(m->name,m->on);
            aden_snprintf(s_modmsg,sizeof s_modmsg,"%s is now %s - restart the game to apply",m->name,m->on?"ON":"off");
        }
    }
    if(pr(B_Y)){ scan_mods(); if(*cur>s_nmods-1) *cur=s_nmods>0?s_nmods-1:0; if(*top>*cur) *top=*cur;   /* the list may have shrunk */
        aden_snprintf(s_modmsg,sizeof s_modmsg,"%d mods found",s_nmods); }
    text(26,C_DIM,"A on/off   Y rescan folder   B close");
    if(s_modmsg[0]) text(ROW0+22,C_DIM,s_modmsg);
}
static void tab_about(void){
    text(ROW0,C_TEXT,"Aden Modmenu v" ADEN_VERSION " for ACNH 3.0.2 / 3.0.3");
    text(ROW0+2,C_DIM,"Open from the Nook Phone. B closes.");
    text(ROW0+3,C_DIM,"Your character is paused while it is open.");
}

/* panel geometry (720p units, origin centre, y up) */
#define PX0  -625.f
#define PW    570.f
#define PY0   350.f
#define PH    700.f
#define HEADH 100.f
#define TX   (PX0+30.f)
#define CY   (PY0-HEADH-16.f)

static void draw_chrome(void){
    gfx_prim_begin();
    gfx_rrect(PX0,PY0,PW,PH,34,C_PANEL);                 /* cream card */
    gfx_rrect(PX0,PY0,PW,HEADH,34,C_HEAD);               /* teal header */
    gfx_rect(PX0,PY0-HEADH+34,PW,34,C_HEAD);             /* square off its lower corners */
    /* active tab pill */
    float tx=TX-6;
    for(int i=0;i<NTABS;i++){
        float w=strlen(TABNAME[i])*8.f+28;
        if(i==s_tab) gfx_rrect(tx,PY0-58,w,26,13,0xFFFFFFFF);
        tx+=w+8;
    }
    if(s_hl_row>=0){
        if(s_hl_key>=0) gfx_rrect(TX+s_hl_key*24-2,CY-s_hl_row*20+3,28,22,11,C_PILL);
        else            gfx_rrect(PX0+16,CY-s_hl_row*20+3,PW-32,22,11,C_PILL);
    }
    gfx_rect(PX0+20,CY-25*20+6,PW-40,2,C_LINE);          /* footer divider */
}
static void draw_tabs_text(void){
    float tx=TX-6;
    for(int i=0;i<NTABS;i++){
        float w=strlen(TABNAME[i])*8.f+28;
        gfx_text(tx+14,PY0-63,1.0f,i==s_tab?C_HEADDK:C_TABOFF,TABNAME[i]);
        tx+=w+8;
    }
    gfx_text(PX0+PW-60,PY0-63,1.0f,C_TABOFF,"L/R");
}

void aden_menu_draw(void){
    extern volatile int g_aden_open_request;
    if(g_aden_open_request && !g_menu_open){ g_menu_open=1; s_armed=0; }
    /* after closing, give input back to the game only once the closing B (and all else) is released */
    if(!g_menu_open){ s_was_open=0; if(g_input_hold && aden_input_take()==0) g_input_hold=0; return; }
    if(!s_was_open){ s_was_open=1; aden_log("menu opened"); }
    poll();
    /* ignore input until every button from the phone tap has been released */
    if(!s_armed){ if(s_now==0) s_armed=1; s_press=0; }
    if(!s_typing){
        if(pr(B_L)) s_tab=(s_tab+NTABS-1)%NTABS;
        if(pr(B_R)) s_tab=(s_tab+1)%NTABS;
    }
    s_hl_row=-1; s_hl_key=-1;
    switch(s_tab){
        case TAB_ITEMS:  tab_items(); break;
        case TAB_PLAYER: tab_player(); break;
        case TAB_ISLAND: tab_island(); break;
        case TAB_MODS:   tab_mods(); break;
        case TAB_ABOUT:  tab_about(); break;
    }
    if(s_status[0]) put(ROW0+LIST+4,0,C_WARN,"%s",s_status);
    draw_chrome();
    gfx_text(TX,PY0-18,1.6f,C_TITLE,"Aden Modmenu");
    draw_tabs_text();
    flush(TX,CY);
    if(pr(B_B) && !s_typing){ g_input_hold=1; g_menu_open=0; g_aden_open_request=0; s_status[0]=0; aden_log("menu closed"); }
}
