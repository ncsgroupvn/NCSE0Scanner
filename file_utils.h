#pragma once

#include <tchar.h>

typedef int (*FileEnumFunc)(TCHAR* strFileName, ULONG64 nFileSize, void* ctx);

int FolderLookup(LPCTSTR strFolder, FileEnumFunc cb, void* ctx);