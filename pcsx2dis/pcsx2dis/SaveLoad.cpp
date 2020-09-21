#include "Main.h"
#include "MainList.h"
#include "GameShark.h"
#include "Windows.h"

#include <cstdio>
#include <cstring>

#include <Windows.h> // DeleteFile

#define WRITE(data, size) fwrite(data, size, 1, f);
#define WRITENUM(num, size) {UINT32 _tmp = (num); fwrite(&_tmp, size, 1, f);}

#define READ(data, size) fread(data, size, 1, f);
#define READNUM(var, size) {UINT32 _tmp = 0; fread(&_tmp, size, 1, f); var = _tmp;}

UINT32 hackStandaloneNumBreakpoints;
UINT32 hackStandaloneBreakpoints[12];

void SaveProject(const char* filename)
{
	char tempFilename[280];
	sprintf(tempFilename, "%s_TEMP.bin", filename);

	FILE* f = fopen(tempFilename, "wb");

	if (! f)
		return;

	if (! memlen8)
		return; // Or a crash at 'int curType = lines[0].datatype;'

	// Write version number
	WRITENUM(0x3713, 2); // Magic number for compatibility with oldest project files
	WRITENUM(0x0004, 2); // Actual version number

	// Write memory
	WRITENUM(memlen8, 4);
	WRITE(mem, memlen8);

	// Write labels
	WRITENUM(numlabels, 4);

	for (int i = 0; i < numlabels; i ++)
	{
		WRITENUM(strlen(labels[i].string), 2);
		WRITENUM(labels[i].address, 4);
		WRITENUM(labels[i].autoGenerated, 1);

		WRITE(labels[i].string, strlen(labels[i].string));
	}

	// Write comments
	int numUserComments = 0;

	for (int i = 0; i < numcomments; i ++) {if (! comments[i].autoGenerated) numUserComments ++;}

	WRITENUM(numUserComments, 4);

	for (int i = 0; i < numcomments; i ++)
	{
		if (comments[i].autoGenerated)
			continue;

		WRITENUM(strlen(comments[i].string), 2);
		WRITENUM(comments[i].address, 4);

		WRITE(comments[i].string, strlen(comments[i].string));
	}

	// Write changed data types
	int rangeStart = 0;
	int curType = lines[0].datatype;

	for (int i = 0; i < memlen32; i ++)
	{
		if (lines[i].datatype == curType)
			continue;

		// Else, datatype has changed for the first timesince our chain started at rangeStart.
		WRITENUM(i - rangeStart, 4);
		WRITENUM(curType, 1);

		// Begin the new range
		rangeStart = i;
		curType = lines[i].datatype;
	}

	WRITENUM(memlen32 - rangeStart, 4);
	WRITENUM(curType, 1);
	
	// Write GameShark code string
	int stringLength = 0;

	if (IsWindowVisible(wndGameShark.hwnd))
	{
		// If the GameShark window is open, update it now so the changes can be saved
		stringLength = SendMessage(wndGameShark.edCodes, WM_GETTEXTLENGTH, 0, 0);
				
		if (gameSharkCodeString)
			Free(gameSharkCodeString);

		gameSharkCodeString = (char*) Alloc(stringLength + 1);

		SendMessage(wndGameShark.edCodes, WM_GETTEXT, stringLength + 1, (LPARAM) gameSharkCodeString);
		GameSharkUpdateCodes();
	}
	else if (gameSharkCodeString)
		stringLength = strlen(gameSharkCodeString);

	WRITENUM(stringLength, 4);
	WRITE(gameSharkCodeString, stringLength);

	// Write current breakpoints
#ifdef _DLL
	WRITENUM(lxSharedData->numInternalBreakpoints, 4);

	for (int i = 0; i < 12; i ++)
		WRITENUM(lxSharedData->internalBreakpoints[i], 4);
#else
	WRITENUM(hackStandaloneNumBreakpoints, 4);

	for (int i = 0; i < 12; i ++)
		WRITENUM(hackStandaloneBreakpoints[i], 4);
#endif

	// Write reg overrides
	WRITENUM(numRegOverrides, 4);

	for (int i = 0; i < numRegOverrides; i ++)
	{
		INT8 numValues = regOverrides[i].numRegSetters, numNames = regOverrides[i].numRegNamers;

		WRITENUM(regOverrides[i].address, 4);
		WRITENUM(numValues, 1);
		WRITENUM(numNames, 1);

		for (int j = 0; j < numValues; j ++)
		{
			WRITENUM(regOverrides[i].regSetters[j].reg, 1);
			WRITENUM(regOverrides[i].regSetters[j].value, 4);
		}
			
		for (int j = 0; j < numNames; j ++)
		{
			INT8 strLen = strlen(regOverrides[i].regNamers[j].name);

			WRITENUM(regOverrides[i].regNamers[j].reg, 1);
			WRITENUM(strLen, 1);
			WRITE(regOverrides[i].regNamers[j].name, strLen);
		}
	}

	// Write struct defs
	WRITENUM(numStructDefs, 4);

	for (int i = 0; i < numStructDefs; i ++)
	{
		WRITENUM(strlen(structDefs[i].name), 1);
		WRITE(structDefs[i].name, strlen(structDefs[i].name));

		WRITENUM(strlen(structDefs[i].script), 4);
		WRITE(structDefs[i].script, strlen(structDefs[i].script));

		WRITENUM(structDefs[i].size, 4);
		WRITENUM(structDefs[i].numVars, 4);

		for (int j = 0; j < structDefs[i].numVars; j ++)
		{
			WRITENUM(strlen(structDefs[i].vars[j].name), 1);
			WRITE(structDefs[i].vars[j].name, strlen(structDefs[i].vars[j].name));

			WRITENUM(structDefs[i].vars[j].dataType, 1);
			WRITENUM(structDefs[i].vars[j].offset, 4);
			WRITENUM(structDefs[i].vars[j].numItems, 4);
		}
	}

	// Write struct insts
	WRITENUM(numStructInsts, 4);

	for (int i = 0; i < numStructInsts; i ++)
	{
		WRITENUM(structInsts[i].address, 4);
		WRITENUM(structInsts[i].numInsts, 4);
		WRITENUM(structInsts[i].structDefId, 4);
	}

	// Write current list location, etc
	WRITENUM(list.sel, 4);
	WRITENUM(list.address, 4);

	fclose(f);

	// Copy to actual file
	DeleteFile(filename);
	CopyFile(tempFilename, filename, false);
}

void OpenProject_Old(const char* filename);
void GameDataInit();

void OpenProject(const char* filename)
{
	FILE* f = fopen(filename, "rb");

	if (! f)
		return;

	MainCleanup();

	// Read version
	int magic, version;
	READNUM(magic, 2);
	READNUM(version, 2);

	if (magic != 0x3713)
	{
		// Try loading as an old project
		fclose(f);
		OpenProject_Old(filename);
		return;
	}

	// Read memory
	READNUM(memlen8, 4);

	if (! mem)
	{
		memorySize = memlen8;
		GameDataInit();
	}

	READ(mem, memlen8);

	// Read labels
	READNUM(numlabels, 4);
	labels = (Label*) Alloc(((numlabels + 256) / 256 * 256) * sizeof (Label));

	for (int i = 0; i < numlabels; i ++)
	{
		int strLen;

		READNUM(strLen, 2);
		READNUM(labels[i].address, 4);
		READNUM(labels[i].autoGenerated, 1);

		READ(labels[i].string, strLen);
		labels[i].string[strLen] = '\0';
	}

	// Read comments
	READNUM(numcomments, 4);
	comments = (Comment*) Alloc(numcomments * sizeof (Comment));

	for (int i = 0; i < numcomments; i ++)
	{
		int strLen;

		READNUM(strLen, 2);
		READNUM(comments[i].address, 4);

		READ(comments[i].string, strLen);
		comments[i].string[strLen] = '\0';

		comments[i].autoGenerated = 0;
	}
	
	// Read changed data types
	int rangeStart = 0;

	while (1)
	{
		int rangeLen;
		int type;

		READNUM(rangeLen, 4);
		READNUM(type, 1);

		for (int i = rangeStart; i < rangeStart + rangeLen; i ++)
			lines[i].datatype = type;

		rangeStart += rangeLen;

		if (rangeStart >= memlen32)
			break;
	}
	
	// Read GameShark code string
	int stringLength;

	READNUM(stringLength, 4);

	if (stringLength)
	{
		gameSharkCodeString = (char*) Alloc(stringLength + 1);

		READ(gameSharkCodeString, stringLength);
		gameSharkCodeString[stringLength] = '\0';
	}

	// Read current breakpoints
#ifdef _DLL
	READNUM(lxSharedData->numInternalBreakpoints, 4);

	for (int i = 0; i < 12; i ++)
		READNUM(lxSharedData->internalBreakpoints[i], 4);

#ifdef NEW_BREAKPOINTS
	for (int i = 0; i < 10; i ++)
	{
		if (lxSharedData->internalBreakpoints[i + BREAKPOINT_PC0])
		{
			breakpoints[i].enabled = true;
			breakpoints[i].address = lxSharedData->internalBreakpoints[i + BREAKPOINT_PC0];
		}
	}
#endif
#else
	READNUM(hackStandaloneNumBreakpoints, 4);

	for (int i = 0; i < 12; i ++)
		READNUM(hackStandaloneBreakpoints[i], 4);
#endif
	
	if (version >= 2)
	{
		// Read reg overrides
		READNUM(numRegOverrides, 4);

		regOverrides = (RegOverride*) Alloc(numRegOverrides * sizeof (RegOverride));
		memset(regOverrides, 0, numRegOverrides * sizeof (RegOverride));

		for (int i = 0; i < numRegOverrides; i ++)
		{
			INT8 numValues, numNames;
			READNUM(regOverrides[i].address, 4);
			READNUM(numValues, 1);
			READNUM(numNames, 1);

			for (int j = 0; j < numValues; j ++)
			{
				INT8 regId;
				UINT32 value;

				READNUM(regId, 1);
				READNUM(value, 4);

				AddRegSetter(&regOverrides[i], regId, value);
			}
			
			for (int j = 0; j < numValues; j ++)
			{
				INT8 regId, strLen;
				char name[17];

				READNUM(regId, 1);
				READNUM(strLen, 1);
				READ(name, strLen);
				name[strLen] = '\0';

				AddRegNamer(&regOverrides[i], regId, name);
			}
		}
	}

	if (version >= 4)
	{
		// Read struct defs
		READNUM(numStructDefs, 4);

		structDefs = (StructDef*) Alloc(numStructDefs * sizeof (StructDef));
		memset(structDefs, 0, numStructDefs * sizeof (StructDef));

		for (int i = 0; i < numStructDefs; i ++)
		{
			char nameLen;
			INT32 scriptLen;

			READNUM(nameLen, 1);
			READ(structDefs[i].name, nameLen);
			structDefs[i].name[nameLen] = '\0';

			READNUM(scriptLen, 4);
			structDefs[i].script = (char*) Alloc(scriptLen + 1);
			READ(structDefs[i].script, scriptLen);
			structDefs[i].script[scriptLen] = '\0';

			READNUM(structDefs[i].size, 4);
			READNUM(structDefs[i].numVars, 4);

			structDefs[i].vars = (StructVar*) Alloc(structDefs[i].numVars * sizeof (StructVar));
			for (int j = 0; j < structDefs[i].numVars; j ++)
			{
				READNUM(nameLen, 1);
				READ(structDefs[i].vars[j].name, nameLen);
				structDefs[i].vars[j].name[nameLen] = '\0';

				READNUM(structDefs[i].vars[j].dataType, 1);
				READNUM(structDefs[i].vars[j].offset, 4);
				READNUM(structDefs[i].vars[j].numItems, 4);
			}
		}

		// Read struct insts
		READNUM(numStructInsts, 4);

		structInsts = (StructInst*) Alloc(numStructInsts * sizeof (StructInst));

		for (int i = 0; i < numStructInsts; i ++)
		{
			READNUM(structInsts[i].address, 4);
			READNUM(structInsts[i].numInsts, 4);
			READNUM(structInsts[i].structDefId, 4);
		}
	}

	// Read current list location, etc
	READNUM(list.sel, 4);
	READNUM(list.address, 4);

	UpdateList();
	GameSharkUpdateCodes();
	UpdateStructManagerWindow();

	fclose(f);
}

void OpenProject_Old(const char* filename)
{
	FILE* f = fopen(filename, "rb");

	if (! f)
		return;

	MainCleanup();

	// Read memory
	READNUM(memlen8, 4);
	READ(mem, memlen8);

	// Read labels
	READNUM(numlabels, 4);
	labels = (Label*) Alloc(((numlabels + 256) / 256 * 256) * sizeof (Label));

	for (int i = 0; i < numlabels; i ++)
	{
		int strLen;

		READNUM(strLen, 2);
		READNUM(labels[i].address, 4);
		READNUM(labels[i].autoGenerated, 1);

		READ(labels[i].string, strLen);
		labels[i].string[strLen] = '\0';
	}

	// Read comments
	READNUM(numcomments, 4);
	comments = (Comment*) Alloc(numcomments * sizeof (Comment));

	for (int i = 0; i < numcomments; i ++)
	{
		int strLen;

		READNUM(strLen, 2);
		READNUM(comments[i].address, 4);

		READ(comments[i].string, strLen);
		comments[i].string[strLen] = '\0';

		comments[i].autoGenerated = 0;
	}

	// Read changed data types
	int rangeStart = 0;

	while (1)
	{
		int rangeLen;
		int type;

		READNUM(rangeLen, 4);
		READNUM(type, 1);

		for (int i = rangeStart; i < rangeStart + rangeLen; i ++)
			lines[i].datatype = type;

		rangeStart += rangeLen;

		if (rangeStart >= memlen32)
			break;
	}

	fclose(f);
}