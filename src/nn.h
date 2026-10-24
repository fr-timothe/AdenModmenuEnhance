#pragma once
#include "aden.h"
/* Nintendo SDK functions, resolved by rtld from the game's `sdk` module. */
typedef struct { void* h; } FsFile;
typedef struct { int flags; } FsWriteOption;
enum { FS_READ = 1, FS_WRITE = 2, FS_APPEND = 4 };
Result nn_fs_MountSdCardForDebug(const char*) __asm__("_ZN2nn2fs19MountSdCardForDebugEPKc");
Result nn_fs_MountSdCard(const char*)         __asm__("_ZN2nn2fs11MountSdCardEPKc");
Result nn_fs_CreateDirectory(const char*)     __asm__("_ZN2nn2fs15CreateDirectoryEPKc");
Result nn_fs_CreateFile(const char*, s64)     __asm__("_ZN2nn2fs10CreateFileEPKcl");
Result nn_fs_DeleteFile(const char*)          __asm__("_ZN2nn2fs10DeleteFileEPKc");
Result nn_fs_OpenFile(FsFile*, const char*, int) __asm__("_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci");
Result nn_fs_ReadFile(FsFile, s64, void*, u64)   __asm__("_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm");
Result nn_fs_WriteFile(FsFile, s64, const void*, u64, const FsWriteOption*) __asm__("_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE");
Result nn_fs_GetFileSize(s64*, FsFile)        __asm__("_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE");
Result nn_fs_FlushFile(FsFile)                __asm__("_ZN2nn2fs9FlushFileENS0_10FileHandleE");
void   nn_fs_CloseFile(FsFile)                __asm__("_ZN2nn2fs9CloseFileENS0_10FileHandleE");
u64    nn_os_GetSystemTick(void)              __asm__("_ZN2nn2os13GetSystemTickEv");

/* directories */
typedef struct { void* h; } FsDir;
typedef struct { char name[0x301]; u8 _pad[3]; u8 type; u8 _pad2[3]; s64 size; } FsDirEntry; /* type 0=dir 1=file */
Result nn_fs_OpenDirectory(FsDir*, const char*, int)       __asm__("_ZN2nn2fs13OpenDirectoryEPNS0_15DirectoryHandleEPKci");
Result nn_fs_ReadDirectory(s64*, FsDirEntry*, FsDir, s64)  __asm__("_ZN2nn2fs13ReadDirectoryEPlPNS0_14DirectoryEntryENS0_15DirectoryHandleEl");
void   nn_fs_CloseDirectory(FsDir)                          __asm__("_ZN2nn2fs14CloseDirectoryENS0_15DirectoryHandleE");
Result nn_fs_RenameDirectory(const char*, const char*)      __asm__("_ZN2nn2fs15RenameDirectoryEPKcS2_");
/* threads */
typedef struct { u8 data[0x1C0]; } __attribute__((aligned(16))) OsThread;
Result nn_os_CreateThread(OsThread*, void(*)(void*), void*, void*, u64, int) __asm__("_ZN2nn2os12CreateThreadEPNS0_10ThreadTypeEPFvPvES3_S3_mi");
void   nn_os_StartThread(OsThread*) __asm__("_ZN2nn2os11StartThreadEPNS0_10ThreadTypeE");
void   nn_os_SleepThread(s64 ns)    __asm__("_ZN2nn2os11SleepThreadENS_8TimeSpanE");
void   nn_os_WaitThread(OsThread*)  __asm__("_ZN2nn2os10WaitThreadEPNS0_10ThreadTypeE");
void   nn_os_DestroyThread(OsThread*) __asm__("_ZN2nn2os13DestroyThreadEPNS0_10ThreadTypeE");
/* hid */
typedef struct { s64 sampling; u64 buttons; s32 lx, ly, rx, ry; u32 attr; u32 _pad; } NpadState;
int nn_hid_GetNpadStates_FullKey (NpadState*, int, const u32*) __asm__("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadFullKeyStateEiRKj");
int nn_hid_GetNpadStates_Handheld(NpadState*, int, const u32*) __asm__("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadHandheldStateEiRKj");
int nn_hid_GetNpadStates_JoyDual (NpadState*, int, const u32*) __asm__("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyDualStateEiRKj");
int nn_hid_GetNpadStates_JoyLeft (NpadState*, int, const u32*) __asm__("_ZN2nn3hid13GetNpadStatesEPNS0_16NpadJoyLeftStateEiRKj");
int nn_hid_GetNpadStates_JoyRight(NpadState*, int, const u32*) __asm__("_ZN2nn3hid13GetNpadStatesEPNS0_17NpadJoyRightStateEiRKj");
/* heap (exported by the game's main) */
void* malloc(size_t); void free(void*);
/* add-on content (DLC) the user owns: indices relative to the game's first add-on title id */
int nn_aoc_CountAddOnContent(void)          __asm__("_ZN2nn3aoc17CountAddOnContentEv");
int nn_aoc_ListAddOnContent(int*, int, int) __asm__("_ZN2nn3aoc16ListAddOnContentEPiii");
