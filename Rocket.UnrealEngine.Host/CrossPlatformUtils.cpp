#include "CrossPlatformUtils.h"
#include <ostream>
#include <iostream>
#include <fstream>     
#include <libloaderapi.h>
#include "../coreclr/src/pal/prebuilt/inc/mscoree.h"

using namespace std;

void* GetCoreCLRFunction(const char* functionName)
{
#if defined(_WIN32)
	HMODULE coreCLRModule = LoadLibraryExW(L"coreclr/CoreCLR.dll", NULL, 0);
	if (coreCLRModule != nullptr)
		return GetProcAddress(coreCLRModule, functionName);
	else
		wcout << L"coreCLRModule == null" << endl;
#elif defined(__unix__)
	void *coreCLRModule = dlopen(L"coreclr/libcoreclr.dylib", RTLD_NOW | RTLD_LOCAL);
	if (coreCLRModule != nullptr)
		return dlsym(coreCLRModule, functionName);
	else
		wcout << L"coreCLRModule == null" << endl;
#else
#error Platform Not Supported
#endif
	return nullptr;
}

void GetCurrentDirectory_CP(WCHAR* buffer, size_t nSize)
{
#if defined(_WIN32)
	GetCurrentDirectoryW(nSize, buffer);
#elif defined(__unix__)
	char *answer = getcwd(buffer, nSize);
	string s_cwd;
	if (answer)
	{
		s_cwd = answer;
	}
#else
#error Platform Not Supported
#endif
}

bool FileExists(const WCHAR* path)
{
	ifstream Infield(path);
	return Infield.good();
}