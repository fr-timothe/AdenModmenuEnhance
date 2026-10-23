#include "nn.h"
/* Log lines go to svcOutputDebugString immediately, and to sd:/aden/log.txt
   once the SD card is mounted (earlier lines are buffered and written then).
   SD lines are appended to one of two buffers under the spinlock; a single
   writer (s_busy) swaps buffers and writes the full one outside the lock. */
static char s_early[16384]; static u32 s_early_len;
static char s_buf[2][8192]; static u32 s_len; static int s_cur;
static int  s_sd, s_busy; static s64 s_off; static u64 s_last;
static volatile int s_lock;
#define LOG_PATH  "adensd:/aden/log.txt"
#define LOG_BATCH 2048          /* buffered bytes that trigger a write */
#define LOG_IDLE  19200000ull   /* 1 s of ticks: older buffered lines are written by the next line */

static void lock(void){ while(__atomic_exchange_n(&s_lock,1,__ATOMIC_ACQUIRE)); }
static void unlock(void){ __atomic_store_n(&s_lock,0,__ATOMIC_RELEASE); }

static void sd_write(const char* s, u32 n){
    FsFile f; if(nn_fs_OpenFile(&f, LOG_PATH, FS_WRITE|FS_APPEND)) return;
    FsWriteOption o={1}; nn_fs_WriteFile(f, s_off, s, n, &o); s_off+=n; nn_fs_CloseFile(f);
}
HIDDEN int aden_sd_ready(void){ return s_sd; }
HIDDEN void aden_log_attach_sd(void){
    if(s_sd) return;
    Result r = nn_fs_MountSdCardForDebug("adensd");
    if(r) r = nn_fs_MountSdCard("adensd");
    if(r){ char b[64]; int n=aden_snprintf(b,64,"[aden] SD mount failed %x\n",r); svcOutputDebugString(b,n); return; }
    nn_fs_CreateDirectory("adensd:/aden");
    nn_fs_DeleteFile(LOG_PATH);
    nn_fs_CreateFile(LOG_PATH, 0);
    lock(); s_sd=1; s_busy=1; s_off=0; unlock();   /* s_early is frozen once s_sd is set */
    sd_write(s_early, s_early_len);
    lock(); s_busy=0; unlock();
}
HIDDEN void aden_log(const char* fmt, ...){
    char b[512]; int n=aden_snprintf(b,sizeof b,"[%lu] ",nn_os_GetSystemTick()/19200);
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    n+=aden_vsnprintf(b+n,sizeof b-n-1,fmt,ap); __builtin_va_end(ap);
    if(n>(int)sizeof b-2) n=sizeof b-2;
    b[n++]='\n'; b[n]=0;
    svcOutputDebugString(b,n);
    for(;;){
        lock();
        if(!s_sd){ if(s_early_len+n<sizeof s_early){ memcpy(s_early+s_early_len,b,n); s_early_len+=n; } unlock(); return; }
        if(s_len+n<=sizeof s_buf[0]){ memcpy(s_buf[s_cur]+s_len,b,n); s_len+=n; n=0; }
        u64 t=nn_os_GetSystemTick();
        if(!s_busy && s_len && (n || s_len>=LOG_BATCH || t-s_last>=LOG_IDLE)){
            const char* p=s_buf[s_cur]; u32 len=s_len;
            s_busy=1; s_cur^=1; s_len=0; s_last=t; unlock();
            sd_write(p,len);                       /* only the s_busy owner touches s_off and p */
            lock(); s_busy=0;
        }
        unlock();
        if(!n) return;
        nn_os_SleepThread(1000000);                /* full while another thread writes: wait, never drop */
    }
}
