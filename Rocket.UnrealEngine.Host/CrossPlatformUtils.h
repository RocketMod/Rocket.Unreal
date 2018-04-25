#pragma once
#include <windows.h>

#if defined(__unix__)
#define WCHAR char
#define MAX_PATH SIZE
#endif

void* GetCoreCLRFunction(const char* functionName); 
void GetCurrentDirectory_CP(WCHAR* buffer, size_t nSize);
bool FileExists(const WCHAR* path);