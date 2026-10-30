/*
 * Minimal crash recorder for test runs: attaches to a 32-bit process as a debugger, lets every
 * first-chance exception pass, and on a second-chance (unhandled) exception prints the faulting
 * address, registers, the module list and the raw stack. No symbols needed.
 *
 *   crashcatch <pid>
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>

static void modules(DWORD pid)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	MODULEENTRY32 entry = { sizeof(entry) };
	if ( snap == INVALID_HANDLE_VALUE )
	{
		return;
	}
	for ( BOOL more = Module32First(snap, &entry); more; more = Module32Next(snap, &entry) )
	{
		printf("module %08lx-%08lx %s\n", (unsigned long)(DWORD_PTR)entry.modBaseAddr,
			(unsigned long)((DWORD_PTR)entry.modBaseAddr + entry.modBaseSize), entry.szModule);
	}
	CloseHandle(snap);
}

int main(int argc, char **argv)
{
	DEBUG_EVENT event;
	DWORD pid;
	HANDLE process = NULL;

	if ( argc != 2 )
	{
		return 2;
	}
	pid = (DWORD)strtoul(argv[1], NULL, 10);
	if ( !DebugActiveProcess(pid) )
	{
		printf("attach failed %lu\n", (unsigned long)GetLastError());
		return 1;
	}
	DebugSetProcessKillOnExit(FALSE);
	printf("attached %lu\n", (unsigned long)pid);
	fflush(stdout);
	for ( ;; )
	{
		DWORD status = DBG_CONTINUE;
		if ( !WaitForDebugEvent(&event, INFINITE) )
		{
			break;
		}
		if ( event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT )
		{
			process = event.u.CreateProcessInfo.hProcess;
			if ( event.u.CreateProcessInfo.hFile ) CloseHandle(event.u.CreateProcessInfo.hFile);
		}
		else if ( event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile )
		{
			CloseHandle(event.u.LoadDll.hFile);
		}
		else if ( event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT )
		{
			printf("process exited code %lu\n", (unsigned long)event.u.ExitProcess.dwExitCode);
			break;
		}
		else if ( event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT )
		{
			const EXCEPTION_RECORD *record = &event.u.Exception.ExceptionRecord;
			status = DBG_EXCEPTION_NOT_HANDLED;
			if ( record->ExceptionCode == EXCEPTION_BREAKPOINT && event.u.Exception.dwFirstChance )
			{
				status = DBG_CONTINUE; /* the attach breakpoint */
			}
			else if ( !event.u.Exception.dwFirstChance )
			{
				HANDLE thread = OpenThread(THREAD_ALL_ACCESS, FALSE, event.dwThreadId);
				CONTEXT context = { 0 };
				DWORD stack[256];
				SIZE_T got = 0;
				context.ContextFlags = CONTEXT_FULL;
				GetThreadContext(thread, &context);
				printf("second-chance exception %08lx at %08lx thread %lu", (unsigned long)record->ExceptionCode,
					(unsigned long)(DWORD_PTR)record->ExceptionAddress, (unsigned long)event.dwThreadId);
				if ( record->NumberParameters >= 2 )
				{
					printf(" (%s %08lx)", record->ExceptionInformation[0] ? "write" : "read", (unsigned long)record->ExceptionInformation[1]);
				}
				printf("\neax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx\nesp=%08lx ebp=%08lx eip=%08lx\n",
					context.Eax, context.Ebx, context.Ecx, context.Edx, context.Esi, context.Edi, context.Esp, context.Ebp, context.Eip);
				ReadProcessMemory(process, (void*)(DWORD_PTR)context.Esp, stack, sizeof(stack), &got);
				for ( SIZE_T i = 0; i < got / 4; ++i )
				{
					printf("%s%08lx", i % 8 ? " " : "\nstack: ", stack[i]);
				}
				printf("\n");
				modules(event.dwProcessId);
				fflush(stdout);
				CloseHandle(thread);
			}
		}
		ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
	}
	fflush(stdout);
	return 0;
}
