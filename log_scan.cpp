#include "framework.h"
#include <tchar.h>
#include <stdio.h>
#include <string>
#include "log_scan.h"
#include "utils.h"
#include "matcher.h"

#include <atomic>

scan_ctx_t *g_scan_ctx = NULL;
volatile int g_lookup_files_done = 0;
std::mutex g_list_mutex;

// not printable character
static inline int not_printable_char(char c)
{
    if ((c == '\r') || (c == '\n') || (c == ' ') || (c == '\t'))
        return 1;

    return 0;
}

static inline int is_linebreak(char c) {
    if ((c == '\r') || (c == '\n'))
        return 1;

    return 0;
}

void format_pattern(void* ctx)
{
    scan_ctx_t* scan_ctx = (scan_ctx_t*)ctx;
    scan_ctx->cur_line = 1; // reset line counter
    scan_ctx->pt.ptr = scan_ctx->pattern;
    scan_ctx->pt.pt_asterisk_cnt = 0;
    scan_ctx->pt.len = (uint16_t)strlen(scan_ctx->pattern);
    uint16_t off = 0;
    while (off < scan_ctx->pt.len) {
        if (scan_ctx->pattern[off] == '*')
            scan_ctx->pt.pt_asterisk_cnt++;
        off++;
    }
}

// convert str to lowercase string and return string len
uint32_t tolowercase_strlen(char* str)
{
    uint32_t len = 0;
    char* pos = str;
    while (*str)
    {
        *pos++ = tolower(*str++);
        len++;
    }

    return len;
}

extern std::atomic<long> g_matched_cnt;

void do_scan_pattern(char* buf, size_t len, void* ctx)
{
    scan_ctx_t* scan_ctx = (scan_ctx_t*)ctx;
    pattern_t* pt = &scan_ctx->pt;

    if (wc_matcher_i(buf, (int)len, pt->ptr, pt->len, pt->pt_asterisk_cnt) == 0) {
        // match a line
        LogF(LOG_MATCHED, "Thread %d: Match in file \"%s\" at line %d: \"%s\"\n",
            scan_ctx->thread_idx, scan_ctx->fname, scan_ctx->cur_line, buf);
        g_matched_cnt++;
    }
}

void scan_buffer(char* buf, size_t sz, size_t* not_processed, void* ctx)
{
    size_t last_start_line_off = 0;
    size_t off = 0;
    size_t scaned_off = 0;
    scan_ctx_t* scan_ctx = (scan_ctx_t*)ctx;
    char lb;

    while (off < sz) {
        // find end of line
        if (is_linebreak(buf[off])) {
            lb = buf[off];
            // set this bytes to 0
            buf[off++] = '\0';
            // do scan from last_start_line_off
            do_scan_pattern(buf + last_start_line_off, off - last_start_line_off, ctx);
            if (lb == '\n')
                scan_ctx->cur_line++;
            // move to next line with trim process
            while (is_linebreak(buf[off]) && (off < sz)) {
                if (buf[off] == '\n')
                    scan_ctx->cur_line++;
                off++;
            }
            scaned_off = off;
            if (off < sz) {
                last_start_line_off = off;
            }
            else {
                break;
            }
        }
        else {
            off++;
        }
    }
    scan_ctx->bytes_scanned += scaned_off;
    // not processed check
    *not_processed = sz - scaned_off;
    if (scaned_off < sz) {
        // move not processed data to start of buffer
        memmove(buf, buf + scaned_off, sz - scaned_off);
        buf[sz - scaned_off] = '\0'; // null terminated
    }
}

void LogFileScan(TCHAR* strFileName, void* ctx)
{
    FILE* f = NULL;
    errno_t err = _tfopen_s(&f, strFileName, _T("rb"));
    if ((err != 0) || (f == NULL)) {
        LogF(LOG_APP, "Error when open file\n");
        return;
    }

    char* scan_buff = (char *)malloc(PROCESS_BUFFER_SIZE + 1);
    if (scan_buff == NULL) {
        LogF(LOG_APP, "Can't malloc memory for process buffer\n");
        fclose(f);
        return;
    }

    scan_buff[PROCESS_BUFFER_SIZE] = '\0'; // null terminated
    // format pattern
    format_pattern(ctx);
    // process file content
    size_t buffered = 0;
    size_t len = 0;
    while (!feof(f)) {
        len = fread(scan_buff + buffered, 1, PROCESS_BUFFER_SIZE - buffered, f);
        if (len > 0) {
            scan_buffer(scan_buff, len + buffered, &buffered, ctx);
        }
    }
    // processed not processed data
    if (buffered > 0) {
        // append end of line to it
        scan_buff[buffered] = '\n';
        scan_buffer(scan_buff, buffered, &buffered, ctx);
    }    
    // free memory and close file
    free(scan_buff);
    fclose(f);
}

inline string getCurrentDateTime(string s)
{
    time_t now = time(0);
    struct tm  tstruct;
    char  buf[80];
    localtime_s(&tstruct, &now);
    if (s == "now")
        strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    else if (s == "date")
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);

    buf[79] = 0;

    return string(buf);
};

std::string utf8_encode(LPCTSTR in_str);

void ScanFile(TCHAR* filename, scan_ctx_t* scan_ctx)
{
    string stime = getCurrentDateTime("now");
    char stime_str[80];
    snprintf(stime_str, 80, "%s", &stime[0]);

    std::string s = utf8_encode(filename);
    snprintf(scan_ctx->fname, 520, "%s", &s[0]);

    ULONGLONG start_ms = GetTickCount64();
    LogF(LOG_APP, "Thread %d: Start scan file %s at %s (at tick %I64d)\n",
        scan_ctx->thread_idx,scan_ctx->fname, stime_str, start_ms);

    LogFileScan(filename, scan_ctx);

    ULONGLONG end_ms = GetTickCount64();
    stime = getCurrentDateTime("now");
    snprintf(stime_str, 80, "%s", &stime[0]);
    LogF(LOG_APP, "Thread %d: Done scan file %s at %s (at tick %I64d), total in %I64d ms\n",
        scan_ctx->thread_idx, scan_ctx->fname, stime_str, end_ms, end_ms - start_ms);
}

extern volatile int g_cancel;

DWORD WINAPI LogScanThreadFunction(LPVOID lpParam)
{
    scan_ctx_t* scan_ctx = (scan_ctx_t*)lpParam;
    std::queue<TCHAR *>* filequeue = scan_ctx->filequeue;

    while (1) {
        if (scan_ctx->file_poped < scan_ctx->file_pushed) {
            g_list_mutex.lock();
            TCHAR *fname = filequeue->front();
            filequeue->pop();
            scan_ctx->file_poped++;
            g_list_mutex.unlock();
            ScanFile(fname, scan_ctx);
            scan_ctx->file_processed++;
            free(fname);
            if (g_cancel)
                break;
        }
        if (g_lookup_files_done && (scan_ctx->file_poped == scan_ctx->file_pushed))
            break;
    }

    ExitThread(0);
    return 0;
}