#pragma once
using namespace std;
#include <queue>
#include <mutex>

#define PROCESS_BUFFER_SIZE     1024 * 1024 // 1MB buffer

typedef char* fname_t;

typedef struct __pattern_s {
    const char* ptr;
    uint16_t len;
    uint8_t pt_asterisk_cnt;
    uint8_t pad[5];
} pattern_t;

typedef struct __scan_ctx_s {
    char fname[MAX_PATH * 2]; // in UTF-8 from
    char pattern[512];
    uint32_t cur_line;
    std::queue<TCHAR *> *filequeue;
    volatile int file_pushed;
    volatile int file_poped;
    int file_processed;
    int thread_idx;
    ULONG64 bytes_scanned;
    pattern_t pt; // formated pattern
} scan_ctx_t;

void LogFileScan(TCHAR* strFileName, void* ctx);
extern scan_ctx_t* g_scan_ctx;
extern volatile int g_lookup_files_done;
extern std::mutex g_list_mutex;
