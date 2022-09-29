#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <strsafe.h>

#include "file_utils.h"
#pragma comment(lib, "User32.lib")

using namespace std;
#include <queue>

int FolderLookup(LPCTSTR strFolder, FileEnumFunc cb, void *ctx)
{
    WIN32_FIND_DATA file;
    int cnt = 0;

    TCHAR strPathToSearch[MAX_PATH + 20];
    StringCchPrintf(strPathToSearch, 280, _T("%s\\*"), strFolder);
    size_t len = 0;
    HRESULT res = StringCchLength(strFolder, MAX_PATH, &len);
    if (res != S_OK)
        return 0;

    HANDLE hFile = FindFirstFile(strPathToSearch, &file);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        std::queue<LPTSTR> subDirs;

        do
        {
            // It could be a directory we are looking at
            // if so look into that dir
            if (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (lstrcmpi(file.cFileName, _T(".")) && lstrcmpi(file.cFileName, _T("..")))
                {
                    size_t flen = 0;
                    res = StringCchLength(file.cFileName, MAX_PATH, &flen);
                    if (res != S_OK) {
                        // TODO need show error here?
                        continue;
                    }
                        
                    size_t dir_len = len + flen + 2; // splash + null terminate
                    LPTSTR sub_dir_str = (LPTSTR)malloc(dir_len * sizeof(TCHAR));
                    if (sub_dir_str == NULL) {
                        // TODO need show error here?
                        continue;
                    }
                    StringCchPrintf(sub_dir_str, dir_len, _T("%s\\%s"), strFolder, file.cFileName);
                    subDirs.push(sub_dir_str);
                }
            }
            else
            {
                TCHAR strTheNameOfTheFile[MAX_PATH + 20];
                cnt++;
                StringCchPrintf(strTheNameOfTheFile, 280, _T("%s\\%s"), strFolder, file.cFileName);
                // process the found file                
                ULONG64 nFileSize = file.nFileSizeHigh;
                nFileSize <<= 32;
                nFileSize |= file.nFileSizeLow;
                cb(strTheNameOfTheFile, nFileSize, ctx);
            }
        } while (FindNextFile(hFile, &file));

        FindClose(hFile);
        // find in all subdirectories
        while (!subDirs.empty()) {
            LPTSTR& dir = subDirs.front();
            subDirs.pop();
            cnt += FolderLookup(dir, cb, ctx);
            // free dir
            free(dir);
        }
    }

    return cnt;
}