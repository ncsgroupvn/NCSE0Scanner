#include "framework.h"
#include <CommCtrl.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <vector>
#include <strsafe.h>

#include "main.h"
#include "define.h"
#include "file_utils.h"
#include "log_scan.h"
#include "utils.h"

extern volatile int g_cancel;
void InitAtMiddle(HWND hWnd);
void SetDlgIcon(HWND hwnd);

INT_PTR CALLBACK ScanDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG:
        SetWindowText(hDlg, DEFAULT_APP_TITLE);
        InitAtMiddle(hDlg);
        SetDlgIcon(hDlg);
        g_cancel = 0;
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDCANCEL:
            //SendMessage(hDlg, WM_CLOSE, 0, 0);
            EndDialog(hDlg, 0);
            g_cancel = 1;
            return TRUE;
        }
        break;

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return TRUE;
    }

    return (DefWindowProcW(hDlg, uMsg, wParam, lParam));
}