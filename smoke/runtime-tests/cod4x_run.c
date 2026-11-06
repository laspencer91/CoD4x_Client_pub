/*
 * Test runner: starts iw3mp.exe from a private game folder with a given CoD4x client DLL,
 * without the CoD4x launcher or loader. The DLL is loaded into the suspended process with the
 * "installed" flag set, so its DllMain routes the entry point to its own WinMain (the client's
 * legacy start path). Optionally on a separate desktop so no window appears or takes focus.
 *
 *   cod4x_run [--desktop <name>] <game folder> <cod4x_021.dll> [game arguments...]
 *
 * Prints the process id and exits; the caller waits for and stops the game by that id.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

#define INSTALLED_FLAG_ADDRESS 0x690429

static int fail(const char *what)
{
	fprintf(stderr, "cod4x_run: %s failed (error %lu)\n", what, (unsigned long)GetLastError());
	return 1;
}

int wmain(int argc, wchar_t **argv)
{
	const wchar_t *desktop = NULL;
	wchar_t exe[MAX_PATH], command[8192], dll[MAX_PATH];
	STARTUPINFOW startup = { sizeof(startup) };
	PROCESS_INFORMATION process;
	const int installed = 1;
	DWORD oldProtect, exitCode = 0;
	SIZE_T written;
	void *remote;
	HANDLE thread;
	int first = 1, i;

	if ( argc > 2 && !wcscmp(argv[1], L"--desktop") )
	{
		desktop = argv[2];
		first = 3;
	}
	if ( argc - first < 2 )
	{
		fprintf(stderr, "usage: cod4x_run [--desktop <name>] <game folder> <cod4x_021.dll> [game arguments...]\n");
		return 2;
	}
	if ( !GetFullPathNameW(argv[first + 1], MAX_PATH, dll, NULL) )
	{
		return fail("GetFullPathName");
	}
	swprintf(exe, MAX_PATH, L"%ls\\iw3mp.exe", argv[first]);
	swprintf(command, 8192, L"\"%ls\"", exe);
	for ( i = first + 2; i < argc; ++i )
	{
		wcscat(command, L" ");
		wcscat(command, argv[i]);
	}
	if ( desktop )
	{
		HDESK handle = CreateDesktopW(desktop, NULL, NULL, 0, GENERIC_ALL, NULL);
		if ( !handle )
		{
			return fail("CreateDesktop");
		}
		startup.lpDesktop = (LPWSTR)desktop; /* the handle stays open while this runner lives */
	}
	else
	{
		startup.dwFlags = STARTF_USESHOWWINDOW;
		startup.wShowWindow = SW_SHOWMINNOACTIVE;
#ifdef RML_LAB_FOREGROUND
        startup.wShowWindow = SW_SHOWNORMAL;
#endif
	}
	if ( !CreateProcessW(exe, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, argv[first], &startup, &process) )
	{
		return fail("CreateProcess");
	}
	if ( !VirtualProtectEx(process.hProcess, (void*)INSTALLED_FLAG_ADDRESS, sizeof(installed), PAGE_EXECUTE_READWRITE, &oldProtect)
		|| !WriteProcessMemory(process.hProcess, (void*)INSTALLED_FLAG_ADDRESS, &installed, sizeof(installed), &written) )
	{
		TerminateProcess(process.hProcess, 1);
		return fail("setting the installed flag");
	}
	VirtualProtectEx(process.hProcess, (void*)INSTALLED_FLAG_ADDRESS, sizeof(installed), oldProtect, &oldProtect);
	remote = VirtualAllocEx(process.hProcess, NULL, sizeof(dll), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if ( !remote || !WriteProcessMemory(process.hProcess, remote, dll, sizeof(dll), &written) )
	{
		TerminateProcess(process.hProcess, 1);
		return fail("writing the DLL path");
	}
	thread = CreateRemoteThread(process.hProcess, NULL, 0,
		(LPTHREAD_START_ROUTINE)(void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"), remote, 0, NULL);
	if ( !thread || WaitForSingleObject(thread, 60000) != WAIT_OBJECT_0 || !GetExitCodeThread(thread, &exitCode) || !exitCode )
	{
		TerminateProcess(process.hProcess, 1);
		return fail("loading the client DLL");
	}
	CloseHandle(thread);
	ResumeThread(process.hThread);
	printf("%lu\n", (unsigned long)process.dwProcessId);
	fflush(stdout);
	if ( desktop )
	{
		/* keep the desktop alive for as long as the game runs */
		WaitForSingleObject(process.hProcess, INFINITE);
	}
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return 0;
}
