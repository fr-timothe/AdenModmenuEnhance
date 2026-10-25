#pragma once
typedef unsigned long u64; typedef long s64; typedef unsigned int u32; typedef int s32;
typedef unsigned short u16; typedef short s16; typedef unsigned char u8; typedef signed char s8;
typedef u64 uptr; typedef u64 size_t; typedef u32 Handle; typedef u32 Result;
#define NULL ((void*)0)
#define HIDDEN __attribute__((visibility("hidden")))
#define ALIGN_DOWN(x,a) ((x)&~((u64)(a)-1))
#define ALIGN_UP(x,a) ALIGN_DOWN((x)+(a)-1,(a))
#define PAGE 0x1000UL
#define CUR_PROCESS_HANDLE 0xFFFF8001
#define ADEN_VERSION "0.6.0-beta.12"   /* release version: start-up log and About tab */

/* search progress shown by the menu's loading page (player.c writes, menu.c reads) */
enum { STEP_MEM, STEP_GPU, STEP_STATS, STEP_PERS, STEP_ISLAND, NSTEPS };
enum { ST_WAIT, ST_RUN, ST_OK, ST_FAIL, ST_SKIP };
extern volatile int g_step[NSTEPS];
extern volatile u32 g_step_n[NSTEPS];    /* what the step found: players, stat fields, copies, saves */
extern volatile u32 g_scan_mb;           /* memory read so far by the running scan */

typedef struct { u64 addr, size; u32 type, attr, perm, ipc_refcount, device_refcount, padding; } MemoryInfo;

/* svc.s */
Result svcQueryMemory(MemoryInfo*, u32*, u64);
Result svcGetInfo(u64*, u32, Handle, u64);
Result svcMapProcessMemory(void*, Handle, u64, u64);
Result svcUnmapProcessMemory(void*, Handle, u64, u64);
Result svcOutputDebugString(const char*, u64);
Result svcCreateSession(Handle*, Handle*, u32, u64);
Result svcReplyAndReceive(s32*, const Handle*, s32, Handle, u64);
Result svcSendSyncRequest(Handle);
Result svcCreateThread(Handle*, void*, void*, void*, int, int);
Result svcStartThread(Handle);
void   svcExitThread(void);
Result svcCloseHandle(Handle);
Result svcWaitSynchronization(s32*, const Handle*, s32, u64);
void*  armGetTls(void);
void   aden_cache_flush(uptr rw, uptr rx, u64 size);

/* mini libc (util.c) */
void* memcpy(void*, const void*, size_t);
void* memset(void*, int, size_t);
void* memmove(void*, const void*, size_t);
int   memcmp(const void*, const void*, size_t);
size_t strlen(const char*);
int   strcmp(const char*, const char*);
int   aden_vsnprintf(char*, size_t, const char*, __builtin_va_list);
int   aden_snprintf(char*, size_t, const char*, ...);

/* log.c */
void aden_log(const char* fmt, ...);
void aden_log_attach_sd(void);   /* call once nn::fs is usable */
int  aden_sd_ready(void);
void aden_log_flush(void);       /* write pending SD lines; called once per frame */

/* hook.c */
int   aden_hook_init(void);
/* Redirect `site` to `fn`. If orig != NULL, *orig gets a trampoline that runs the original code. */
int   aden_hook(uptr site, void* fn, void** orig);
int   aden_patch32(uptr site, u32 insn);

/* module layout */
extern uptr g_main_base, g_main_text_size;
