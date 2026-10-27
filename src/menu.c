#include "nn.h"
/* Aden Modmenu - on-screen UI. Coordinates: 1280x720, origin at screen centre, y up. */
void gfx_text(float x, float y, float scale, u32 rgba, const char* s);
void gfx_prim_begin(void);
void gfx_rect(float x,float y,float w,float h,u32 rgba);
void gfx_rrect(float x,float y,float w,float h,float r,u32 rgba);
void gfx_disk(float cx,float cy,float r,u32 rgba);
static int s_hl_row=-1, s_hl_key=-1;   /* highlight pill: content row, keyboard column */
static void (*s_paint)(void);           /* the tab's shapes, drawn over the panel and under the text */
static int s_status_row;                /* where the status line goes this frame */
u64  aden_input_take(void);
int aden_player_ready(void); int aden_player_copies(void); void aden_player_find(void); void aden_player_find_full(void);
int aden_stat_ok(int); u32 aden_stat_get(int); void aden_stat_set(int,u32);
void aden_player_names(char*,char*,int);
int aden_player_count(void); int aden_player_active(void); const char* aden_player_name(int); void aden_player_select(int);
int aden_recipes_learn_all(void); int aden_recipes_known(void); int aden_recipes_total(void);
int aden_reactions_learn_all(void); int aden_reactions_known(void);
int aden_storage_add(u16,int,int,u16); int aden_storage_free(void);
extern volatile int g_island_state; void aden_island_find(void); int aden_island_ready(void);
u32 aden_turnip_sell(void); u32 aden_turnip_buy(void); void aden_turnip_set(int,u32);
int aden_villager_info(int,const char**,int*,int*); void aden_villager_friend(int,u8); void aden_villager_moving(int,int);
int aden_villager_species(int); int aden_turnip_week(u32*);
int aden_weeds_count(void); int aden_weeds_clear(void);
int aden_pocket_free(void); int aden_pocket_slots(void); int aden_reactions_total(void);
u32 aden_wallet_get(void); void aden_wallet_set(u32); int aden_pocket_add(u16 id,int qty,int stack,u16 var);
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

/* panel geometry (720p units, origin centre, y up) */
#define PX0  -625.f
#define PW    570.f
#define PY0   350.f
#define PH    700.f
#define HEADH 100.f
#define TX   (PX0+30.f)
#define CY   (PY0-HEADH-16.f)
#define TABX0 (TX+8.f)      /* x of the first tab name */
/* panel coordinates: x right from the panel's left edge, y down from its top */
#define WX(x) (PX0+(x))
#define WY(y) (PY0-(y))
static void box(float x,float y,float w,float h,float r,u32 c){ gfx_rrect(WX(x),WY(y),w,h,r,c); }
static void dot(float x,float y,float r,u32 c){ gfx_disk(WX(x),WY(y),r,c); }

/* ---------- text canvas ----------
   Text sits on a grid of 8 px columns (the debug font is monospaced) and 20 px rows, from
   (TX, CY). Each colour is ONE multi-line print padded with spaces, so a whole screen costs one
   print per colour: the debug font allows only 16 prints per frame. In panel coordinates column
   c starts at x = GX(c) and row r at y = GY(r); its letters are centred about GY(r)+8. */
#define PX 8.0f
#define LH 20.0f
#define MAXROWS 32
#define CVW 72
#define NINK 8
#define GX(c) (30.f+8.f*(c))
#define GY(r) (116.f+20.f*(r))
typedef struct { u32 color; u8 len[MAXROWS]; char row[MAXROWS][CVW]; } Ink;
static Ink s_ink[NINK]; static int s_nink;
static void at_s(int row,int col,u32 c,const char* t){
    if(row<0||row>=MAXROWS||col<0) return;
    Ink* k=0; for(int i=0;i<s_nink;i++) if(s_ink[i].color==c) k=&s_ink[i];
    if(!k){ if(s_nink>=NINK) return; k=&s_ink[s_nink++]; k->color=c; memset(k->len,0,sizeof k->len); }
    for(int i=0;t[i] && col+i<CVW;i++){
        int x=col+i; while(k->len[row]<x) k->row[row][k->len[row]++]=' ';
        k->row[row][x]=t[i]; if(k->len[row]<=x) k->len[row]=(u8)(x+1);
    }
}
/* text at row, column (8 px each) */
static void at(int row,int col,u32 c,const char* f,...){
    char b[CVW+1]; __builtin_va_list ap; __builtin_va_start(ap,f); aden_vsnprintf(b,sizeof b,f,ap); __builtin_va_end(ap);
    at_s(row,col,c,b);
}
/* the same, returning its length */
static int at_n(int row,int col,u32 c,const char* f,...){
    char b[CVW+1]; __builtin_va_list ap; __builtin_va_start(ap,f); aden_vsnprintf(b,sizeof b,f,ap); __builtin_va_end(ap);
    at_s(row,col,c,b); return (int)strlen(b);
}
/* the same, ending at column `end` (right-aligned) */
static void at_r(int row,int end,u32 c,const char* f,...){
    char b[CVW+1]; __builtin_va_list ap; __builtin_va_start(ap,f); aden_vsnprintf(b,sizeof b,f,ap); __builtin_va_end(ap);
    at_s(row,end-(int)strlen(b),c,b);
}
static const int COLC[2]={0,41};   /* put() columns: labels, values */
static void put(int row,int col,u32 c,const char* f,...){
    char b[CVW+1]; __builtin_va_list ap; __builtin_va_start(ap,f); aden_vsnprintf(b,sizeof b,f,ap); __builtin_va_end(ap);
    at_s(row,COLC[col&1],c,b);
}
static void flush(float x,float y){
    static char buf[2040];
    for(int i=0;i<s_nink;i++){
        Ink* k=&s_ink[i]; int last=-1, n=0;
        for(int r=0;r<MAXROWS;r++) if(k->len[r]) last=r;
        for(int r=0;r<=last;r++){
            int m=k->len[r]; if(n+m+2>(int)sizeof buf) break;
            memcpy(buf+n,k->row[r],m); n+=m; buf[n++]='\n';
        }
        buf[n]=0;
        if(n) gfx_text(x,y,1.0f,k->color,buf);
    }
    s_nink=0;
}
/* larger text (one print each), panel coordinates of its top-left corner */
typedef struct { float x,y,s; u32 c; char t[24]; } Big;
static Big s_big[6]; static int s_nbig;
static void big(float x,float y,float sc,u32 c,const char* f,...){
    if(s_nbig>=6) return; Big* b=&s_big[s_nbig++]; b->x=x; b->y=y; b->s=sc; b->c=c;
    __builtin_va_list ap; __builtin_va_start(ap,f); aden_vsnprintf(b->t,sizeof b->t,f,ap); __builtin_va_end(ap);
}
static void flush_big(void){ for(int i=0;i<s_nbig;i++) gfx_text(WX(s_big[i].x),WY(s_big[i].y),s_big[i].s,s_big[i].c,s_big[i].t); s_nbig=0; }
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

/* ---------- item database (sd:/aden/items.txt) ----------
   columns: hex id, category, name, max stack, family (F C N M O), dlc (- H P S), group, body, fabric;
   all after the name are optional. group: the other colours of a clothing item follow its first
   colour and give its id here; they share one grid tile (sub set, the first colour counts them in
   ncol). body, fabric: remake variants "<part>|<variant 0>|<variant 1>|..." */
typedef struct { const char* cat; const char* name; const char* body; const char* fab; u16 id, stack; char fam, dlc; u8 sub, ncol; } Item;
static Item* s_items; static int s_nitems; static int s_items_loaded; static char* s_itembuf;
/* add-on content the player owns: Happy Home Paradise (index 1000), Pocket Camp ticket (300), Switch Online items (301) */
static int s_own_h, s_own_p, s_own_s;
static void check_dlc(void){
    int n=nn_aoc_CountAddOnContent(), idx[32], k= n>0 ? nn_aoc_ListAddOnContent(idx,0,n<32?n:32) : 0;
    for(int i=0;i<k;i++){ if(idx[i]==1000) s_own_h=1; if(idx[i]==300) s_own_p=1; if(idx[i]==301) s_own_s=1;
        aden_log("add-on content index %d", idx[i]); }
    aden_log("add-on content: %d owned (Happy Home Paradise %d, Pocket Camp %d, Switch Online items %d)", n, s_own_h, s_own_p, s_own_s);
}
static int dlc_ok(char d){ return d=='H' ? s_own_h : d=='P' ? s_own_p : d=='S' ? s_own_s : 1; }
static u32 hexv(const char* h){ u32 v=0; for(;*h;h++){ v<<=4; v|= (*h<='9')?*h-'0':((*h|32)-'a'+10); } return v; }
static void load_items(void){
    s_items_loaded=1;
    check_dlc();
    FsFile f; if(nn_fs_OpenFile(&f,"adensd:/aden/items.txt",FS_READ)){ aden_log("items.txt missing"); return; }
    s64 sz=0; nn_fs_GetFileSize(&sz,f);
    s_itembuf=malloc(sz+1); if(!s_itembuf){ nn_fs_CloseFile(f); return; }
    nn_fs_ReadFile(f,0,s_itembuf,sz); nn_fs_CloseFile(f); s_itembuf[sz]=0;
    int lines=0, hidden=0; for(s64 i=0;i<sz;i++) if(s_itembuf[i]=='\n') lines++;
    s_items=malloc(sizeof(Item)*(lines+1));
    char* p=s_itembuf; int lead=-1;
    while(*p){
        char* nl=p; while(*nl && *nl!='\n') nl++;
        char* line=p; p=*nl?nl+1:nl; *nl=0;
        if(nl>line && nl[-1]=='\r') nl[-1]=0;   /* CRLF files */
        if(line[0]=='#'||!line[0]) continue;
        char* fl[9]; int nf=0; char* q=line;
        while(nf<9){ fl[nf++]=q; while(*q && *q!='\t') q++; if(!*q) break; *q++=0; }
        if(nf<3) continue;
        int st=1; if(nf>3){ st=0; for(char* d=fl[3];*d>='0'&&*d<='9';d++) st=st*10+(*d-'0'); if(st<1) st=1; }
        char fam= nf>4 && fl[4][0] ? fl[4][0] : 'O', dl= nf>5 && fl[5][0] ? fl[5][0] : '-';
        if(!dlc_ok(dl)){ hidden++; continue; }   /* items from DLC the player does not own are never listed */
        u32 id=hexv(line);
        /* another colour of the item before it (if the first colour is hidden, this one stands alone) */
        int sub= nf>6 && fl[6][0] && fl[6][0]!='-' && lead>=0 && s_items[lead].id==hexv(fl[6]) && s_items[lead].ncol<255;
        if(sub) s_items[lead].ncol++; else lead=s_nitems;
        const char* body= nf>7 && fl[7][0] && st==1 ? fl[7] : 0;
        const char* fab = nf>8 && fl[8][0] && st==1 ? fl[8] : 0;
        s_items[s_nitems++]=(Item){fl[1],fl[2],body,fab,(u16)id,(u16)st,fam,dl,(u8)sub,1};
    }
    aden_log("loaded %d items, %d hidden (DLC not owned)", s_nitems, hidden);
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
/* Items tab family filter (ZL/ZR): codes from items.txt column 5 */
#define NFAM 6
static const char  FAMC[NFAM]={0,'F','C','N','M','O'};
static const char* FAMN[NFAM]={"All","Furniture","Clothing","Nature","Materials","Other"};
static const u32   FAMCOL[NFAM]={0xA39380FF,0xE0A24AFF,0xE07A9BFF,0x7DBF5AFF,0x9C7A55FF,0xA39380FF};
static int s_fam;
static int fam_idx(char c){ for(int i=1;i<NFAM;i++) if(FAMC[i]==c) return i; return NFAM-1; }
/* one entry per grid tile: an item, or the first colour of a clothing item when any colour matches */
static void refilter(void){
    if(!s_match) s_match=malloc(sizeof(int)*(s_nitems+1));
    s_nmatch=0;
    for(int i=0;i<s_nitems;i++){
        Item* it=&s_items[i]; if(it->sub || (s_fam && it->fam!=FAMC[s_fam])) continue;
        int hit=0; for(int k=0;k<it->ncol && !hit;k++) hit=contains(it[k].name,s_query);
        if(hit) s_match[s_nmatch++]=i;
    }
}
/* length of a clothing item's name without its colour ("tee (Blue)" -> 3); other items: the whole name */
static int base_len(const Item* it){
    int n=(int)strlen(it->name);
    if((it->ncol>1 || it->sub) && it->name[n-1]==')') for(int i=n-2;i>0;i--) if(it->name[i]==' ' && it->name[i+1]=='(') return i;
    return n;
}
/* remake variants "<part>|<v0>|<v1>|...": how many there are, and field k (0 the part, k+1 variant k) */
static int vcount(const char* s){ int n=0; if(s) for(;*s;s++) n+= *s=='|'; return n; }
static void vname(const char* s,int k,char* o,int cap){
    int f=0, j=0;
    for(;s && *s && f<=k;s++){ if(*s=='|'){ f++; continue; } if(f==k && j<cap-1) o[j++]=*s; }
    o[j]=0;
}

/* ---------- mods: folders in sd:/aden/mods, on/off kept in sd:/aden/mods_on.txt ---------- */
int aden_mod_is_on(const char*); void aden_mod_set(const char*,int);
int aden_stack99_on(void); int aden_stack99_found(void); int aden_stack99_set(int);
int aden_split_stacks(int (*maxof)(u16), int* need, int* have);
/* normal max stack of an item (items.txt is built from the unmodded ItemKind table); 0 if unknown */
static int item_stack(u16 id){
    if(!s_items_loaded) load_items();
    for(int i=0;i<s_nitems;i++) if(s_items[i].id==id) return s_items[i].stack;
    return 0;
}
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
static int s_tab, s_cur[NTABS];
static int s_qty=1; static int s_to_storage;
/* give sheet: A on a grid tile opens it; the amount starts at 1 each time it opens */
static int s_sheet, s_sh_item, s_sh_col, s_sh_body, s_sh_fab, s_sh_row;
static int s_typing; static int s_kx, s_ky;
static const char* KEYS[4]={"ABCDEFGHIJ","KLMNOPQRST","UVWXYZ -'.","0123456789"};
static char s_status[96];
static int s_was_open, s_armed;
#define ROW0 0
#define LIST 18

/* pocket-style grid: GCOLS x GROWS tiles per page. tab_items fills the tile state, draw_grid
   draws the tiles (primitives) and draw_grid_text the labels: one print per column, since the
   debug font allows only 16 prints per frame. */
#define GCOLS 5
#define GROWS 4
#define GPAGE (GCOLS*GROWS)
#define GLINE 11                   /* label characters per line (tile width / 8) */
/* grid geometry, panel coordinates (x right from PX0, y down from PY0) */
#define GX0   40.f
#define GY0   152.f
#define GTILE 90.f
#define GPITCH 100.f        /* 5 label lines of 20 px: one print per column lines up with every row */
static int s_grid_on, s_grid_n, s_grid_sel, s_grid_fam[GPAGE], s_grid_nv[GPAGE];
/* the item grid has the search bar (s_grid_bar); the give sheet's variant grid sits lower (s_grid_y0)
   and marks the picked tile in teal while the cursor is in another zone (s_grid_cur 0) */
static int s_grid_bar, s_grid_cur=1; static float s_grid_y0, s_pill_top; static u16 s_grid_var[GPAGE];
static u16 s_grid_id[GPAGE]; static char s_grid_fc[GPAGE];
void aden_icons_want(int n,const u16* ids,const char* fams,const u16* vars); int aden_icon_draw(u16 id,char fam,u16 var,float x,float y,float w,float h,int mode);
int aden_icons_on(void); int aden_icon_mode(void); void aden_icon_mode_set(int); void aden_icons_enable(void); void aden_icons_disable(void);
int aden_icon_ready(u16 id,char fam,u16 var);
/* Icons are drawn last, after every box and text of the frame: their shader program replaces
   the one the boxes use. icon_at queues the icon and says if it will be drawn. */
typedef struct { u16 id, var; char fam; s8 mode; float x,y,w,h; } IconQ;
static IconQ s_iq[24]; static int s_niq;
static int icon_at(u16 id,char fam,u16 var,float x,float y,float w,float h,int mode){
    if(s_niq>=24 || !aden_icon_ready(id,fam,var)) return 0;
    s_iq[s_niq++]=(IconQ){id,var,fam,(s8)mode,x,y,w,h}; return 1;
}
static char s_glab[GCOLS][GROWS*(2*GLINE+8)+1];
static char s_famtxt[24];   /* text of the teal pill right of the bar (empty: no pill) */
/* tile labels, two lines each: one print per column, 5 lines (100 px) per tile row */
static void grid_labels(char la[][GLINE+1],char lb[][GLINE+1]){
    for(int c=0;c<GCOLS;c++){
        char* o=s_glab[c]; int n=0, cap=(int)sizeof s_glab[c];
        for(int r=0;r<GROWS;r++){ int i=r*GCOLS+c;
            n+=aden_snprintf(o+n,cap-n,"%s\n%s\n\n\n\n", i<s_grid_n?la[i]:"", i<s_grid_n?lb[i]:""); }
    }
}
/* copy at most GLINE characters, ending with '.' when the text is cut */
static void fit(char* o,const char* s,int n){
    int k=0; for(;k<n && k<GLINE;k++) o[k]=s[k];
    if(n>GLINE) o[GLINE-1]='.';
    o[k]=0;
}
/* split a name over two label lines: "base (Variant)" keeps the variant on line 2,
   other names break at a space or after a hyphen when they can */
static void wrap2(const char* s,char* a,char* b){
    int n=(int)strlen(s), cut=n;
    for(int i=0;i+1<n;i++) if(s[i]==' ' && s[i+1]=='(' && s[n-1]==')'){ fit(a,s,i); fit(b,s+i+2,n-i-3); return; }
    if(n>GLINE){ cut=GLINE; for(int i=GLINE;i>0;i--){ if(s[i]==' '){ cut=i; break; } if(s[i-1]=='-'){ cut=i; break; } } }
    int k=0; for(;k<cut;k++) a[k]=s[k]; a[k]=0;
    const char* r=s+cut; while(*r==' ') r++;
    int m=(int)strlen(r); for(k=0;k<m && k<GLINE;k++) b[k]=r[k];
    if(m>GLINE) b[GLINE-1]='.';
    b[k]=0;
}

/* ---------- loading page: every place a search looks, one row each ----------
   Shown instead of the tab while the player (or island) search runs or after it failed,
   and for a moment after it succeeds so the green checks can be read. */
#define C_OK 0x5FB55AFF
typedef struct { u8 row, st; } Icon;
static Icon s_icons[NSTEPS]; static int s_nicons; static u32 s_frame;
static int s_seen_search; static u64 s_done_at;
#define HOLD_TICKS (19200000ull*6/5)   /* 1.2 s at 19.2 MHz */
static const char* const STEPNAME[NSTEPS]={"Game memory","Graphics memory","Nook Miles, bank, Poki","Personal save","Island save"};
static void step_row(int row,int i,int st){
    u32 n=g_step_n[i]; char d[40];
    if(s_nicons<NSTEPS) s_icons[s_nicons++]=(Icon){(u8)row,(u8)st};
    put(row,0,st==ST_WAIT||st==ST_SKIP?C_DIM:C_TEXT,"   %s",STEPNAME[i]);
    switch(st){
        case ST_RUN:  if(i==STEP_STATS||i==STEP_PERS) aden_snprintf(d,sizeof d,"checking");
                      else aden_snprintf(d,sizeof d,"%u MB read",g_scan_mb); break;
        case ST_OK:   if(i==STEP_MEM||i==STEP_GPU) aden_snprintf(d,sizeof d,"%u player%s",n,n==1?"":"s");
                      else if(i==STEP_STATS) aden_snprintf(d,sizeof d,"%u of 3",n);
                      else aden_snprintf(d,sizeof d,"%u cop%s",n,n==1?"y":"ies"); break;
        case ST_FAIL: aden_snprintf(d,sizeof d,"not found"); break;
        case ST_SKIP: aden_snprintf(d,sizeof d,"not needed"); break;
        default:      aden_snprintf(d,sizeof d,"waiting"); break;
    }
    put(row,1,st==ST_FAIL?C_WARN:C_DIM,"%s",d);
}
/* returns 1 when it drew the page (the caller skips its tab).
   island: 0 player only, 1 player and island, 2 the same but X goes on without the island */
static int s_isl_skip;
static int loading_page(int island){
    int ps=g_player_state, is=g_island_state;
    int pfail= ps==3, ifail= island && ps==2 && is==3 && !(island==2 && s_isl_skip);
    int busy= !pfail && !ifail && (ps<2 || (island && is<2));
    if(!busy && !pfail && !ifail){
        if(!s_seen_search) return 0;
        if(!s_done_at) s_done_at=nn_os_GetSystemTick();
        if(nn_os_GetSystemTick()-s_done_at>HOLD_TICKS){ s_seen_search=0; s_done_at=0; return 0; }
    } else { s_done_at=0; if(busy) s_seen_search=1; }
    const char* who= island && ps==2 ? "island" : "player";
    if(pfail||ifail) put(ROW0,0,C_WARN,"Your %s data was not found",who);
    else if(busy) put(ROW0,0,C_TEXT,"Looking for your %s data",who);
    else if(island) text(ROW0,C_TEXT,"Island data found");
    else { const char* nm=aden_player_name(aden_player_active()); put(ROW0,0,C_TEXT,"Found %s",nm[0]?nm:"your player"); }
    if(busy) text(ROW0+1,C_DIM,"Reading the game's memory - a few seconds.");
    int last= island ? STEP_ISLAND : STEP_PERS;
    for(int i=0;i<=last;i++){
        int st=__atomic_load_n(&g_step[i],__ATOMIC_ACQUIRE);
        if(i<STEP_ISLAND && ps==0) st=ST_WAIT;         /* a new search is about to reset the rows */
        if(i==STEP_ISLAND && is==0) st=ST_WAIT;
        step_row(ROW0+3+i*2,i,st);
    }
    if(pfail||ifail){
        text(ROW0+15,C_TEXT,"Load your island (not the title screen),");
        text(ROW0+16,C_TEXT,"then press A to search again.");
        text(ROW0+18,C_DIM,"Details: sdmc/aden/log.txt");
        if(ifail && island==2) text(ROW0+20,C_DIM,"X: use Items without the island data");
        if(ifail && island==2 && pr(B_X)){ s_isl_skip=1; s_press&=~(u64)B_X; }
        if(pr(B_A)){ s_status[0]=0; if(pfail) aden_player_find(); else { g_island_state=0; s_isl_skip=0; } }
    }
    return 1;
}
static const float SPIN[8][2]={{7,0},{4.95f,-4.95f},{0,-7},{-4.95f,-4.95f},{-7,0},{-4.95f,4.95f},{0,7},{4.95f,4.95f}};
static const float CHK[6][2]={{-4,0},{-2.5f,-1.5f},{-1,-3},{0.6f,-1.2f},{2.2f,0.7f},{3.8f,2.6f}};
static u32 mix(u32 a,u32 b,int t){   /* t/8 of the way from a to b, opaque */
    u32 r=0; for(int s=8;s<32;s+=8){ int x=(a>>s)&255, y=(b>>s)&255; r|=(u32)(x+(y-x)*t/8)<<s; } return r|255;
}
static void draw_icon(float cx,float cy,int st){
    switch(st){
        case ST_RUN:   /* eight dots, the bright one going round clockwise */
            for(int k=0;k<8;k++){ int d=((int)(s_frame/4)-k+64)&7; gfx_disk(cx+SPIN[k][0],cy+SPIN[k][1],2.2f,mix(C_HEADDK,C_LINE,d)); } break;
        case ST_OK:   gfx_disk(cx,cy,9,C_OK); for(int k=0;k<6;k++) gfx_disk(cx+CHK[k][0],cy+CHK[k][1],1.6f,0xFFFFFFFF); break;
        case ST_FAIL: gfx_disk(cx,cy,9,C_WARN); for(int k=-2;k<=2;k++){ gfx_disk(cx+k*1.6f,cy+k*1.6f,1.5f,0xFFFFFFFF); gfx_disk(cx+k*1.6f,cy-k*1.6f,1.5f,0xFFFFFFFF); } break;
        case ST_SKIP: gfx_rect(cx-5,cy+1,10,2,C_DIM); break;
        default:      gfx_disk(cx,cy,8,C_LINE); gfx_disk(cx,cy,6,C_PANEL); break;
    }
}
/* ---------- shared bits of the redesigned tabs ---------- */
#define C_WHITE  0xFFFFFFFF
#define C_WELL   0xEFE7D2FF
#define C_EMPTY  0xF2ECDBFF
#define C_OFF    0xD8CFBDFF
#define C_SLOT   0xD9CDB0FF
#define C_GOLD   0xE8B33AFF
#define C_BUY    0xE3A04AFF
#define C_WARNBG 0xF8E0DAFF
#define C_BODY   0xE2D6BCFF
#define C_STICK  0x7A6853FF
/* number with thousands separators; four buffers so a line can hold several */
static const char* th(u32 v){
    static char b[4][16]; static int k; char* o=b[k++&3]; char t[12];
    int n=aden_snprintf(t,sizeof t,"%u",v), j=0;
    for(int i=0;i<n;i++){ if(i && (n-i)%3==0) o[j++]=','; o[j++]=t[i]; }
    o[j]=0; return o;
}
/* at most n characters of s (aden_snprintf has no precision); four buffers */
static const char* clip(const char* s,int n){
    static char b[4][24]; static int k; char* o=b[k++&3]; int i=0;
    if(n>23) n=23;
    for(;s[i] && i<n;i++) o[i]=s[i];
    o[i]=0; return o;
}
/* a horizontal meter: track and fill */
static void meter(float x,float y,float w,float h,float f,u32 c){
    box(x,y,w,h,h*0.5f,C_LINE);
    if(f>1) f=1;
    if(f>0){ float fw=w*f; if(fw<h) fw=h; box(x,y,fw,h,h*0.5f,c); }
}

/* ---------- give sheet: the item's colours or variants as a grid, the amount, where it goes ----------
   Up/Down moves between zones: the variant grid (when the item has colours or variants), the
   amount and the destination. The tile picked in the grid is what A gives. An item with both a
   body and a pattern shows one of the two sets at a time (ZL/ZR). */
enum { SET_COLOR, SET_BODY, SET_FAB };
enum { SZ_GRID, SZ_QTY, SZ_DEST };
static struct { int set[2], nset, n, pick, mx, fi; u16 id, var; char fam; } SH;
static int s_sh_set;
static void sheet_open(int i){
    s_sheet=1; s_sh_item=i; s_sh_col=s_sh_body=s_sh_fab=s_sh_set=0; s_sh_row=-1; s_qty=1; s_status[0]=0;
}
static void sheet_close(void){ s_sheet=0; s_qty=1; }
#define SH_GY0   252.f   /* top of the variant grid (panel y) */
#define SH_SET   5       /* text row above the grid: the set and the picked variant */
#define SH_QTY   17      /* text rows of the amount, the destination and the give button */
#define SH_DEST  19
#define SH_GIVE  21
static void paint_sheet(void){
    box(20,108,530,92,22,C_WHITE);
    box(32,116,76,76,18,C_WELL);
    if(!icon_at(SH.id,SH.fam,SH.var,WX(38),WY(122),64,64,aden_icon_mode())) dot(70,154,20,FAMCOL[SH.fi]);
    if(!SH.nset) box(20,252,530,190,22,C_WELL);
    box(20,GY(SH_QTY)-10,530,36,18,s_sh_row==SZ_QTY?C_PILL:C_WELL);
    box(20,GY(SH_DEST)-10,530,36,18,s_sh_row==SZ_DEST?C_PILL:C_WELL);
    box(20,GY(SH_GIVE)-10,530,36,18,C_HEAD);
}
static void give(const Item* it,u16 var,int q,const char* what){
    const char* np=needs_param(it->id);
    if(np){ aden_snprintf(s_status,sizeof s_status,"%s",np); return; }
    int r= s_to_storage ? aden_storage_add(it->id,q,it->stack,var) : aden_pocket_add(it->id,q,it->stack,var);
    /* -1 player data not ready, -2 data looks wrong (nothing written) */
    if(r==-1) aden_snprintf(s_status,sizeof s_status,s_to_storage?"Home storage not ready yet - try again in a moment":"Player data not ready yet - try again in a moment");
    else if(r<0) aden_snprintf(s_status,sizeof s_status,s_to_storage?"Home storage looks wrong - nothing was added":"Pockets look wrong - nothing was added");
    else if(s_to_storage) aden_snprintf(s_status,sizeof s_status,r==0?"Home storage is full":r<q?"Sent %d x %s (home storage now full)":"Sent %d x %s to home storage",r,what);
    else if(r==0) aden_snprintf(s_status,sizeof s_status,"Your pockets are full");
    else if(r<q) aden_snprintf(s_status,sizeof s_status,"Added %d x %s (pockets now full)",r,what);
    else aden_snprintf(s_status,sizeof s_status,"Added %d x %s to your pockets",r,what);
}
/* the colour of a clothing item, from its name "base (Colour)"; the whole name if it has none */
static void color_of(const Item* it,char* o,int cap){
    const char* c=it->name+base_len(it); int m=(int)strlen(c);
    if(m>3){ c+=2; m-=3; } else { c=it->name; m=(int)strlen(c); }
    if(m>cap-1) m=cap-1;
    memcpy(o,c,m); o[m]=0;
}
/* what the sheet picks now: the item (colour), its variant count field, the sets shown */
static Item* sheet_pick(void){
    Item* L=&s_items[s_sh_item];
    if(s_sh_col>=L->ncol) s_sh_col=0;
    Item* it=L+s_sh_col;
    int nb=vcount(it->body), nf=vcount(it->fab);
    if(s_sh_body>=nb) s_sh_body=0;
    if(s_sh_fab>=nf) s_sh_fab=0;
    SH.nset=0;
    if(L->ncol>1) SH.set[SH.nset++]=SET_COLOR;
    if(nb) SH.set[SH.nset++]=SET_BODY;
    if(nf && SH.nset<2) SH.set[SH.nset++]=SET_FAB;
    if(s_sh_set>=SH.nset) s_sh_set=0;
    int s= SH.nset ? SH.set[s_sh_set] : -1;
    SH.n=    s==SET_COLOR ? L->ncol : s==SET_BODY ? nb : s==SET_FAB ? nf : 0;
    SH.pick= s==SET_COLOR ? s_sh_col : s==SET_BODY ? s_sh_body : s==SET_FAB ? s_sh_fab : 0;
    if(s_sh_row<0 || (s_sh_row==SZ_GRID && !SH.nset)) s_sh_row= SH.nset ? SZ_GRID : SZ_QTY;
    /* stackables go up to 40 full slots, the rest to 99 */
    SH.mx= it->stack>1 ? 40*it->stack : 99;
    SH.id=it->id; SH.fam=it->fam; SH.fi=fam_idx(it->fam); SH.var=(u16)(s_sh_body+32*s_sh_fab);
    return it;
}
static void sheet_set_pick(int k){
    int s=SH.set[s_sh_set];
    if(s==SET_COLOR) s_sh_col=k; else if(s==SET_BODY) s_sh_body=k; else s_sh_fab=k;
}
static void sheet(void){
    Item* L=&s_items[s_sh_item]; Item* it=sheet_pick();
    if(pr(B_B)){ sheet_close(); s_press&=~(u64)B_B; return; }   /* B only closes the sheet */
    int z=s_sh_row;
    if(z==SZ_GRID){
        /* move in the variant grid like in the item grid; down from the last row goes to the amount */
        int k=SH.pick, n=SH.n;
        if(left()  && k>0) k--;
        if(right() && k<n-1) k++;
        if(up()    && k>=GCOLS) k-=GCOLS;
        if(down()){ if(k+GCOLS<n) k+=GCOLS; else if(k/GCOLS<(n-1)/GCOLS) k=n-1; else s_sh_row=SZ_QTY; }
        if(k!=SH.pick) sheet_set_pick(k);
        if(SH.nset>1 && (pr(B_ZL)||pr(B_ZR))) s_sh_set^=1;
    } else {
        if(up()){ if(z==SZ_DEST) s_sh_row=SZ_QTY; else if(SH.nset) s_sh_row=SZ_GRID; }
        if(down() && z==SZ_QTY) s_sh_row=SZ_DEST;
        if(z==SZ_QTY){
            if(left()) s_qty--;
            if(right()) s_qty++;
            if(pr(B_ZL)) s_qty=(s_qty-1)/10*10;   /* previous / next ten */
            if(pr(B_ZR)) s_qty=s_qty/10*10+10;
        }
        if(z==SZ_DEST && (left()||right())) s_to_storage=!s_to_storage;
    }
    /* from any zone: -/+ 1, X max, Y back to 1 */
    if(pr(B_MINUS)) s_qty--;
    if(pr(B_PLUS))  s_qty++;
    if(pr(B_X))  s_qty=SH.mx;
    if(pr(B_Y))  s_qty=1;
    it=sheet_pick();
    if(s_qty>SH.mx) s_qty=SH.mx;
    if(s_qty<1) s_qty=1;
    int q=s_qty, nb=vcount(it->body), nf=vcount(it->fab);
    /* the name in messages: a clothing item's name holds its colour, a remake gets its variants */
    char bn[24], fn[24], what[64]; vname(it->body,s_sh_body+1,bn,sizeof bn); vname(it->fab,s_sh_fab+1,fn,sizeof fn);
    if(nb && nf) aden_snprintf(what,sizeof what,"%s (%s, %s)",it->name,bn,fn);
    else if(nb||nf) aden_snprintf(what,sizeof what,"%s (%s)",it->name,nb?bn:fn);
    else aden_snprintf(what,sizeof what,"%s",it->name);
    if(pr(B_A)) give(it,SH.var,q,what);

    /* header card: icon, name, family and id */
    char base[64]; int n=base_len(it); if(n>63) n=63; memcpy(base,it->name,n); base[n]=0;
    if(n<=22) big(126,118,1.6f,C_TEXT,"%s",base); else at(0,12,C_TEXT,"%s",base);
    at(2,12,C_DIM,"%s - %s",FAMN[SH.fi],it->cat);
    if(nb||nf) at(3,12,C_DIM,"Item %04X   variant %d",it->id,SH.var);
    else if(it->stack>1) at(3,12,C_DIM,"Item %04X   stacks to %d",it->id,it->stack);
    else at(3,12,C_DIM,"Item %04X",it->id);
    /* variant grid: one tile per colour or variant of the set shown, like the item grid */
    if(SH.nset){
        int s=SH.set[s_sh_set]; char part[24], v[40];
        if(s==SET_COLOR){ aden_snprintf(part,sizeof part,"Color"); color_of(it,v,sizeof v); }
        else if(s==SET_BODY){ vname(it->body,0,part,sizeof part); aden_snprintf(v,sizeof v,"%s",bn); }
        else { vname(it->fab,0,part,sizeof part); aden_snprintf(v,sizeof v,"%s",fn); }
        at(SH_SET,1,C_TEXT,"%s: %s",part,v);
        if(SH.nset>1) aden_snprintf(s_famtxt,sizeof s_famtxt,"< %s >",clip(part,16));   /* ZL/ZR: the other set */
        else at_r(SH_SET,62,C_DIM,"%d/%d",SH.pick+1,SH.n);
        char la[GPAGE][GLINE+1], lb[GPAGE][GLINE+1];
        s_grid_on=1; s_grid_bar=0; s_grid_y0=SH_GY0; s_pill_top=GY(SH_SET)-10; s_grid_cur= z==SZ_GRID;
        s_grid_n=SH.n; s_grid_sel=SH.pick;
        for(int k=0;k<SH.n;k++){
            const Item* t= s==SET_COLOR ? L+k : it; char nm[40];
            s_grid_id[k]=t->id; s_grid_fc[k]=t->fam; s_grid_fam[k]=SH.fi; s_grid_nv[k]=0;
            s_grid_var[k]= s==SET_BODY ? (u16)(k+32*s_sh_fab) : s==SET_FAB ? (u16)(s_sh_body+32*k) : 0;
            if(s==SET_COLOR) color_of(t,nm,sizeof nm); else vname(s==SET_BODY?it->body:it->fab,k+1,nm,sizeof nm);
            wrap2(nm,la[k],lb[k]);
        }
        grid_labels(la,lb);
        aden_icons_want(s_grid_n,s_grid_id,s_grid_fc,s_grid_var);   /* the picked tile is the header icon too */
    } else {
        at(11,1,C_DIM,"This item has no colors or variants to choose.");
        aden_icons_want(1,&SH.id,&SH.fam,&SH.var);
    }
    /* amount and destination rows, then the give button with the slots the amount takes */
    at(SH_QTY,2,C_TEXT,"Amount"); at(SH_QTY,20,C_TEXT,"< %d >",q); at_r(SH_QTY,62,C_DIM,"max %d",SH.mx);
    int f= s_to_storage ? aden_storage_free() : aden_pocket_free();
    at(SH_DEST,2,C_TEXT,"Send to"); at(SH_DEST,20,C_TEXT,"< %s >",s_to_storage?"Home storage":"Pockets");
    if(f>=0) at_r(SH_DEST,62,C_DIM,"%d free",f);
    int slots= it->stack>1 ? (q+it->stack-1)/it->stack : q;
    const char* who=aden_player_name(aden_player_active());
    at(SH_GIVE,2,C_WHITE,"A  Give %d to %s",q,s_to_storage?"home storage":who[0]?clip(who,12):"pockets");
    at_r(SH_GIVE,62,C_WHITE,"%d slot%s",slots,slots==1?"":"s");
    z=s_sh_row;
    text(26,C_DIM, z==SZ_QTY ? "Left/Right 1  ZL/ZR 10  X max  Y one  A give  B back  L/R tabs"
                 : z==SZ_DEST ? "Left/Right pockets or storage  A give  B back  L/R tabs"
                 : SH.nset>1 ? "D-pad choose  ZL/ZR other part  A give  B back  L/R tabs"
                 : "D-pad choose  Down amount  A give  B back  L/R tabs");
    s_status_row=SH_GIVE+2;
    s_paint=paint_sheet;
}
/* the search bar's text: the query on the left, the count at the bar's right end */
static void search_line(void){
    int n=at_n(ROW0,1,C_TEXT,"Search: %s%s",s_query,s_typing?"_":"");
    if(!s_qlen && !s_typing) at(ROW0,10,C_DIM,"Y to type");
    char c[16]; int m=aden_snprintf(c,sizeof c,"%d found",s_nmatch);
    if(1+n+2+m>41) m=aden_snprintf(c,sizeof c,"%d",s_nmatch);
    at_r(ROW0,41,C_DIM,"%s",c);
}
static void tab_items(void){
    if(!s_items_loaded) load_items();
    if(!s_match) refilter();
    if(g_player_state==0) aden_player_find();   /* spawning needs the player data */
    /* the island search starts here too, so a failed read shows up on the first tab */
    if(g_player_state==2 && g_island_state==0) aden_island_find();
    if(loading_page(2)) return;
    if(!s_nitems){ text(ROW0,C_WARN,"items.txt not found on SD card"); text(ROW0+1,C_DIM,"put it in sdmc/aden/items.txt"); return; }
    int* cur=&s_cur[TAB_ITEMS];
    if(*cur>s_nmatch-1) *cur= s_nmatch>0 ? s_nmatch-1 : 0;
    if(s_typing){
        search_line();
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
        if(pr(B_A) && s_qlen<22){ s_query[s_qlen++]=KEYS[s_ky][s_kx]; s_query[s_qlen]=0; refilter(); *cur=0; }
        if(pr(B_Y) && s_qlen>0){ s_query[--s_qlen]=0; refilter(); *cur=0; }
        if(pr(B_X)){ s_qlen=0; s_query[0]=0; refilter(); *cur=0; }
        if(pr(B_B)||pr(B_PLUS)){ s_typing=0; s_press&=~(u64)(B_B|B_PLUS); }  /* B only leaves the keyboard */
        return;
    }
    /* A on a tile opens the sheet; once it is open, A is the sheet's (give) */
    if(!s_sheet && pr(B_A) && s_nmatch){ sheet_open(s_match[*cur]); s_press&=~(u64)B_A; }
    if(s_sheet){ sheet(); return; }
    search_line();
    /* move in the grid; going past the first or last row turns the page */
    if(left()  && *cur>0) (*cur)--;
    if(right() && *cur<s_nmatch-1) (*cur)++;
    if(up()    && *cur>=GCOLS) *cur-=GCOLS;
    if(down()){ if(*cur+GCOLS<s_nmatch) *cur+=GCOLS; else if(*cur/GCOLS<(s_nmatch-1)/GCOLS) *cur=s_nmatch-1; }
    if(pr(B_ZL)){ s_fam=(s_fam+NFAM-1)%NFAM; refilter(); *cur=0; }
    if(pr(B_ZR)){ s_fam=(s_fam+1)%NFAM; refilter(); *cur=0; }
    Item* si= s_nmatch ? &s_items[s_match[*cur]] : 0;
    if(pr(B_Y)) { s_typing=1; return; }
    /* tiles of the current page */
    int top=(*cur/GPAGE)*GPAGE, pages=(s_nmatch+GPAGE-1)/GPAGE;
    s_grid_on=1; s_grid_bar=1; s_grid_y0=GY0; s_pill_top=GY(ROW0)-10; s_grid_cur=1; s_grid_sel=*cur-top; s_grid_n=0;
    char la[GPAGE][GLINE+1], lb[GPAGE][GLINE+1];
    for(int i=0;i<GPAGE && top+i<s_nmatch;i++){ Item* it=&s_items[s_match[top+i]];
        s_grid_fam[i]=fam_idx(it->fam); s_grid_id[i]=it->id; s_grid_fc[i]=it->fam; s_grid_var[i]=0; s_grid_n++;
        s_grid_nv[i]= it->ncol>1 ? it->ncol : vcount(it->body)+vcount(it->fab);
        /* a clothing item shows its name without the colour, and how many colours it has */
        if(it->ncol>1){ char b[64]; int n=base_len(it); if(n>63) n=63; memcpy(b,it->name,n); b[n]=0;
            wrap2(b,la[i],lb[i]); if(!lb[i][0]) aden_snprintf(lb[i],GLINE+1,"%d colors",it->ncol); }
        else wrap2(it->name,la[i],lb[i]);
    }
    aden_icons_want(s_grid_n,s_grid_id,s_grid_fc,0);   /* only the tiles on this page are loaded */
    grid_labels(la,lb);
    aden_snprintf(s_famtxt,sizeof s_famtxt,"< %s >",FAMN[s_fam]);
    if(si){ int n=base_len(si); char b[64]; if(n>63) n=63; memcpy(b,si->name,n); b[n]=0;
        put(ROW0+23,0,C_TEXT,"%s   (%04X, page %d/%d)",b,si->id,top/GPAGE+1,pages);
        int nb=vcount(si->body), nf=vcount(si->fab); char pb[24], pf[24]; vname(si->body,0,pb,sizeof pb); vname(si->fab,0,pf,sizeof pf);
        if(si->ncol>1) put(ROW0+24,0,C_DIM,"%d colors - A to choose one",si->ncol);
        else if(nb && nf) put(ROW0+24,0,C_DIM,"%s: %d   %s: %d - A to choose",pb,nb,pf,nf);
        else if(nb||nf) put(ROW0+24,0,C_DIM,"%s: %d variants - A to choose",nb?pb:pf,nb?nb:nf);
        else if(si->stack>1) put(ROW0+24,0,C_DIM,"Stacks to %d",si->stack);
    }
    else text(ROW0+23,C_DIM,"Nothing found - change the search or the family (ZL/ZR)");
    text(26,C_DIM,"A choose  Y search  ZL/ZR family  L/R tabs");
}
/* ---------- Player tab: resident card, currency tokens over a counter, unlock cards ----------
   Up/Down picks a zone, Left/Right an entry in it. A on a currency opens its digits. */
enum { Z_WHO, Z_MONEY, Z_UNLOCK };
static int s_pz=Z_MONEY, s_pcur, s_punl, s_pedit, s_pdig;
static const char* const CUR_NAME[4]={"Bells in wallet","Nook Miles","Bank savings","Poki"};
static const char* const CUR_SHORT[4]={"Bells","Miles","Bank","Poki"};
static const char* const CUR_ABBR[4]={"B","M","S","P"};
static const u32 CUR_COL[4]={0xE8B33AFF,0x6FA8DCFF,0x9C7A55FF,0xE07A9BFF};
/* the game's limits: 99,999 Bells in the wallet (more only fits as Bell bags), 9,999,999 Nook
   Miles, 999,999,999 Bells in the bank. Poki has no published limit and players hold more than
   1,000,000, so it gets 7 digits like the miles. */
static const u32 CUR_CAP[4]={99999,9999999,999999999,9999999};
static const s32 CUR_BIG[4]={10000,100000,1000000,100000};
static int cur_ok(int k){ return k==0 ? aden_player_ready() : aden_stat_ok(k-1); }
static u32 cur_get(int k){ return k==0 ? aden_wallet_get() : aden_stat_get(k-1); }
static void cur_set(int k,s64 v){
    if(v<0) v=0; if(v>(s64)CUR_CAP[k]) v=CUR_CAP[k];
    if((u32)v==cur_get(k)) return;
    if(k==0) aden_wallet_set((u32)v); else aden_stat_set(k-1,(u32)v);
}
static int ndig(u32 v){ int n=1; while(v>=10){ v/=10; n++; } return n; }
static u32 pow10u(int n){ u32 p=1; while(n-->0) p*=10; return p; }
/* digit boxes: 48 px apart, 16 px more between groups of three, centred on the counter */
static float dig_x(int n,int i){
    float x=285-(n*48-8+16*((n-1)/3))*0.5f;
    for(int j=0;j<i;j++) x+= (n-1-j)%3==0 ? 64 : 48;
    return x;
}
static struct { int np, ap, ok[4], free, slots, rk, rt, ek, et; u32 v[4]; } P;
static void paint_player(void){
    if(s_pz==Z_WHO) box(15,103,540,100,28,C_PILL);
    box(20,108,530,90,24,C_WHITE);
    dot(64,154,30,C_HEAD);
    if(P.np>1) for(int i=0;i<P.np;i++) dot(114+i*14,186,4,i==P.ap?C_HEAD:C_LINE);
    for(int k=0;k<4;k++){
        float x=20+136*k;
        if(s_pz==Z_MONEY && k==s_pcur) box(x,208,124,96,20,C_WHITE);
        else box(x,208,124,80,20,P.ok[k]?C_WELL:C_EMPTY);
        dot(x+22,224,12,P.ok[k]?CUR_COL[k]:C_DIM);
    }
    box(20,296,530,152,24,C_WHITE);
    if(s_pz==Z_MONEY && P.ok[s_pcur]){
        int n=ndig(CUR_CAP[s_pcur]);
        for(int i=0;i<n;i++) box(dig_x(n,i),340,40,52,12,s_pedit && i==s_pdig ? C_PILL : C_WELL);
    } else if(s_pz==Z_UNLOCK && s_punl==2){
        for(int i=0;i<P.slots;i++) box(48+(i%20)*24,340+(i/20)*24,18,18,6,i<P.slots-P.free?C_SLOT:C_PILL);
    } else if(s_pz==Z_UNLOCK){
        int k= s_punl ? P.ek : P.rk, t= s_punl ? P.et : P.rt;
        meter(40,342,490,10,t?(float)k/t:0,C_HEAD);
    }
    /* unlock cards; progress as a ring of 16 dots like the loading spinner */
    for(int i=0;i<3;i++){
        float x=22+176*i, cx=x+36, cy=494;
        if(s_pz==Z_UNLOCK && i==s_punl) box(x-5,457,180,94,22,C_PILL);
        box(x,462,170,84,20,C_WHITE);
        if(i<2){
            int k= i ? P.ek : P.rk, t= i ? P.et : P.rt, on= t ? (k*16+t/2)/t : 0;
            static const float RING[16][2]={{0,-24},{9.18f,-22.17f},{16.97f,-16.97f},{22.17f,-9.18f},{24,0},{22.17f,9.18f},{16.97f,16.97f},{9.18f,22.17f},
                                            {0,24},{-9.18f,22.17f},{-16.97f,16.97f},{-22.17f,9.18f},{-24,0},{-22.17f,-9.18f},{-16.97f,-16.97f},{-9.18f,-22.17f}};
            for(int d=0;d<16;d++) dot(cx+RING[d][0],cy+RING[d][1],3.2f,d<on?C_HEAD:C_LINE);
        } else { dot(cx,cy+6,18,C_GOLD); dot(cx,cy-16,7,C_GOLD); }
    }
}
static void tab_player(void){
    if(g_player_state==0) aden_player_find();
    if(loading_page(0)){ s_pedit=0; return; }
    if(!aden_player_ready()){ text(ROW0+2,C_DIM,"Finding your player data..."); s_pedit=0; return; }
    /* Minus redoes the player search over the whole heap (not while the island thread reads the copies) */
    if(!s_pedit && pr(B_MINUS) && g_island_state!=1){ s_status[0]=0; g_player_state=0; g_island_state=0; aden_player_find_full(); return; }
    P.np=aden_player_count(); P.ap=aden_player_active();
    for(int k=0;k<4;k++){ P.ok[k]=cur_ok(k); P.v[k]=P.ok[k]?cur_get(k):0; }
    int minz= P.np>1 ? Z_WHO : Z_MONEY;
    if(s_pz<minz) s_pz=minz;
    if(s_pedit && !P.ok[s_pcur]) s_pedit=0;
    if(s_pedit){
        int k=s_pcur, n=ndig(CUR_CAP[k]);
        if(left() && s_pdig>0) s_pdig--;
        if(right() && s_pdig<n-1) s_pdig++;
        s64 v=P.v[k], d=pow10u(n-1-s_pdig);
        if(up()) v+=d; if(down()) v-=d;
        if(pr(B_X)) v=CUR_CAP[k]; if(pr(B_Y)) v=0;
        cur_set(k,v);
        if(pr(B_A)||pr(B_B)){ s_pedit=0; s_press&=~(u64)B_B; }   /* B only closes the digits */
    } else {
        if(up() && s_pz>minz) s_pz--;
        if(down() && s_pz<Z_UNLOCK) s_pz++;
        if(s_pz==Z_WHO){
            if(left())  aden_player_select((P.ap+P.np-1)%P.np);
            if(right()) aden_player_select((P.ap+1)%P.np);
        } else if(s_pz==Z_MONEY){
            if(left() && s_pcur>0) s_pcur--;
            if(right() && s_pcur<3) s_pcur++;
            int k=s_pcur;
            if(P.ok[k]){
                s64 v=P.v[k];
                if(pr(B_ZL)) v-=CUR_BIG[k]; if(pr(B_ZR)) v+=CUR_BIG[k];
                if(pr(B_X)) v=CUR_CAP[k]; if(pr(B_Y)) v=0;
                cur_set(k,v);
                if(pr(B_A)){ int n=ndig(CUR_CAP[k]); s_pedit=1; s_pdig= n>4 ? n-4 : 0; }   /* start on the thousands */
            }
        } else {
            if(left() && s_punl>0) s_punl--;
            if(right() && s_punl<2) s_punl++;
            if(pr(B_A)){
                if(s_punl==0){ int n=aden_recipes_learn_all(); aden_snprintf(s_status,sizeof s_status,n<0?"Recipe book not found":(n?"Learned %d new recipes":"You already know every recipe"),n); }
                else if(s_punl==1){ int n=aden_reactions_learn_all(); aden_snprintf(s_status,sizeof s_status,n<0?"Reactions not found":(n?"Learned %d new reactions":"You already know every reaction"),n); }
                else { int n=aden_pocket_add(0x08A4,40,1,0); aden_snprintf(s_status,sizeof s_status,n>0?"Added %d Bell bags":n==0?"Your pockets are full":n==-1?"Player data not ready yet - try again in a moment":"Pockets look wrong - nothing was added",n); }
            }
        }
    }
    /* read again after the edits above */
    P.np=aden_player_count(); P.ap=aden_player_active();
    for(int k=0;k<4;k++){ P.ok[k]=cur_ok(k); P.v[k]=P.ok[k]?cur_get(k):0; }
    P.free=aden_pocket_free(); P.slots=aden_pocket_slots(); if(P.slots<0) P.slots=0; if(P.free>P.slots) P.free=P.slots;
    P.rk=aden_recipes_known(); P.rt=aden_recipes_total(); P.ek=aden_reactions_known(); P.et=aden_reactions_total();

    /* resident card */
    char pn[24],tn[24]; aden_player_names(pn,tn,sizeof pn);
    const char* nm=aden_player_name(P.ap); if(!nm[0]) nm=pn[0]?pn:"You";
    big(56,138,2.0f,C_WHITE,"%c",nm[0]);
    big(110,120,1.6f,C_TEXT,"%s",clip(nm,16));
    if(tn[0]) at(2,10,C_DIM,"of %s",tn);
    if(P.np>1) at_r(1,62,C_DIM,"< %d / %d >",P.ap+1,P.np); else at_r(1,62,C_DIM,"Only resident");
    /* currency tokens */
    for(int k=0;k<4;k++){
        at(5,1+17*k,C_WHITE,"%s",CUR_ABBR[k]);
        at(5,4+17*k,C_TEXT,"%s",CUR_SHORT[k]);
        if(P.ok[k]) at(7,1+17*k,C_TEXT,"%s",th(P.v[k])); else at(7,1+17*k,C_DIM,"--");
    }
    /* counter: what the chosen entry does */
    if(s_pz==Z_MONEY){
        int k=s_pcur;
        if(P.ok[k]){
            int n=ndig(CUR_CAP[k]), lead=n-ndig(P.v[k]);
            at(10,1,C_TEXT,"%s",CUR_NAME[k]); at_r(10,62,C_DIM,"max %s",th(CUR_CAP[k]));
            /* one print for the digits, one for the grey leading zeros: 3 characters (48 px) per box */
            char d[40], z[40]; int m=0; u32 v=P.v[k];
            for(int i=0;i<n;i++){
                char c='0'+(char)(v/pow10u(n-1-i)%10);
                int w= i<n-1 && (n-1-i)%3==0 ? 4 : 3;
                for(int j=0;j<w;j++){ d[m+j]=' '; z[m+j]=' '; }
                if(i<lead) z[m]=c; else d[m]=c;
                m+=w;
            }
            d[m]=z[m]=0;
            big(dig_x(n,0)+12,350,2.0f,C_TEXT,"%s",d);
            if(lead) big(dig_x(n,0)+12,350,2.0f,C_DIM,"%s",z);
            if(!s_pedit){
                at(14,1,C_DIM,"A edit digits   ZL/ZR +-%s   X max   Y zero",th(CUR_BIG[k]));
                if(k==0) at(15,1,C_DIM,"More than 99,999 Bells only fits as Bell bags.");
                if(k==3) at(15,1,C_DIM,"Poki is the Happy Home Paradise currency.");
            }
        } else {
            at(10,1,C_TEXT,"%s was not found in memory.",CUR_SHORT[k]);
            at(12,1,C_DIM,"Press - to search the player data again.");
            if(k==3) at(13,1,C_DIM,"Poki is the Happy Home Paradise currency.");
        }
    } else if(s_pz==Z_WHO){
        at(10,1,C_TEXT,"Residents"); at_r(10,62,C_DIM,"%d on this island",P.np);
        for(int i=0;i<P.np && i<8;i++)
            at(11+i%4,3+30*(i/4),i==P.ap?C_TEXT:C_DIM,"%s%s",i==P.ap?"> ":"  ",clip(aden_player_name(i),20));
        at(15,1,C_DIM,"Left/Right: who to edit (items you spawn go to them too)");
    } else if(s_punl<2){
        int rec= s_punl==0, k= rec ? P.rk : P.ek, t= rec ? P.rt : P.et;
        at(10,1,C_TEXT,rec?"Learn every DIY recipe":"Learn every reaction");
        at(13,1,C_DIM,rec?"%d recipes to learn - new ones show up marked as new.":"%d reactions to learn.",t>k?t-k:0);
        at(14,1,C_DIM,rec?"A adds them all to your recipe book.":"A adds them all to your reaction list.");
    } else {
        at(10,1,C_TEXT,"Fill pockets with Bell bags");
        at(14,1,C_DIM,"A puts a 99,000 Bell bag in each free slot.");
        at(15,1,C_TEXT,"%d bags = %s Bells",P.free,th((u32)P.free*99000u));
    }
    /* unlock cards */
    at(18,8,C_TEXT,"Recipes");   at(19,8,C_DIM,"%d/%d",P.rk,P.rt);
    at(18,30,C_TEXT,"Reactions"); at(19,30,C_DIM,"%d/%d",P.ek,P.et);
    at(18,52,C_TEXT,"Bell bags"); if(P.free>=0) at(19,52,C_DIM,"%d free",P.free); else at(19,52,C_DIM,"--");
    text(26,C_DIM,s_pedit?"Left/Right digit  Up/Down change  X max  Y zero  A done"
                         :"Up/Down zone  Left/Right pick  A open/use  - search again");
    s_paint=paint_player;
}

/* ---------- Island tab: two price tickets, villager list, a card for the chosen line ---------- */
static const char* const SPECIES[35]={"Anteater","Bear","Bird","Bull","Cat","Cub","Chicken","Cow","Alligator","Deer",
    "Dog","Duck","Elephant","Frog","Goat","Gorilla","Hamster","Hippo","Horse","Koala","Kangaroo","Lion","Monkey","Mouse",
    "Octopus","Ostrich","Eagle","Penguin","Pig","Rabbit","Rhino","Sheep","Squirrel","Tiger","Wolf"};
static const u32 VCOL[8]={0x9DB0CCFF,0xD9B48AFF,0xE9A3C9FF,0xA9CDEBFF,0xF2A65AFF,0xE8C547FF,0xF08A6CFF,0xC79C6EFF};
/* friendship level 1-6 from the 0-255 points, 0 if not met */
static int flevel(int f){ return f<0?0 : f<25?1 : f<70?2 : f<100?3 : f<150?4 : f<200?5 : 6; }
static struct { int nv, vk[10], vf[10], vm[10], vs[10], weeds; const char* vn[10]; u32 sell, buy, week[12]; } I;
#define WEED_ROW 18   /* screen row of the Weeds line, below the longest villager list */
#define C_WEED 0x7DBF5AFF
static int s_itk;   /* the ticket Up goes back to from the list */
static void paint_island(void){
    int cur=s_cur[TAB_ISLAND];
    for(int t=0;t<2;t++){
        float x=14+272*t; u32 v= t ? I.buy : I.sell;
        if(cur==t) box(x-4,100,270,100,22,C_PILL);
        box(x,104,262,92,18,C_WHITE);
        meter(x+16,178,230,8,v/999.f,t?C_BUY:C_HEAD);
    }
    box(16,204,220,356,18,C_WELL);
    dot(46,244,7,C_HEAD);
    for(int i=0;i<I.nv;i++) dot(46,264+20*i,7,I.vs[i]<0?C_OFF:VCOL[I.vs[i]%8]);
    dot(46,GY(WEED_ROW)+8,7,C_WEED);
    int weed= cur==3+I.nv;
    if(weed) box(22,GY(WEED_ROW)-3,208,22,11,C_PILL);
    else if(cur>=2) box(22,GY(6+cur-2)-3,208,22,11,C_PILL);
    for(int i=0;i<I.nv;i++) if(I.vm[i]) box(152,GY(7+i)-1,40,18,9,C_WARNBG);
    box(244,204,310,356,18,C_WHITE);
    if(cur<2){
        for(int i=0;i<4;i++) box(258,GY(8+2*i)-3,64,22,11,C_HEADDK);
        if(cur==0){
            for(int i=0;i<12;i++){ float h=I.week[i]*70.f/999; if(h<2) h=2; if(h>70) h=70; box(264+23*i,530-h,17,h,4,C_HEAD); }
            box(262,530,276,2,0,C_LINE);
        }
    } else if(weed){
        dot(294,244,28,C_WEED);
        box(262,366,276,2,0,C_LINE);
        dot(274,GY(13)+8,12,C_HEADDK);
        box(262,GY(16)-6,276,92,14,C_WARNBG);   /* the "seen after a loading screen" warning */
    } else {
        int i=cur-3, f= i<0 ? -1 : I.vf[i], L=flevel(f);
        dot(294,244,28,i<0?C_HEAD:(I.vs[i]<0?C_OFF:VCOL[I.vs[i]%8]));
        if(i>=0){
            meter(262,318,276,10,f<0?0:f/255.f,C_HEAD);
            for(int d=0;d<6;d++) dot(271+22*d,GY(11)+8,7,d<L?C_PILL:C_LINE);
        }
        box(262,366,276,2,0,C_LINE);
        int nk= i<0 ? 1 : 3;
        for(int k=0;k<nk;k++) dot(274,GY(13+2*k)+8,12,C_HEADDK);
        if(i>=0 && I.vm[i]) box(262,488,276,36,14,C_WARNBG);
    }
}
static void tab_island(void){
    int* cur=&s_cur[TAB_ISLAND];
    if(g_player_state==0) aden_player_find();
    if(g_player_state==2 && g_island_state==0) aden_island_find();
    if(loading_page(1)) return;
    if(!aden_island_ready()){ text(ROW0+2,C_DIM,"Finding your island data..."); return; }
    I.nv=0;
    for(int k=0;k<10;k++) if(aden_villager_info(k,&I.vn[I.nv],&I.vf[I.nv],&I.vm[I.nv])){ I.vk[I.nv]=k; I.vs[I.nv]=aden_villager_species(k); I.nv++; }
    /* rows: the two tickets side by side (Left/Right), then Everyone, each villager, then Weeds */
    int WEED=3+I.nv, NROWS=WEED+1;
    if(*cur>NROWS-1) *cur=NROWS-1;
    if(*cur<2){
        if(left()) *cur=0; if(right()) *cur=1;
        if(down()){ s_itk=*cur; *cur=2; }
    } else {
        if(up()){ if(*cur==2) *cur=s_itk; else (*cur)--; }
        else if(down() && *cur<NROWS-1) (*cur)++;
    }
    if(*cur<2){
        int sell=*cur==0; s64 old=sell?aden_turnip_sell():aden_turnip_buy(), v=old;
        if(pr(B_MINUS)) v-=10; if(pr(B_PLUS)) v+=10; if(pr(B_ZL)) v-=100; if(pr(B_ZR)) v+=100;
        if(pr(B_X)) v= sell?660:90; if(pr(B_Y)) v=0;
        if(v<0) v=0; if(v>999) v=999;
        if(v!=old) aden_turnip_set(sell,(u32)v);
    } else if(*cur==2){
        if(pr(B_A)){ for(int i=0;i<I.nv;i++) aden_villager_friend(I.vk[i],255); aden_snprintf(s_status,sizeof s_status,"Every villager is now your best friend"); }
    } else if(*cur==WEED){
        if(pr(B_A)){ int n=aden_weeds_clear();
            aden_snprintf(s_status,sizeof s_status,n>0?"Removed %d weeds - enter a building to see it":n==0?"No weeds on your island":n==-1?"Island data not ready yet - try again in a moment":"Island map looks wrong - nothing was removed",n); }
    } else {
        int i=*cur-3;
        if(pr(B_A)){ aden_villager_friend(I.vk[i],255); aden_snprintf(s_status,sizeof s_status,"%s is now your best friend",I.vn[i]); }
        if(pr(B_Y)){ aden_villager_friend(I.vk[i],25); aden_snprintf(s_status,sizeof s_status,"%s friendship reset",I.vn[i]); }
        if(pr(B_X)){ aden_villager_moving(I.vk[i],!I.vm[i]); aden_snprintf(s_status,sizeof s_status,I.vm[i]?"%s is staying":"%s is now moving out",I.vn[i]); }
    }
    /* read again after the edits above */
    for(int i=0;i<I.nv;i++){ const char* n; aden_villager_info(I.vk[i],&n,&I.vf[i],&I.vm[i]); }
    I.sell=aden_turnip_sell(); I.buy=aden_turnip_buy(); aden_turnip_week(I.week); I.weeds=aden_weeds_count();

    /* tickets: both prices in one print at scale 2 (16 px a character, the tickets 17 apart) */
    at(0,0,C_DIM,"Sell, all week"); at(0,34,C_DIM,"Buy, Daisy Mae");
    char sb[24]; int sl=aden_snprintf(sb,sizeof sb,"%u",I.sell), bl=ndig(I.buy);
    while(sl<17) sb[sl++]=' ';
    aden_snprintf(sb+17,sizeof sb-17,"%u",I.buy); sl=ndig(I.sell);
    big(30,136,2.0f,C_TEXT,"%s",sb);
    at(2,2*sl+1,C_DIM,"bells"); at(2,34+2*bl+1,C_DIM,"bells");
    /* villager list */
    at(5,2,C_DIM,"Villagers %d/10",I.nv);
    at(6,4,C_TEXT,"Everyone");
    for(int i=0;i<I.nv;i++){
        at(7+i,4,C_TEXT,"%s",clip(I.vn[i],10));
        if(I.vm[i]) at(7+i,16,C_WARN,"out");
        if(I.vf[i]<0) at_r(7+i,24,C_DIM,"-"); else at_r(7+i,24,C_TEXT,"%d",I.vf[i]);
    }
    at(WEED_ROW,4,C_TEXT,"Weeds");
    if(I.weeds<0) at_r(WEED_ROW,24,C_DIM,"-"); else at_r(WEED_ROW,24,C_TEXT,"%d",I.weeds);
    /* card */
    if(*cur<2){
        int sell=*cur==0;
        at(5,29,C_TEXT,sell?"Sell price":"Buy price");
        at(6,29,C_DIM,sell?"Every half-day, Mon to Sat":"Daisy Mae, Sunday morning");
        static const char* const KEY[4]={"- +","ZL ZR","X","Y"}; static const int KC[4]={31,30,32,32};
        for(int k=0;k<4;k++) at(8+2*k,KC[k],C_WHITE,"%s",KEY[k]);
        at(8,38,C_TEXT,"10 down / up"); at(10,38,C_TEXT,"100 down / up");
        at(12,38,C_TEXT,sell?"Best natural (660)":"Cheapest natural (90)"); at(14,38,C_TEXT,"Zero");
        if(sell){ at(16,29,C_DIM,"This week"); at(21,29,C_DIM,"Mon"); at_r(21,63,C_DIM,"Sat"); }
        else { int pf=(int)I.sell-(int)I.buy;
               at(16,29,C_DIM,"Profit per turnip"); at_r(16,63,pf<0?C_WARN:C_TEXT,"%s%d",pf>0?"+":"",pf);
               at(17,29,C_DIM,"at this week's sell price"); }
    } else if(*cur==2){
        int bf=0; for(int i=0;i<I.nv;i++) if(I.vf[i]==255) bf++;
        at(6,32,C_WHITE,"All");
        big(334,222,1.6f,C_TEXT,"Everyone");
        at(7,38,C_DIM,"%d villagers",I.nv);
        at(9,29,C_DIM,"Best friends"); at_r(9,63,C_TEXT,"%d / %d",bf,I.nv);
        at(13,30,C_WHITE,"A"); at(13,33,C_TEXT,"Best friend for all (255)");
    } else if(*cur==WEED){
        at(6,32,C_WHITE,"Wd");
        big(334,222,1.6f,C_TEXT,"Weeds");
        at(7,38,C_DIM,"Whole island");
        at(9,29,C_DIM,"On the map"); if(I.weeds<0) at_r(9,63,C_DIM,"--"); else at_r(9,63,C_TEXT,"%d",I.weeds);
        at(13,30,C_WHITE,"A"); at(13,33,C_TEXT,"Remove every weed");
        at(16,31,C_WARN,"! Weeds already on screen");
        at(17,31,C_WARN,"  stay until the next");
        at(18,31,C_WARN,"  loading screen: enter and");
        at(19,31,C_WARN,"  leave a building.");
    } else {
        int i=*cur-3, f=I.vf[i];
        at(6,32,C_TEXT,"%c%c",I.vn[i][0],I.vn[i][1]?I.vn[i][1]:' ');
        big(334,222,1.6f,C_TEXT,"%s",clip(I.vn[i],12));
        at(7,38,C_DIM,"%s",I.vs[i]>=0?SPECIES[I.vs[i]]:"");
        at(9,29,C_DIM,"Friendship");
        if(f<0) at_r(9,63,C_DIM,"not met yet"); else at_r(9,63,C_TEXT,"%d / 255",f);
        if(f>=0) at_r(11,63,C_DIM,"Level %d of 6",flevel(f));
        at(13,30,C_WHITE,"A"); at(13,33,C_TEXT,"Best friend (255)");
        at(15,30,C_WHITE,"Y"); at(15,33,C_TEXT,"Reset friendship (25)");
        at(17,30,C_WHITE,"X"); at(17,33,C_TEXT,"Moving out: %s",I.vm[i]?"yes":"no");
        if(I.vm[i]) at(19,31,C_WARN,"Leaves after your next save");
    }
    text(26,C_DIM,*cur<2?"Left/Right sell or buy   Down villagers   B close"
                         :"Up/Down pick   Up from the top: prices   B close");
    s_status_row=23;
    s_paint=paint_island;
}

/* ---------- Mods tab: Stack 99 banner, then the mod folders as cards, 9 a page ---------- */
#define MPAGE 9
static int s_msel=-1;   /* -1: the Stack 99 banner, else a mod */
static const u32 BADGE[8]={0xE0A24AFF,0xE07A9BFF,0x7DBF5AFF,0x6B9BD1FF,0x9C7A55FF,0xC08A55FF,0xD46A5AFF,0x8FB3C9FF};
static int mods_pending(void){ int n=0; for(int i=0;i<s_nmods;i++) if(s_mods[i].on!=s_mods[i].on_at_start) n++; return n; }
static void paint_mods(void){
    int on=aden_stack99_on(), pend=mods_pending();
    box(20,108,530,76,22,s_msel<0?C_PILL:C_WELL);
    box(38,128,64,36,18,on?C_HEAD:C_OFF); dot(on?84:56,146,14,C_WHITE);
    int L= aden_stack99_found() ? 4 : 10;
    box(30+8*(62-L)-12,131,8*L+24,26,13,C_WHITE);
    int pages=(s_nmods+MPAGE-1)/MPAGE, pg= s_msel<0 ? 0 : s_msel/MPAGE;
    if(pages>1) for(int i=0;i<pages;i++) dot(540-(pages-1-i)*16,204,5,i==pg?C_HEAD:C_LINE);
    if(!s_nmods){
        box(20,220,530,284,22,C_WELL);
        box(255,262,26,10,3,C_SLOT); box(255,270,60,40,6,C_SLOT);   /* a folder */
    }
    for(int i=0;i<MPAGE && pg*MPAGE+i<s_nmods;i++){
        int k=pg*MPAGE+i; Mod* m=&s_mods[k];
        float x=18+176*(i%3), y=220+100*(i/3);
        if(k==s_msel) box(x-5,y-5,180,94,22,C_PILL);
        box(x,y,170,84,18,m->romfs?C_WELL:C_EMPTY);
        dot(x+28,y+25,13,m->romfs?BADGE[k%8]:C_OFF);
        if(m->romfs){ box(x+118,y+14,38,22,11,m->on?C_HEAD:C_OFF); dot(m->on?x+145:x+129,y+25,9,C_WHITE); }
    }
    if(pend) box(20,512,530,32,16,C_PILL);
}
/* up to two capitals of the folder name, else its first letter */
static void initials(const char* s,char* o){
    int n=0; for(const char* p=s;*p && n<2;p++) if(*p>='A'&&*p<='Z') o[n++]=*p;
    if(!n){ o[n++]= s[0]>='a'&&s[0]<='z' ? s[0]-32 : s[0]; }
    o[n]=0;
}
static void stack99_toggle(void){
    int on=!aden_stack99_on();
    /* off: split stacks over the normal max first, or the game shows them as 1 */
    if(!on){
        int need, have, k=aden_split_stacks(item_stack,&need,&have);
        if(k==-1){ if(g_player_state!=1) aden_player_find();
            aden_snprintf(s_modmsg,sizeof s_modmsg,"Finding your player data to split stacks - press A again"); return; }
        if(k==-2){ aden_snprintf(s_modmsg,sizeof s_modmsg,"Pockets or storage look wrong - Stack 99 stays ON"); return; }
        if(k==-3){ aden_snprintf(s_modmsg,sizeof s_modmsg,"Need %d free slots (pockets+storage), have %d - Stack 99 stays ON",need,have); return; }
        if(k>0) aden_log("stack 99 off: %d stacks split", k);
    }
    int r=aden_stack99_set(on);
    if(r<0) aden_snprintf(s_modmsg,sizeof s_modmsg,"Stack 99 is now %s - restart the game to apply",on?"ON":"off");
    else aden_snprintf(s_modmsg,sizeof s_modmsg,"Stack 99 is now %s (%d item kinds changed)",on?"ON":"off",r);
}
static void tab_mods(void){
    if(!s_mods_scanned) scan_mods();
    int n=s_nmods;
    if(s_msel>=n) s_msel=n-1;
    /* move: the banner sits above the first row of cards */
    if(s_msel<0){ if(down() && n) s_msel=0; }
    else {
        int pos=s_msel%MPAGE;
        if(left() && pos%3>0) s_msel--;
        if(right() && pos%3<2 && s_msel+1<n) s_msel++;
        if(up()){ if(pos>=3) s_msel-=3; else s_msel=-1; }
        else if(down() && pos<6){ if(s_msel+3<n) s_msel+=3; else if((s_msel/3+1)*3<n) s_msel=n-1; }
        if(pr(B_ZL) && s_msel>=MPAGE) s_msel-=MPAGE;
        if(pr(B_ZR) && (s_msel/MPAGE+1)*MPAGE<n){ s_msel+=MPAGE; if(s_msel>=n) s_msel=n-1; }
    }
    if(pr(B_A) && s_msel<0) stack99_toggle();
    else if(pr(B_A)){
        Mod* m=&s_mods[s_msel];
        if(!m->romfs) aden_snprintf(s_modmsg,sizeof s_modmsg, m->exefs?"%s is a code mod - turn it on in Eden instead":"%s has no romfs folder",m->name);
        else {
            m->on=!m->on; aden_mod_set(m->name,m->on);
            aden_snprintf(s_modmsg,sizeof s_modmsg,"%s is now %s - restart the game to apply",m->name,m->on?"ON":"off");
        }
    }
    /* Minus puts every changed mod back the way it was at start, so no restart is needed */
    if(pr(B_MINUS)){
        int k=0; for(int i=0;i<s_nmods;i++){ Mod* m=&s_mods[i]; if(m->on!=m->on_at_start){ m->on=m->on_at_start; aden_mod_set(m->name,m->on); k++; } }
        aden_snprintf(s_modmsg,sizeof s_modmsg,k?"%d change%s undone - no restart needed":"Nothing to undo",k,k==1?"":"s");
    }
    if(pr(B_Y)){ scan_mods(); if(s_msel>=s_nmods) s_msel=s_nmods-1;   /* the list may have shrunk */
        aden_snprintf(s_modmsg,sizeof s_modmsg,"%d mods found",s_nmods); }
    n=s_nmods;
    at(1,11,C_TEXT,"Stack 99 (built-in)");
    at(2,11,C_DIM,"Fruit, materials, shells stack to 99");
    at_r(1,62,C_HEADDK,"%s",aden_stack99_found()?"live":"next start");
    at(4,1,C_TEXT,"Folder mods  %d",n);
    if(!n){
        at(13,26,C_TEXT,"No mods yet");
        at(15,18,C_DIM,"Copy a romfs mod folder into");
        at(16,18,C_TEXT,"sdmc/aden/mods/<name>/romfs");
    }
    int pg= s_msel<0 ? 0 : s_msel/MPAGE;
    for(int i=0;i<MPAGE && pg*MPAGE+i<n;i++){
        Mod* m=&s_mods[pg*MPAGE+i]; int c=1+22*(i%3), r=6+5*(i/3);
        char ini[3]; initials(m->name,ini);
        at(r,c,C_WHITE,"%s",ini);
        char nm[17]; int l=(int)strlen(m->name); memcpy(nm,m->name,l>16?16:l); nm[l>16?16:l]=0; if(l>16) nm[15]='.';
        at(r+1,c,m->romfs?C_TEXT:C_DIM,"%s",nm);
        if(m->on!=m->on_at_start) at(r+2,c,C_WARN,"restart needed");
        else if(!m->romfs) at(r+2,c,C_DIM,"%s",m->exefs?"code mod: use Eden":"no romfs folder");
        else if(m->exefs) at(r+2,c,C_DIM,"romfs only");
    }
    int pend=mods_pending();
    if(pend){ at(20,2,C_TEXT,"%d change%s for a restart",pend,pend==1?" waits":"s wait"); at_r(20,61,C_TEXT,"- undo"); }
    if(s_modmsg[0]) at(22,0,C_DIM,"%s",s_modmsg);
    text(26,C_DIM,"D-pad move  ZL/ZR page  A on/off  - undo  Y rescan");
    s_status_row=23;
    s_paint=paint_mods;
}

/* ---------- About tab: a rail of two pages (ZL/ZR) - Info and Credits ---------- */
static int s_apage;
static const char* const APAGE[2]={"Info","Credits"};
static u32 state_col(int st){ return st==2 ? C_OK : st==1 ? C_HEAD : st==3 ? C_WARN : C_OFF; }
static void paint_about(void){
    box(16,108,150,440,18,C_WELL);
    box(24,GY(1+2*s_apage)-5,134,30,15,C_WHITE);
    if(s_apage==0){
        box(184,180,366,92,18,C_WHITE);
        dot(210,GY(4)+8,6,state_col(g_player_state));
        dot(210,GY(5)+8,6,state_col(g_player_state==2?g_island_state:0));
        dot(210,GY(6)+8,6,aden_icons_on()?C_OK:C_OFF);
    }
}
static void tab_about(void){
    if(pr(B_ZL)||pr(B_ZR)||up()||down()) s_apage^=1;
    for(int i=0;i<2;i++) at(1+2*i,2,i==s_apage?C_HEADDK:C_TEXT,"%s",APAGE[i]);
    const char* hint="ZL/ZR page   L/R tabs   B close";
    if(s_apage==0){
        big(198,120,1.6f,C_TEXT,"v%s",ADEN_VERSION);
        at(2,21,C_DIM,"for ACNH 3.0.2 / 3.0.3");
        int ps=g_player_state, is=g_island_state, on=aden_icons_on();
        at(4,24,C_TEXT,"Player data");
        if(ps==2){ int n=aden_player_count(); at(4,39,C_DIM,"found, %d player%s",n,n==1?"":"s"); }
        else at(4,39,ps==3?C_WARN:C_DIM,"%s",ps==1?"searching":ps==3?"not found":"not searched yet");
        at(5,24,C_TEXT,"Island");
        at(5,39,ps==2&&is==3?C_WARN:C_DIM,"%s",ps!=2||is==0?"waiting":is==1?"searching":is==2?"found":"not found");
        at(6,24,C_TEXT,"Item icons"); at(6,39,C_DIM,"%s",on?"ON":"off");
        at(9,21,C_HEADDK,"How to use");
        at(10,21,C_DIM,"Open it from the Nook Phone.");
        at(11,21,C_DIM,"Your character is paused while it is open.");
        at(13,21,C_HEADDK,"Item icons");
        at(14,21,C_DIM,"The Items grid shows the game's icons.");
        at(15,21,C_DIM,"If they crash the game, they turn off");
        at(16,21,C_DIM,"by themselves at the next start.");
        if(pr(B_A)){ if(on) aden_icons_disable(); else aden_icons_enable(); }
        hint= on ? "A icons off   ZL/ZR page   L/R tabs   B close" : "A icons on   ZL/ZR page   L/R tabs   B close";
    } else {
        at(1,21,C_DIM,"Made by"); at(2,21,C_TEXT,"AdenDev"); at(3,21,C_TEXT,"Timothe Montfrond (fr-timothe)");
        at(5,21,C_DIM,"Item names and data"); at(6,21,C_TEXT,"NHSE by kwsch");
        at(8,21,C_DIM,"License"); at(9,21,C_TEXT,"GPL-3.0");
        at(11,21,C_DIM,"Found a bug? Send this file:"); at(12,21,C_TEXT,"sdmc/aden/log.txt");
    }
    text(26,C_DIM,hint);
    s_paint=paint_about;
}

#define C_TILE 0xEFE7D2FF
static void draw_grid(void){
    if(s_grid_bar) gfx_rrect(PX0+20,PY0-s_pill_top,350,36,18,0xFFFFFFFF);   /* search bar */
    if(s_famtxt[0]) gfx_rrect(PX0+382,PY0-s_pill_top,168,36,18,C_HEAD);     /* family filter / variant set pill */
    for(int i=0;i<s_grid_n;i++){
        float x=PX0+GX0+(i%GCOLS)*GPITCH, y=PY0-s_grid_y0-(i/GCOLS)*GPITCH;
        if(i==s_grid_sel) gfx_rrect(x-5,y+5,GTILE+10,GTILE+10,22,s_grid_cur?C_PILL:C_HEAD);
        gfx_rect(x,y,GTILE,GTILE,C_TILE);
        /* the item's icon once it is loaded, its family colour until then */
        if(!icon_at(s_grid_id[i],s_grid_fc[i],s_grid_var[i],x+22,y-2,46,46,aden_icon_mode())) gfx_disk(x+GTILE*0.5f,y-27,15,FAMCOL[s_grid_fam[i]]);
        /* three dots in the corner: colours or variants to choose from in the give sheet */
        if(s_grid_nv[i]>1) for(int k=0;k<3;k++) gfx_disk(x+GTILE-10-k*7,y-9,2.5f,C_HEAD);
    }
}
static void draw_grid_text(void){
    for(int c=0;c<GCOLS;c++) gfx_text(PX0+GX0+c*GPITCH+1,PY0-s_grid_y0-50,1.0f,C_TEXT,s_glab[c]);
    if(s_famtxt[0]) gfx_text(PX0+466-strlen(s_famtxt)*4.f,PY0-s_pill_top-10,1.0f,0xFFFFFFFF,s_famtxt);
}
static void draw_chrome(void){
    gfx_prim_begin();
    gfx_rrect(PX0,PY0,PW,PH,34,C_PANEL);                 /* cream card */
    gfx_rrect(PX0,PY0,PW,HEADH,34,C_HEAD);               /* teal header */
    gfx_rect(PX0,PY0-HEADH+34,PW,34,C_HEAD);             /* square off its lower corners */
    /* active tab pill; tabs sit on the 8 px character grid (see draw_tabs_text) */
    int pos=0;
    for(int i=0;i<NTABS;i++){
        if(i==s_tab) gfx_rrect(TABX0+pos*8.f-14,PY0-58,strlen(TABNAME[i])*8.f+28,26,13,0xFFFFFFFF);
        pos+=strlen(TABNAME[i])+5;
    }
    if(s_grid_on) draw_grid();
    for(int i=0;i<s_nicons;i++) draw_icon(TX+9,CY-s_icons[i].row*20-8,s_icons[i].st);
    if(s_hl_row>=0){
        if(s_hl_key>=0) gfx_rrect(TX+s_hl_key*24-2,CY-s_hl_row*20+3,28,22,11,C_PILL);
        else            gfx_rrect(PX0+16,CY-s_hl_row*20+3,PW-32,22,11,C_PILL);
    }
    if(!s_grid_on) gfx_rect(PX0+20,CY-25*20+6,PW-40,2,C_LINE);   /* footer divider (the grid fills that space) */
    if(s_paint) s_paint();
}
/* two prints for the whole tab row: the inactive names (and L/R) padded with spaces, then the active one */
static void draw_tabs_text(void){
    char off[80], on[80]; int pos=0;
    memset(off,' ',sizeof off); memset(on,' ',sizeof on);
    for(int i=0;i<NTABS;i++){ int n=strlen(TABNAME[i]);
        memcpy(i==s_tab?on+pos:off+pos,TABNAME[i],n); pos+=n+5; }
    int lr=(int)((PW-60-(TABX0-PX0))/8); memcpy(off+lr,"L/R",3);
    off[lr+3]=0; on[pos]=0;
    gfx_text(TABX0,PY0-63,1.0f,C_TABOFF,off);
    gfx_text(TABX0,PY0-63,1.0f,C_HEADDK,on);
}
void aden_menu_draw(void){
    extern volatile int g_aden_open_request;
    if(g_aden_open_request && !g_menu_open){ g_menu_open=1; s_armed=0; }
    /* after closing, give input back to the game only once the closing B (and all else) is released */
    if(!g_menu_open){ s_was_open=0; if(g_input_hold && aden_input_take()==0) g_input_hold=0; return; }
    if(!s_was_open){ s_was_open=1; s_seen_search=0; s_done_at=0; sheet_close(); aden_log("menu opened"); }
    poll();
    /* ignore input until every button from the phone tap has been released */
    if(!s_armed){ if(s_now==0) s_armed=1; s_press=0; }
    /* the give sheet stays open across tab changes: back on Items, the same item is still shown */
    if(!s_typing && !s_pedit){
        if(pr(B_L)) s_tab=(s_tab+NTABS-1)%NTABS;
        if(pr(B_R)) s_tab=(s_tab+1)%NTABS;
    }
    s_hl_row=-1; s_hl_key=-1; s_grid_on=0; s_famtxt[0]=0; s_nicons=0; s_frame++; s_paint=0; s_niq=0; s_status_row=ROW0+LIST+4;
    switch(s_tab){
        case TAB_ITEMS:  tab_items(); break;
        case TAB_PLAYER: tab_player(); break;
        case TAB_ISLAND: tab_island(); break;
        case TAB_MODS:   tab_mods(); break;
        case TAB_ABOUT:  tab_about(); break;
    }
    if(s_status[0]) put(s_status_row,0,C_WARN,"%s",s_status);
    draw_chrome();
    gfx_text(TX,PY0-18,1.6f,C_TITLE,"Aden Modmenu Enhance");
    draw_tabs_text();
    flush(TX,CY);
    flush_big();
    if(s_grid_on) draw_grid_text();
    for(int i=0;i<s_niq;i++) aden_icon_draw(s_iq[i].id,s_iq[i].fam,s_iq[i].var,s_iq[i].x,s_iq[i].y,s_iq[i].w,s_iq[i].h,s_iq[i].mode);
    s_niq=0;
    if(pr(B_B) && !s_typing && !s_pedit){ g_input_hold=1; g_menu_open=0; g_aden_open_request=0; s_status[0]=0; aden_log("menu closed"); }
}
