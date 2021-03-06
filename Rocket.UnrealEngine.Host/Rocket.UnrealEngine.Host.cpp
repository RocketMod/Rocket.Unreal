﻿#include <iostream>
#include "../coreclr/src/pal/prebuilt/inc/mscoree.h"
#include "CrossPlatformUtils.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__)
#include <dlfcn.h>
#include <unistd.h>
#endif

using namespace std;

typedef void (FnInitialize)();

int Exit(int code);
void Unload(ICLRRuntimeHost2* runtimeHost, DWORD domainId);
HRESULT SetupAppDomain(ICLRRuntimeHost2* runtime_host, DWORD* domainId);
HRESULT InitializeCoreCLR(DWORD* domaindId);

int main()
{
	DWORD domainId;
	HRESULT hr = InitializeCoreCLR(&domainId);
	return Exit(hr == S_OK ? 0 : 1);
}

ICLRRuntimeHost2* runtimeHost;
HRESULT InitializeCoreCLR(DWORD* domainId)
{
	HRESULT hr;
	if (runtimeHost == nullptr)
	{
		wcout << L"Initalizing coreclr..." << endl;
		FnGetCLRRuntimeHost pfnGetCLRRuntimeHost = (FnGetCLRRuntimeHost)::GetCoreCLRFunction("GetCLRRuntimeHost");

		if (pfnGetCLRRuntimeHost == nullptr)
		{
			wcerr << "Failed to load coreclr" << endl;
			return E_FAIL;
		}

		hr = pfnGetCLRRuntimeHost(IID_ICLRRuntimeHost2, (IUnknown**)&runtimeHost);

		if (hr != S_OK)
		{
			wcerr << "GetCLRRuntimeHost failed." << endl;
			return hr;
		}

		//Todo: don't use single AppDomain to allow sandboxed environments
		hr = runtimeHost->SetStartupFlags(
			// These startup flags control runtime-wide behaviors.
			// A complete list of STARTUP_FLAGS can be found in mscoree.h,
			// but some of the more common ones are listed below.
			static_cast<STARTUP_FLAGS>(
				// STARTUP_FLAGS::STARTUP_SERVER_GC |								// Use server GC
				// STARTUP_FLAGS::STARTUP_LOADER_OPTIMIZATION_MULTI_DOMAIN |		// Maximize domain-neutral loading
				// STARTUP_FLAGS::STARTUP_LOADER_OPTIMIZATION_MULTI_DOMAIN_HOST |	// Domain-neutral loading for strongly-named assemblies
				STARTUP_CONCURRENT_GC |						// Use concurrent GC
				STARTUP_SINGLE_APPDOMAIN |					// All code executes in the default AppDomain 
															// (required to use the runtimeHost->ExecuteAssembly helper function)
				STARTUP_LOADER_OPTIMIZATION_SINGLE_DOMAIN	// Prevents domain-neutral loading
				)
		);

		if (hr != S_OK)
		{
			wcerr << "runtimeHost->SetStartupFlags failed." << endl;
			return hr;
		}

		hr = runtimeHost->Start();

		if (hr != S_OK)
		{
			wcerr << "runtimeHost->Start failed." << endl;
			return hr;
		}
	}

	hr = SetupAppDomain(runtimeHost, domainId);

	if (hr != S_OK)
	{
		wcerr << "SetupAppDomain failed." << endl;
		return hr;
	}

	const WCHAR* targetAssembly = L"Rocket.UnityEngine.dll";
	if (!FileExists(targetAssembly))
	{
		wcerr << "Rocket.UnityEngine.dll not found." << endl;
		return hr;
	}

	DWORD exitCode = -1;
	hr = runtimeHost->ExecuteAssembly(*domainId, targetAssembly, 0, NULL, &exitCode);
	if (hr != S_OK)
	{
		wcerr << "runtimeHost->ExecuteAssembly failed." << endl;
		return hr;
	}

	void *pfnDelegate = NULL;
	hr = runtimeHost->CreateDelegate(
		*domainId,
		L"Rocket.UnrealEngine, Version=1.0.0.0, Culture=neutral",  // Target managed assembly
		L"Rocket.UnrealEngine.Boostrap",						   // Target managed type
		L"Initialize",											   // Target entry point (static method)
		(INT_PTR*)&pfnDelegate);
	if (hr != S_OK)
	{
		wcerr << "runtimeHost->CreateDelegate failed." << endl;
		return hr;
	}

	((FnInitialize*)pfnDelegate)();

	return S_OK;
}

HRESULT SetupAppDomain(ICLRRuntimeHost2* runtimeHost, DWORD* domainId)
{
	int appDomainFlags =
		// APPDOMAIN_FORCE_TRIVIAL_WAIT_OPERATIONS |		// Do not pump messages during wait
		APPDOMAIN_SECURITY_SANDBOXED |						// Causes assemblies not from the TPA list to be loaded as partially trusted
		APPDOMAIN_ENABLE_PLATFORM_SPECIFIC_APPS |			// Enable platform-specific assemblies to run
		APPDOMAIN_ENABLE_PINVOKE_AND_CLASSIC_COMINTEROP |	// Allow PInvoking from non-TPA assemblies
		APPDOMAIN_DISABLE_TRANSPARENCY_ENFORCEMENT;			// Entirely disables transparency checks 

														 // TRUSTED_PLATFORM_ASSEMBLIES
														 // "Trusted Platform Assemblies" are prioritized by the loader and always loaded with full trust.
														 // A common pattern is to include any assemblies next to CoreCLR.dll as platform assemblies.
														 // More sophisticated hosts may also include their own Framework extensions (such as AppDomain managers)
														 // in this list.
	int tpaSize = 100 * MAX_PATH; // Starting size for our TPA (Trusted Platform Assemblies) list
	wchar_t* trustedPlatformAssemblies = new wchar_t[tpaSize];
	trustedPlatformAssemblies[0] = L'\0';

	// Extensions to probe for when finding TPA list files
	const WCHAR *tpaExtensions[] = {
		L"*.dll",
		L"*.exe",
		L"*.winmd"
	};

	WCHAR targetAppPath[MAX_PATH];
	GetCurrentDirectory_CP(targetAppPath, MAX_PATH);

	WCHAR coreRoot[MAX_PATH];
	wcscpy_s(coreRoot, targetAppPath);
	wcscat_s(coreRoot, L"/coreclr");

	// Probe next to CoreCLR.dll for any files matching the extensions from tpaExtensions and
	// add them to the TPA list. In a real host, this would likely be extracted into a separate function
	// and perhaps also run on other directories of interest.
	for (int i = 0; i < _countof(tpaExtensions); i++)
	{
		// Construct the file name search pattern
		wchar_t searchPath[MAX_PATH];
		wcscpy_s(searchPath, MAX_PATH, coreRoot);
		wcscat_s(searchPath, MAX_PATH, L"\\");
		wcscat_s(searchPath, MAX_PATH, tpaExtensions[i]);

		// Find files matching the search pattern
		WIN32_FIND_DATAW findData;
		HANDLE fileHandle = FindFirstFileW(searchPath, &findData);

		if (fileHandle != INVALID_HANDLE_VALUE)
		{
			do
			{
				// Construct the full path of the trusted assembly
				wchar_t pathToAdd[MAX_PATH];
				wcscpy_s(pathToAdd, MAX_PATH, coreRoot);
				wcscat_s(pathToAdd, MAX_PATH, L"\\");
				wcscat_s(pathToAdd, MAX_PATH, findData.cFileName);

				// Check to see if TPA list needs expanded
				if (wcslen(pathToAdd) + (3) + wcslen(trustedPlatformAssemblies) >= tpaSize)
				{
					// Expand, if needed
					tpaSize *= 2;
					wchar_t* newTPAList = new wchar_t[tpaSize];
					wcscpy_s(newTPAList, tpaSize, trustedPlatformAssemblies);
					trustedPlatformAssemblies = newTPAList;
				}

				// Add the assembly to the list and delimited with a semi-colon
				wcscat_s(trustedPlatformAssemblies, tpaSize, pathToAdd);
				wcscat_s(trustedPlatformAssemblies, tpaSize, L";");

				// Note that the CLR does not guarantee which assembly will be loaded if an assembly
				// is in the TPA list multiple times (perhaps from different paths or perhaps with different NI/NI.dll
				// extensions. Therefore, a real host should probably add items to the list in priority order and only
				// add a file if it's not already present on the list.
				//
				// For this simple sample, though, and because we're only loading TPA assemblies from a single path,
				// we can ignore that complication.
			} while (FindNextFileW(fileHandle, &findData));
			FindClose(fileHandle);
		}
	}


	// APP_PATHS
	// App paths are directories to probe in for assemblies which are not one of the well-known Framework assemblies
	// included in the TPA list.
	//
	// For this simple sample, we just include the directory the target application is in.
	// More complex hosts may want to also check the current working directory or other
	// locations known to contain application assets.
	WCHAR appPaths[MAX_PATH * 50];

	// Just use the targetApp provided by the user and remove the file name
	wcscpy_s(appPaths, targetAppPath);


	// APP_NI_PATHS
	// App (NI) paths are the paths that will be probed for native images not found on the TPA list. 
	// It will typically be similar to the app paths.
	// For this sample, we probe next to the app and in a hypothetical directory of the same name with 'NI' suffixed to the end.
	wchar_t appNiPaths[MAX_PATH * 50];
	wcscpy_s(appNiPaths, targetAppPath);
	wcscat_s(appNiPaths, MAX_PATH * 50, L";");
	wcscat_s(appNiPaths, MAX_PATH * 50, targetAppPath);
	wcscat_s(appNiPaths, MAX_PATH * 50, L"NI");


	// NATIVE_DLL_SEARCH_DIRECTORIES
	// Native dll search directories are paths that the runtime will probe for native DLLs called via PInvoke
	wchar_t nativeDllSearchDirectories[MAX_PATH * 50];
	wcscpy_s(nativeDllSearchDirectories, appPaths);
	wcscat_s(nativeDllSearchDirectories, MAX_PATH * 50, L";");
	wcscat_s(nativeDllSearchDirectories, MAX_PATH * 50, coreRoot);
	//Todo: support loading game specific dlls (e.g. Unturned4 ones)

	// PLATFORM_RESOURCE_ROOTS
	// Platform resource roots are paths to probe in for resource assemblies (in culture-specific sub-directories)
	wchar_t platformResourceRoots[MAX_PATH * 50];
	wcscpy_s(platformResourceRoots, appPaths);


	// AppDomainCompatSwitch
	// Specifies compatibility behavior for the app domain. This indicates which compatibility
	// quirks to apply if an assembly doesn't have an explicit Target Framework Moniker. If a TFM is
	// present on an assembly, the runtime will always attempt to use quirks appropriate to the version
	// of the TFM.
	// 
	// Typically the latest behavior is desired, but some hosts may want to default to older Silverlight
	// or Windows Phone behaviors for compatibility reasons.
	const wchar_t* appDomainCompatSwitch = L"UseLatestBehaviorWhenTFMNotSpecified";

	// Setup key/value pairs for AppDomain  properties
	const wchar_t* propertyKeys[] = {
		L"TRUSTED_PLATFORM_ASSEMBLIES",
		L"APP_PATHS",
		L"APP_NI_PATHS",
		L"NATIVE_DLL_SEARCH_DIRECTORIES",
		L"PLATFORM_RESOURCE_ROOTS",
		L"AppDomainCompatSwitch"
	};

	// Property values which were constructed in step 5
	const wchar_t* propertyValues[] = {
		trustedPlatformAssemblies,
		appPaths,
		appNiPaths,
		nativeDllSearchDirectories,
		platformResourceRoots,
		appDomainCompatSwitch
	};

	// Create the AppDomain
	return runtimeHost->CreateAppDomainWithManager(
		L"Rocket Host AppDomain",		// Friendly AD name
		appDomainFlags,
		NULL,							// Optional AppDomain manager assembly name
		NULL,							// Optional AppDomain manager type (including namespace)
		sizeof(propertyKeys) / sizeof(wchar_t*),
		propertyKeys,
		propertyValues,
		domainId);
}

void Unload(ICLRRuntimeHost2* runtimeHost, DWORD domainId)
{
	runtimeHost->UnloadAppDomain(domainId, true /* Wait until unload complete */);
	runtimeHost->Stop();
	runtimeHost->Release();
}

int Exit(const int code)
{
#if defined(_WIN32)
	//debugging in VS; dont exit immediately
	if (IsDebuggerPresent())
	{
		system("PAUSE");
	}
#endif

	return code;
}