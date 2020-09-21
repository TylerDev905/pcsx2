#include "PrecompiledHeader.h"

#include "SysThreads.h"
#include <Windows.h>

#include "LXMods.h"
#include "Memory.h"
#include "R5900.h"
#include "vtlb.h"

#include "CpuUsageProvider.h"
#include "System.h"

#include "GS.h"
#include "MTVU.h"
//#include "AppCoreThread.h"

DWORD WINAPI HackThread(LPVOID voidful);

void ResetDynaRec(u32 address);

MemSetter memSetters[1024];
int numUsedMemSetters;

GameSharkCode* gameSharkCodes; // Copied from PCSX2 shared data to fix threading issue
s32 numGameSharkCodes;

u32 lxBreakpoints[12];

PCSX2SharedData lxSharedData;

void StartLXThread()
{
	CreateThread(NULL, 0, HackThread, NULL, 0, NULL);
}

void _cdecl SetPCSX2Memory(UINT32 ps2Address, void* data, u32 dataLength)
{
	if (numUsedMemSetters >= 1024)
		return; // No room for another

	while (numUsedMemSetters == 1337 || numUsedMemSetters == 1338); // Wait for patches to complete
	int actualNumUsed = numUsedMemSetters;
	numUsedMemSetters = 1338;

	// If 0, clean up all possibly-existing memsetters
	if (! actualNumUsed)
	{
		for (int i = 0; i < 1024; i ++)
		{
			free(memSetters[i].data);
			memSetters[i].data = NULL;
		}
	}

	// Look for a duplicate first
	int useId;

	for (useId = 0; useId < actualNumUsed; useId ++)
	{
		if (memSetters[useId].address == ps2Address && memSetters[useId].dataLen == dataLength)
			break; // Duplicate found
	}

	if (useId == actualNumUsed && useId < 1024)
	{
		// Add a mem setter to be used by the EE core thread
		memSetters[actualNumUsed].address = ps2Address;
		memSetters[actualNumUsed].data = malloc(dataLength);
		memSetters[actualNumUsed].dataLen = dataLength;
		memcpy(memSetters[actualNumUsed].data, data, dataLength);

		actualNumUsed ++;
	}
	else if (useId < 1024)
	{
		// Just update the current one
		memcpy(memSetters[useId].data, data, dataLength);
	}

	numUsedMemSetters = actualNumUsed;
}

void LXVsyncUpdate()
{
	// Apply memsetters
	while (numUsedMemSetters == 1338); // Another magic number

	int oldNumUsed = numUsedMemSetters; // Multi-threading hack?
	numUsedMemSetters = 1337; // Magic number

	for (int i = 0; i < oldNumUsed; i ++)
		memcpy((void*) ((u32) eeMem->Main + memSetters[i].address), memSetters[i].data, memSetters[i].dataLen);

	numUsedMemSetters = 0;

	// Apply GameShark codes
	while (numGameSharkCodes < 0);
	int actualNumCodes = numGameSharkCodes;
	numGameSharkCodes = -1;

	for (int i = 0; i < actualNumCodes; i ++)
	{
		u32 address = gameSharkCodes[i].address & 0x03FFFFFF;
		u32 value = gameSharkCodes[i].value;
		u8 type = gameSharkCodes[i].address >> 28;

		switch (type)
		{
			case 0x00:
			{
				if (address >= 0x02000000) break;
				
				*((u8*) &eeMem->Main[address]) = value & 0xFF;
				break;
			}
			case 0x01:
			{
				if (address + 2 >= 0x02000000) break;
				
				*((u16*) &eeMem->Main[address]) = value & 0xFFFF;
				break;
			}
			case 0x02:
			{
				if (address + 4 >= 0x02000000) break;
				
				*((u32*) &eeMem->Main[address]) = value;
				break;
			}
			case 0x0D:
			{
				if (address + 2 >= 0x02000000) break;

				if (i + 1 >= lxSharedData.numGameSharkCodes) break;

				if (*((u16*) &eeMem->Main[address]) != (u16) value)
					i ++;
				break;
			}
		}
	}

	numGameSharkCodes = actualNumCodes;
}

using namespace vtlb_private;
using namespace x86Emitter;

bool isPaused = false;
BreakStatus internalBreakStatus;
BreakStatus internalBreakRequest;
u32 internalBreakPc;
u32 nextBreakPc;
u32 hitDataBreakpoint;

typedef void DynGenFunc();
void __fastcall recRecompile( const u32 startpc );
void recClear(u32 addr, u32 size);
void __fastcall dyna_page_reset(u32 start,u32 sz); // <----- IMPORTANT STUFF
extern __aligned16 u8 manual_counter[Ps2MemSize::MainRam >> 12];
extern void (* ExitRecompiledCode)();
void recResetRaw();

#define GET_OPERATION(code) ((code) >> 26 ? (((code) >> 26) + 0x40) : ((code) & 0x0000003F))

void __cdecl PCBreakpoint(u32 pcAddr)
{
	__asm
	{
		push eax
		push ebx
		push ecx
		push edx
	};

	u32 code = *(u32*) PSM(pcAddr);
	u8 op = GET_OPERATION(code);

	if (op == 0x44 || op == 0x45 || op == 0x46 || op == 0x47 || op == 0x54 || op == 0x55)
		nextBreakPc = pcAddr + (signed short) (cpuRegs.code & 0xFFFF) * 4 + 4;
	else if (op == 0x42 || op == 0x43)
		nextBreakPc = ((code & 0x03FFFFFF) << 2);
	else if (op == 0x08 || op == 0x09)
		nextBreakPc = (cpuRegs.GPR.r[(code >> 21) & 0x1F].UL[0]) & 0x03FFFFFF;
	else
		nextBreakPc = pcAddr + 4;

	internalBreakStatus = BREAK_PAUSED;
	lxSharedData.breakStatus = BREAK_PAUSED;
	internalBreakPc = pcAddr;
	lxSharedData.breakPc = pcAddr;
	
	// Read registers to shared data
	for (int i = 0; i < 32; i ++)
		lxSharedData.breakRegs[i] = cpuRegs.GPR.r[i].UL[0];

	while (1)
	{
		Sleep(20);

		if (lxSharedData.breakStatus != BREAK_PAUSED)
			break;
	}
	
	// Write registers from shared data
	for (int i = 0; i < 32; i ++)
		cpuRegs.GPR.r[i].UL[0] = lxSharedData.breakRegs[i];

	LXVsyncUpdate();

	internalBreakStatus = BREAK_GRANTINGREQUEST;
	internalBreakRequest = (BreakStatus) lxSharedData.breakStatus;
	
	if (internalBreakRequest == BREAK_REQUESTCONTINUE)
	{
		recClear(pcAddr & ~0xfffUL, 0x400);
		manual_counter[(pcAddr >> 12) - 1]++;
		mmap_MarkCountedRamPage( pcAddr - 0x1000 );

		recClear(pcAddr & ~0xfffUL, 0x400);
		manual_counter[pcAddr >> 12]++;
		mmap_MarkCountedRamPage( pcAddr );
	}
	else if (internalBreakRequest == BREAK_REQUESTSTEP)
	{
		for (u32 start = 0x00000000; start < 0x02000000; start += 0x1000)
		{
			recClear(start & ~0xfffUL, 0x0400);
			manual_counter[start >> 12]++;
			mmap_MarkCountedRamPage( start );
		}
	}

	__asm
	{
		pop edx
		pop ecx
		pop ebx
		pop eax
	};

/*#ifdef _MSC_VER
	__asm leave __asm jmp [ExitRecompiledCode]
#else
	__asm__ __volatile__( "leave\n jmp *%[exitRec]\n" : : [exitRec] "m" (ExitRecompiledCode) : );
#endif*/
}

void __cdecl DataBreakpoint(u32 writeAddr, u32 pcAddr)
{
	__asm
	{
		push eax
		push ebx
		push ecx
		push edx
	};
	/*u32 value;
	__asm mov dword ptr[value],edx // Get the value being written (only for write breakpoints) */

	internalBreakStatus = BREAK_PAUSED;
	lxSharedData.breakStatus = BREAK_PAUSED;
	internalBreakPc = pcAddr;
	hitDataBreakpoint = pcAddr;
	lxSharedData.breakPc = pcAddr;

	for (int i = 0; i < 32; i ++)
		lxSharedData.breakRegs[i] = cpuRegs.GPR.r[i].UL[0];

	while (1)
	{
		Sleep(20);

		if (lxSharedData.breakStatus != BREAK_PAUSED)
			break;
	}

	LXVsyncUpdate();

	internalBreakStatus = BREAK_GRANTINGREQUEST;
	internalBreakRequest = (BreakStatus) lxSharedData.breakStatus;

	if (internalBreakRequest == BREAK_REQUESTCONTINUE)
	{
		recClear(pcAddr & ~0xfffUL, 0x400);
		manual_counter[(pcAddr >> 12) - 1]++;
		mmap_MarkCountedRamPage( pcAddr - 0x1000 );

		recClear(pcAddr & ~0xfffUL, 0x400);
		manual_counter[pcAddr >> 12]++;
		mmap_MarkCountedRamPage( pcAddr );
	}
	else if (internalBreakRequest == BREAK_REQUESTSTEP)
	{
		for (u32 start = 0x00000000; start < 0x02000000; start += 0x1000)
		{
			recClear(start & ~0xfffUL, 0x0400);
			manual_counter[start >> 12]++;
			mmap_MarkCountedRamPage( start );
		}
	}

	__asm
	{
		pop edx
		pop ecx
		pop ebx
		pop eax
	};
}

DWORD stepWaitTimer = 0;

DWORD WINAPI HackThread(LPVOID voidful)
{
	HMODULE lxDll = LoadLibrary(L".\\PCSX2dis.dll");
	void (*initFunc)() = NULL;
	void (*handleFunc)(PCSX2SharedData* sharedData) = NULL;

	initFunc = (void(*)()) GetProcAddress(lxDll, "InitDll");
	handleFunc = (void(*)(PCSX2SharedData*)) GetProcAddress(lxDll, "HandleDll");

	assert(sizeof (void*) == 4); // 32-bit only

	if (! lxDll)
	{
		Console.WriteLn(Color_StrongBlack, "ERROR: Could not load .\\PCSX2dis.dll! Did you put the DLL in the same folder as pcsx2dis.exe?");
		return 0;
	}

	if (initFunc)
		initFunc();
	else
	{
		Console.WriteLn(Color_StrongBlack, L"ERROR: initFunc not available! (PCSX2dis DLL load error)");
		return 0;
	}

	if (! handleFunc)
	{
		Console.WriteLn(Color_StrongBlack, L"ERROR: handleFunc not available! (PCSX2dis DLL load error)");
		return 0;
	}

	while (1) // Don't you love seeing this line? ;P
	{
		DWORD startTime = GetTickCount();

		// Update the shared data
		lxSharedData.internalBreakpoints = lxBreakpoints;
		lxSharedData.numInternalBreakpoints = 12;
		lxSharedData.ps2Memory = eeMem->Main;
		lxSharedData.setMemoryFunction = SetPCSX2Memory;

		if (internalBreakStatus == BREAK_RUNNING)
			lxSharedData.breakStatus = BREAK_RUNNING;
		else if (internalBreakStatus == BREAK_GRANTINGREQUEST)
		{
			lxSharedData.breakStatus = BREAK_PAUSED;

			if (! stepWaitTimer)
				stepWaitTimer = GetTickCount();
			else if (GetTickCount() - stepWaitTimer >= 1000)
			{
				internalBreakStatus = BREAK_RUNNING;
				lxSharedData.breakStatus = BREAK_RUNNING;

				Console.WriteLn(Color_Red, "PCSX2dis warning: PC breakpoint lost (sorry) =(");
				stepWaitTimer = 0;
			}
		}
		else
			stepWaitTimer = 0;
		
		lxSharedData.breakPc = internalBreakPc;

		handleFunc(&lxSharedData);
		
		// Delicately transfer codes to PCSX2
		while (numGameSharkCodes < 0); // Threading issue
		numGameSharkCodes = -2;
		while (lxSharedData.numGameSharkCodes < 0);
		int actualNumGameSharkCodes = lxSharedData.numGameSharkCodes;
		lxSharedData.numGameSharkCodes = -1;

		if (actualNumGameSharkCodes)
		{
			void* newData = VirtualAlloc((void*) NULL, actualNumGameSharkCodes * sizeof (GameSharkCode), MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
			GameSharkCode* oldData = gameSharkCodes;

			memcpy(newData, lxSharedData.gameSharkCodes, actualNumGameSharkCodes * sizeof (GameSharkCode));

			gameSharkCodes = (GameSharkCode*) newData;
			numGameSharkCodes = actualNumGameSharkCodes;
			
			if (oldData)
				VirtualFree(oldData, 0, MEM_RELEASE);
		}
		else
		{
			numGameSharkCodes = 0;
			if (gameSharkCodes)
				VirtualFree(gameSharkCodes, 0, MEM_RELEASE);
			gameSharkCodes = NULL;
		}

		lxSharedData.numGameSharkCodes = actualNumGameSharkCodes;

		// Update breakpoints (recompile)
		if (internalBreakStatus == BREAK_RUNNING && eeMem->Main && GetCoreThread().HasActiveMachine() && GetCoreThread().GetExecutionMode() == SysThreadBase::ExecMode_Opened)
		{
			for (int i = 2; i < 12; i ++)
			{
				if (! lxBreakpoints[i])
					continue;

				u32 start = lxBreakpoints[i];

				recClear(start, 0x4);
				//manual_counter[start >> 12]++;
				//mmap_MarkCountedRamPage( start );
			}
		}

		// Try to give the tool a 30FPS refresh rate
		if (GetTickCount() - startTime <= 33)
			Sleep(33 - (GetTickCount() - startTime));
	}
}
