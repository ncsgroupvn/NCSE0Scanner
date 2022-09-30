// main.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include <CommCtrl.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <vector>
#include <strsafe.h>
#include <shellapi.h>

#include "main.h"
#include "define.h"
#include "file_utils.h"
#include "log_scan.h"
#include "utils.h"

using namespace std;
#include <queue>
#include <mutex>
#include <atomic>

#define MAX_LOADSTRING 100

#pragma comment(linker, \
  "\"/manifestdependency:type='Win32' "\
  "name='Microsoft.Windows.Common-Controls' "\
  "version='6.0.0.0' "\
  "processorArchitecture='*' "\
  "publicKeyToken='6595b64144ccf1df' "\
  "language='*'\"")

#pragma comment(lib, "ComCtl32.lib")

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

std::string utf8_encode(LPCTSTR in_str)
{
#ifdef _UNICODE
    if (in_str == NULL) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, in_str, lstrlenW(in_str), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, in_str, lstrlenW(in_str), &strTo[0], size_needed, NULL, NULL);
#else
    std::string strTo = wstr;
#endif
    return strTo;
}

std::mutex log_mutex;

const char* GetLogFileName(int type)
{
    switch (type) {
    case LOG_MATCHED:
        return MATCH_LOG_FILE_NAME;
    default:
        return LOG_FILE_NAME;
    }

    return LOG_FILE_NAME;
}

void LogT(int type, LPCTSTR text)
{
    log_mutex.lock();
    std::ofstream log_file(LOG_FILE_NAME, std::ios_base::out | std::ios_base::app);
    char out_text[1024];
    out_text[0] = '\0';
#ifdef _UNICODE
    // convert to UTF-8
    std::string s = utf8_encode(text);
    snprintf(out_text, 1024, "%s\n", &s[0]);
#else
    snprintf(out_text, 1024, "%s\n", text);
#endif
    
    size_t len = strlen(out_text);    
    if (len > 0) {
        log_file.write((const char *)out_text, len);
    }
    log_mutex.unlock();
}

void Log(int type, const char * text)
{
    log_mutex.lock();
    std::ofstream log_file(GetLogFileName(type), std::ios_base::out | std::ios_base::app);    
    size_t len = strlen(text);
    if (len > 0) {        
        log_file.write((const char*)text, len);
    }
    log_mutex.unlock();
}

void LogF(int type, const char* _Format, ...)
{
    va_list _ArgList;    
    char out_text[2048];
    __crt_va_start(_ArgList, _Format);
    vsnprintf(out_text, 2048, _Format, _ArgList);
    __crt_va_end(_ArgList);

    Log(type, out_text);
}

int EnumWriteLog(TCHAR* strFileName, ULONG64 nFileSize, void* ctx) {
    LogT(LOG_APP, strFileName);
    return 1;
}

int g_files_cnt = 0;
volatile ULONG64 g_bytes_cnt = 0;
int n_worker_threads = 2;
volatile int g_cancel = 0;
volatile int init_progres_bar = 0;
volatile int manager_thread_done = 0;
std::atomic<long> g_matched_cnt(0);

static inline DWORD bytesToMB(ULONG64 bytes)
{
    return (DWORD)(bytes >> 20);
}

int EnumScanLog(TCHAR* strFileName, ULONG64 nFileSize, void* ctx) {
    scan_ctx_t* scan_ctx = (scan_ctx_t*)ctx;
    std::string s = utf8_encode(strFileName);

    LogF(LOG_APP, "Found file: %s with size %I64d bytes\n", &s[0], nFileSize);

    TCHAR * fname = (TCHAR *)malloc(MAX_PATH * sizeof(TCHAR));
    if (fname == NULL) {
        LogF(LOG_APP, "Malloc memory failed for file name %s\n", &s[0]);
        return 1;
    }

    StringCchCopy(fname, MAX_PATH, strFileName);
    int thread_idx = g_files_cnt % n_worker_threads;
    g_files_cnt++;
    g_bytes_cnt += nFileSize;

    // put this file to right context of worker thread
    g_list_mutex.lock();
    scan_ctx[thread_idx].filequeue->push(fname);
    scan_ctx[thread_idx].file_pushed++;
    g_list_mutex.unlock();

    return 1;
}

INT_PTR CALLBACK ScanDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
DWORD WINAPI LogScanThreadFunction(LPVOID lpParam);

void InitProgressBar(HWND hDlg)
{
    HWND hPB = GetDlgItem(hDlg, IDC_PROGRESS_SCAN);

    PostMessage(hPB, PBM_SETRANGE, 0, MAKELPARAM(0, bytesToMB(g_bytes_cnt)));
    init_progres_bar = 1;
}

void UpdateStatus(HWND hDlg)
{
    // get data from g_scan_ctx to feed to progress bar
    int total_processed = 0;
    ULONG64 total_bytes_scanned = 0;

    if (g_lookup_files_done) {
        if (init_progres_bar == 0) {
            // init progress bar dialog
            InitProgressBar(hDlg);
        }
        else {
            if (manager_thread_done == 0) {
                for (int i = 0;i < n_worker_threads;i++) {
                    total_processed += g_scan_ctx[i].file_processed;
                    total_bytes_scanned += g_scan_ctx[i].bytes_scanned;
                }
                HWND hPB = GetDlgItem(hDlg, IDC_PROGRESS_SCAN);
                PostMessage(hPB, PBM_SETPOS, (WPARAM)bytesToMB(total_bytes_scanned), 0);
                TCHAR strStatus[128];
                StringCchPrintf(strStatus, 128, _T("Scanning %d/%d files, %d/%d MB"),
                    total_processed, g_files_cnt, bytesToMB(total_bytes_scanned), bytesToMB(g_bytes_cnt));
                SetDlgItemText(hDlg, IDC_SCAN_STATUS, strStatus);
            }
        }
    }
}

typedef struct __manager_ctx_s {
    LPTSTR strLogFolder;
    LPCTSTR strPattern;
} manager_ctx_t;

DWORD WINAPI ManagerThreadFunction(LPVOID lpParam)
{
    manager_ctx_t* ctx = (manager_ctx_t*)lpParam;

    DWORD   dwThreadIdArray[MAX_THREADS];
    HANDLE  hThreadArray[MAX_THREADS];
    std::queue<TCHAR*> filelist[MAX_THREADS];

    char utf8_pattern[2048];
#ifdef _UNICODE
    // convert to UTF-8
    std::string s = utf8_encode(ctx->strPattern);
    snprintf(utf8_pattern, 2048, "%s", &s[0]);
#else
    snprintf(utf8_pattern, 2048, "%s", strPattern);
#endif

    g_lookup_files_done = 0;
    g_files_cnt = 0;
    g_bytes_cnt = 0;
    g_matched_cnt = 0;
    // init memory for all worker thread
    g_scan_ctx = (scan_ctx_t*)malloc(sizeof(scan_ctx_t) * n_worker_threads);
    if (g_scan_ctx == NULL) {
        LogF(LOG_APP, "Error when allocate memory for scan context\n");
        MessageBox(NULL, _T("Not enough memory, please retry later"), DEFAULT_APP_TITLE, MB_ICONERROR | MB_OK);
        return 0;
    }
    memset(g_scan_ctx, 0, sizeof(scan_ctx_t) * n_worker_threads);

    for (int i = 0;i < n_worker_threads;i++) {
        snprintf(g_scan_ctx[i].pattern, 512, "%s", utf8_pattern);
        g_scan_ctx[i].filequeue = &filelist[i];
        g_scan_ctx[i].thread_idx = i;
        // init a worker thread
        hThreadArray[i] = CreateThread(
            NULL,                   // default security attributes
            0,                      // use default stack size  
            LogScanThreadFunction,  // thread function name
            &g_scan_ctx[i],         // argument to thread function 
            0,                      // use default creation flags 
            &dwThreadIdArray[i]);   // returns the thread identifier 

        if (hThreadArray[i] == NULL)
        {
            LogF(LOG_APP, "Error when create thread %d\n", i + 1);
        }
    }

    // lookup folder and put file to thread safe queue to all workers can process it
    FolderLookup(ctx->strLogFolder, EnumScanLog, g_scan_ctx);
    g_lookup_files_done = 1;

    // Wait until all threads have terminated.
    WaitForMultipleObjects(n_worker_threads, hThreadArray, TRUE, INFINITE);
    manager_thread_done = 1;
    Sleep(2);
    // cleanup resources
    for (int i = 0; i < n_worker_threads; i++)
    {
        if (hThreadArray[i] != 0)
            CloseHandle(hThreadArray[i]);
    }

    free(g_scan_ctx);
    g_scan_ctx = NULL;
    ExitThread(0);
    return 0;
}

void showScanDlg(HWND hDlgParent, LPTSTR strLogFolder, LPCTSTR strPattern)
{
    MSG msg;
    BOOL ret;
    DWORD   dwThreadId = 0;
    HANDLE  hThread = NULL;
    manager_ctx_t mctx;

    mctx.strLogFolder = strLogFolder;
    mctx.strPattern = strPattern;
    manager_thread_done = 0;
    init_progres_bar = 0;
    g_lookup_files_done = 0;
    // init a worker thread
    hThread = CreateThread(
        NULL,                   // default security attributes
        0,                      // use default stack size  
        ManagerThreadFunction,  // thread function name
        &mctx,                  // argument to thread function 
        0,                      // use default creation flags 
        &dwThreadId);           // returns the thread identifier 

    if (hThread == NULL) {
        LogF(LOG_APP, "Error when create scan thread\n");
    }

    HWND hDlg = CreateDialogParam((HINSTANCE)GetModuleHandle(NULL), MAKEINTRESOURCE(IDD_DIALOG_SCANNING), 0, ScanDialogProc, 0);
    ShowWindow(hDlg, SW_SHOW);
    
    while (1) {
        UpdateStatus(hDlg);
        ret = GetMessage(&msg, 0, 0, 0);
        if ((ret == 0) || (ret == -1))
            break;

        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (manager_thread_done)
            break;
        Sleep(20);
    }

    ShowWindow(hDlg, SW_HIDE);
    DestroyWindow(hDlg);
    CloseHandle(hThread);
}

void onScan(HWND hDlg)
{
    TCHAR strLogFolder[MAX_PATH];
#ifdef ALLOW_INPUT_FILTER
    TCHAR strPattern[1024];
#endif

    char strWorkers[1024];
    
    GetDlgItemText(hDlg, IDC_EDIT_LOG_FOLDER, strLogFolder, MAX_PATH);
#ifdef ALLOW_INPUT_FILTER
    GetDlgItemText(hDlg, IDC_EDIT_FILTER, strPattern, 1024);
#endif

    GetDlgItemTextA(hDlg, IDC_EDIT_THREADS, strWorkers, 128);
    int n_workers = atoi(strWorkers);
    if ((n_workers <= 0) || (n_workers > MAX_THREADS)) {
        MessageBox(hDlg, _T("Please enter number of worker threads in range from 1 to 8"), DEFAULT_APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }

    n_worker_threads = n_workers;
    g_matched_cnt = 0;

    if (lstrlen(strLogFolder) == 0) {
        MessageBox(hDlg, _T("Please enter folder to scan log."), DEFAULT_APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }
#ifdef ALLOW_INPUT_FILTER
    if (lstrlen(strPattern) == 0) {
        MessageBox(hDlg, _T("Please enter filter to scan log."), DEFAULT_APP_TITLE, MB_ICONINFORMATION | MB_OK);
        return;
    }
#endif


    std::string s = utf8_encode(strLogFolder);
    char folder[520];
    snprintf(folder, 520, "%s", &s[0]);

    string stime = getCurrentDateTime("now");
    char stime_str[80];
    snprintf(stime_str, 80, "%s", &stime[0]);

    ULONGLONG start_ms = GetTickCount64();
    LogF(LOG_APP, "Start scan folder %s at %s (at tick %I64d)\n", folder, stime_str, start_ms);

#ifdef ALLOW_INPUT_FILTER
    showScanDlg(hDlg, strLogFolder, strPattern);
#else
    showScanDlg(hDlg, strLogFolder, _T("*/powershell*autodiscover.json*200*"));
#endif

    ULONGLONG end_ms = GetTickCount64();
    stime = getCurrentDateTime("now");
    snprintf(stime_str, 80, "%s", &stime[0]);
    LogF(LOG_APP, "Done scan folder %s at %s (at tick %I64d), total in %I64d ms\n",
        folder, stime_str, end_ms, end_ms - start_ms);

    if (g_matched_cnt == 0)
        MessageBox(NULL, _T("Finish scan all log files for Microsoft Exchange 0-day vulnerability.\nSee details result in file e0_scan.log"), DEFAULT_APP_TITLE, MB_OK);
    else
        MessageBox(NULL, _T("Detected signs of exploit use Microsoft Exchange 0-day vulnerability.\nSee details result in file e0_matched.log and e0_scan.log"), DEFAULT_APP_TITLE, MB_OK);
}

void InitAtMiddle(HWND hWnd)
{
    int screenwidth, screenheight;
    int dlgwidth, dlgheight, x, y;
    RECT rect;

    /* Get Screen width and height */
    screenwidth = GetSystemMetrics(SM_CXSCREEN);
    screenheight = GetSystemMetrics(SM_CYSCREEN);

    GetWindowRect(hWnd, &rect);

    /* Calculate Window width and height */
    dlgwidth = rect.right - rect.left;
    dlgheight = rect.bottom - rect.top;

    /* Calculate Window left, top (x,y) */
    x = (screenwidth - dlgwidth) / 2;
    y = (screenheight - dlgheight) / 2;

    /* Reposition Window left, top (x,y) */
    SetWindowPos(hWnd, NULL, x, y, 0, 0, SWP_NOSIZE);
}

void SetDlgIcon(HWND hwnd) {
    HICON hIcon;

    hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL),
        MAKEINTRESOURCEW(IDI_MAINICON),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        0);
    if (hIcon)
    {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }
}

void OpenNCSHomepage(HWND hDlg)
{    
    ShellExecuteA(NULL, "open", "http://ncsgroup.vn", NULL, NULL, SW_SHOWDEFAULT);
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        SetWindowText(hDlg, DEFAULT_APP_TITLE);
        InitAtMiddle(hDlg);
        SetDlgIcon(hDlg);
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            SendMessage(hDlg, WM_CLOSE, 0, 0);
            return TRUE;
        case IDOK:
            onScan(hDlg);
            break;
        case IDC_NCS_HOMEPAGE:
            OpenNCSHomepage(hDlg);
            return TRUE;
            break;
        }
        break;
    case WM_CTLCOLORSTATIC:
        // Set the colour of the text for our URL
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_NCS_HOMEPAGE))
        {
            // we're about to draw the static
            // set the text colour in (HDC)lParam
            SetBkMode((HDC)wParam, TRANSPARENT);
            SetTextColor((HDC)wParam, RGB(0, 0, 255));
            // NOTE: per documentation as pointed out by selbie, GetSolidBrush would leak a GDI handle.
            return (INT_PTR)GetSysColorBrush(COLOR_MENU);
        }
        break;

    case WM_CLOSE:        
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    }

    return FALSE;
}

int WINAPI _tWinMain(HINSTANCE hInst, HINSTANCE h0, LPTSTR lpCmdLine, int nCmdShow)
{
    HWND hDlg;
    MSG msg;
    BOOL ret;

    InitCommonControls();
    hDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_DIALOG_MAIN), 0, DialogProc, 0);
    ShowWindow(hDlg, nCmdShow);

#ifdef ALLOW_INPUT_FILTER
    SetDlgItemTextA(hDlg, IDC_EDIT_FILTER, sFilterConf.c_str());
#endif
    SetDlgItemTextA(hDlg, IDC_EDIT_LOG_FOLDER, DEFAULT_LOG_PATH);
    SetDlgItemInt(hDlg, IDC_EDIT_THREADS, n_worker_threads, false);

    while ((ret = GetMessage(&msg, 0, 0, 0)) != 0) {
        if (ret == -1)
            return -1;

        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return 0;
}