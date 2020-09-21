#pragma once
// MemSetter: Struct shared by DLL and PCSX2 used to store memory write requests and execute them in the EE core thread (at the same time as patches)
struct MemSetter
{
	u32 address;
	void* data;
	int dataLen;
};

// Break status: represents whether broken. Shared between PCSX2 and the DLL
enum BreakStatus
{
	BREAK_RUNNING = 0,
	BREAK_PAUSED = 1,
	BREAK_REQUESTCONTINUE = 2,
	BREAK_REQUESTSTEP = 3,
	BREAK_GRANTINGREQUEST = 4
};

// PCSX2 shared data: data shared between PCSX2 and the DLL
#pragma pack(push, 1)
struct GameSharkCode
{
	u32 address;
	u32 value;
};

struct PCSX2SharedData
{
	void* ps2Memory;
	void (_cdecl *setMemoryFunction)(u32 ps2Address, void* data, u32 dataLength);

	u32* internalBreakpoints;
	u8 numInternalBreakpoints;

	s8 breakStatus;
	u32 breakRegs[32];
	u32 breakPc;

	GameSharkCode* gameSharkCodes;
	s32 numGameSharkCodes;
};
#pragma pack(pop)

extern PCSX2SharedData lxSharedData;

extern MemSetter memSetters[1024];
extern int numUsedMemSetters;

extern u32 lxBreakpoints[12];

extern GameSharkCode* gameSharkCodes; // Copied from PCSX2 shared data to fix threading issue
extern s32 numGameSharkCodes;

extern u32 internalBreakPc;
extern u32 nextBreakPc;
extern BreakStatus internalBreakStatus;
extern BreakStatus internalBreakRequest;
extern u32 hitDataBreakpoint;

void StartLXThread();

void LXVsyncUpdate();

void __cdecl PCBreakpoint(u32 pcAddr);
void __cdecl DataBreakpoint(u32 dataAddr, u32 pcAddr);