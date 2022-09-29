#pragma once

#include <tchar.h>

#define LOG_APP         0
#define LOG_MATCHED     1

void LogT(int type, LPCTSTR text);
void Log(int type, const char* text);
void LogF(int type, const char* _Format, ...);