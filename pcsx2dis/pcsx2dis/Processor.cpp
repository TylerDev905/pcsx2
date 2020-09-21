#include "Processor.h"

#include <cstring>
#include <cstdio>

#define NOTES_RANGE(a, b) for (int i = a; i <= b; i ++) notes[i]

#define READS_RS  0x0001
#define READS_RT  0x0002
#define READS_RD  0x0004
#define READS_FS  0x0008
#define READS_FT  0x0010
#define READS_FD  0x0020
#define WRITES_RS 0x0040
#define WRITES_RT 0x0080
#define WRITES_RD 0x0100
#define WRITES_FS 0x0200
#define WRITES_FT 0x0400
#define WRITES_FD 0x0800
#define REPLACES  0x1000 // CAN_LINK: Determines whether the value would be copied from something else even if it reads from only itself.
						 // A bit hacky as it really falls under the "Analysation" category.

char* operations    [0xFF];
char* sc_operations [0x7F]; // Shortcut operations of type 1. (Maybe there'll be more?)
char* sc2_operations[0x7F]; // Shortcut operations of type 2.

unsigned short notes[0x7F];

char* asmdef[0xFF];
unsigned int asmMask[0xFF];
unsigned int asmCode[0xFF];

const char* registers[32] = 
{
	"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", 
	"t0",   "t1", "t2", "t3", "t4", "t5", "t6", "t7", 
	"s0",   "s1", "s2", "s3", "s4", "s5", "s6", "s7", 
	"t8",   "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

const char f_registers[32][5] = 
{
	"$f0",  "$f1",  "$f2",  "$f3",  "$f4",  "$f5",  "$f6",  "$f7", 
	"$f8",  "$f9",  "$f10", "$f11", "$f12", "$f13", "$f14", "$f15", 
	"$f16", "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23", 
	"$f24", "$f25", "$f26", "$f27", "$f28", "$f29", "$f30", "$f31"
};

bool ASMToCode(unsigned int address, unsigned int* code, unsigned int* mask, const char* str)
{
	int id;
	const char* firstspace = strstr(str, " ");
	unsigned int result = 0;
	unsigned int tmask = 0;

	// Find instruction name
	if (! firstspace)
		firstspace = &str[strlen(str)];

	for (id = 0; id < 0xB0; id ++)
	{
		if (! asmdef[id]) continue;

		if (! strncmp(asmdef[id], str, firstspace - str + 1))
			break;
	}

	if (id >= 0xB0)
	{
		if (! strncmp(str, "nop", 3)) // nop check
		{
			if (code) *code = 0x00000000;
			if (mask) *mask = 0x001FFFC0;

			return 1;
		}

		return 0; // Couldn't find a match for the instruction name
	}

	size_t dur = firstspace - str;
	const char* curasm = firstspace + 1;
	const char* curdef = strstr(asmdef[id], " ") + 1;
	int defLen = strlen(asmdef[id]), strLen = strlen(str);

	while (curasm[0] == ' ') curasm ++; // Skip spaces for the user-input string

	while (curdef < &asmdef[id][defLen] && curasm < &str[strLen])
	{
		// Find the value&value type in the input string
		int strReg = -1, strFReg = -1;
		bool gotimmediate, gotoffset = 0;
		int gotRegDef = -1;
		unsigned int immediate = 0xFFFFFFFF;
		
		gotimmediate = sscanf(curasm, "$%X", &immediate);

		if (! gotimmediate) gotimmediate = sscanf(curasm, "%i", &immediate);

		// Ensure compatibility with ASM def and convert if possible (otherwise, cease! It's invalid assembly code!)
		if (! strncmp(curdef, "rd", 2))
			gotRegDef = 0;
		else if (! strncmp(curdef, "rt", 2))
			gotRegDef = 1;
		else if (! strncmp(curdef, "rs", 2))
			gotRegDef = 2;
		else if (! strncmp(curdef, "fd", 2))
			gotRegDef = 3;
		else if (! strncmp(curdef, "ft", 2))
			gotRegDef = 4;
		else if (! strncmp(curdef, "fs", 2))
			gotRegDef = 5;

		if (gotRegDef != -1)
		{
			for (int i = 0; i < 32; i ++)
			{
				if (! strncmp(curasm, registers[i], strlen(registers[i])))
					strReg = i;
				if (! strncmp(curasm, f_registers[i], strlen(f_registers[i])))
					strReg = i;
			}

			if (strReg == -1)
			{
				// Might be a wildcard
				if (! strncmp(curasm, "*", 1))
					gotRegDef = -1;
				else
					return false; // Otherwise it's invalid
			}

			switch (gotRegDef)
			{
				case 0:
					SET_RD(result, strReg);
					tmask |= 0x0000F800;
					break;
				case 1:
					SET_RT(result, strReg);
					tmask |= 0x001F0000;
					break;
				case 2:
					SET_RS(result, strReg);
					tmask |= 0x03E00000;
					break;
				case 3:
					SET_FD(result, strReg);
					tmask |= 0x000007C0;
					break;
				case 4:
					SET_FT(result, strReg);
					tmask |= 0x001F0000;
					break;
				case 5:
					SET_FS(result, strReg);
					tmask |= 0x0000F800;
					break;
			}
		}
		else if (! strncmp(curdef, "i", 1) && gotimmediate)
		{
			SET_IMMEDIATE(result, (immediate & 0xFFFF));
			tmask |= 0x0000FFFF;
		}
		else if (! strncmp(curdef, "of", 2) && gotimmediate)
		{
			SET_IMMEDIATE(result, ((((int) immediate - (int) address - 1) >> 2) & 0xFFFF));
			tmask |= 0x0000FFFF;
		}
		else if (! strncmp(curdef, "ad", 2) && gotimmediate)
		{
			SET_ADDRESS(result, immediate);
			tmask |= 0x03FFFFFF;
		}
		else if (! strcmp(curdef, "sh") && gotimmediate)
		{
			SET_SHIFT(result, immediate);
			tmask |= 0x07C0;
		}
		else if (! strncmp(curasm, "*", 1)) // Wildcard in the assembly code
			tmask |= 0x00000000;
		else
			return 0; // Failed to find anything matching

		// Advance curasm if possible
		while (curasm[0] != ',' && curasm[0] != '\0' && curasm[0] != '(') curasm ++;
		curasm ++;
		while (curasm[0] == ' ') curasm ++; // Be friendly and skip spaces

		// Advance curdef
		while (curdef[0] != ',' && curdef[0] != '\0' && curdef[0] != '(') curdef ++;
		curdef ++;
	}
	
	// HACK: Jalr special case
	if (id == 0x09)
		SET_RD(result, 31);

	tmask |= (id < 0x40 ? 0x0000003F : 0xFC000000);
	if (id >= 0x80) tmask |= asmMask[id];

	SET_OPERATION(result, id); // Instruction ID
	*code = result;
	if (mask) *mask = tmask;

	return 1;
}

bool CodeToASM(char* str, unsigned int address, const unsigned int code)
{
	int id = GET_OPERATION(code);

	if (! asmdef[id])
	{
		strcpy(str, "unk");
		return 0;
	}
	
	if (code == 0x00000000) // nop check
	{
		strcpy(str, "nop");
		return 1;
	}

	const char* def = asmdef[id];
	const char* curdef = strstr(def, " ") + 1;
	bool gotbracket = 0;

	strncpy(str, def, curdef - def);
	strncpy(&str[curdef - def], "         ", 9 - (curdef - def));
	str[9] = '\0';

	while (curdef < &def[strlen(def)])
	{
		int i = 0;
		char operand[10];
		char extension[5];
		int extend = -1;

		for (i = 0; i < 15; i ++)
		{			
			if (curdef[i] == '+') {operand[i] = '\0'; extend = i;}

			if (curdef[i] == '\0' || curdef[i] == ',' || curdef[i] == '(')
			{
				if (extend != -1)
					extension[i - extend] = '\0';
				else
					operand[i] = '\0';
				if (curdef[i] == '(')
					gotbracket = 1;
				break;
			}

			if (extend == -1)
				operand[i] = curdef[i];
			else
				extension[i - extend] = curdef[i];
		}

		char append[16] = "";

		if (gotbracket)
		{
			if (! strcmp(operand, "rd"))
				sprintf(append, "(%s), ", registers[GET_RD(code)]);
			else if (! strcmp(operand, "rs"))
				sprintf(append, "(%s), ", registers[GET_RS(code)]);
			else if (! strcmp(operand, "rt"))
				sprintf(append, "(%s), ", registers[GET_RT(code)]);
			else if (! strcmp(operand, "fd"))
				sprintf(append, "(%s), ", f_registers[GET_FD(code)]);
			else if (! strcmp(operand, "fs"))
				sprintf(append, "(%s), ", f_registers[GET_FS(code)]);
			else if (! strcmp(operand, "ft"))
				sprintf(append, "(%s), ", f_registers[GET_FT(code)]);
			else goto DontContinue1;

			strcat(str, append);
			curdef += i + 1;
			continue;
			DontContinue1:;
		}
		else
		{
			if (! strcmp(operand, "rd"))
				sprintf(append, "%s, ", registers[GET_RD(code)]);
			else if (! strcmp(operand, "rs"))
				sprintf(append, "%s, ", registers[GET_RS(code)]);
			else if (! strcmp(operand, "rt"))
				sprintf(append, "%s, ", registers[GET_RT(code)]);
			else if (! strcmp(operand, "fd"))
				sprintf(append, "%s, ", f_registers[GET_FD(code)]);
			else if (! strcmp(operand, "fs"))
				sprintf(append, "%s, ", f_registers[GET_FS(code)]);
			else if (! strcmp(operand, "ft"))
				sprintf(append, "%s, ", f_registers[GET_FT(code)]);
			else goto DontContinue2;

			strcat(str, append);
			curdef += i + 1;
			continue;
			DontContinue2:;
		}

		if (! strcmp(operand, "i"))
		{
			if (! gotbracket)
				sprintf(append, "$%04X, ", GET_IMMEDIATE(code));
			else
				sprintf(append, "$%04X", GET_IMMEDIATE(code));
		}
		else if (! strcmp(operand, "of"))
			sprintf(append, "$%08X, ", address + ((signed short) GET_IMMEDIATE(code) + 1) * 4);
		else if (! strcmp(operand, "ad"))
			sprintf(append, "$%08X, ", GET_ADDRESS(code));
		else if (! strcmp(operand, "sh"))
			sprintf(append, "%i, ", GET_SHIFT(code));

		strcat(str, append);
		curdef += i + 1;
	}

	str[strlen(str) - 2] = '\0'; // Cut off the last ','
	return 1;
}

void Converter_Setup()
{
	// Additional codes for each parameter(?):
	// r: reads register
	// w: writes register
	asmdef[0x00] = (char*)"sll rd+w,rt+r,sh";
	asmdef[0x02] = (char*)"srl rd+w,rt+r,sh";
	asmdef[0x03] = (char*)"sra rd+w,rt+r,sh";
	asmdef[0x04] = (char*)"sllv rd+w,rt+r,rs+r";
	asmdef[0x06] = (char*)"srlv rd+w,rt+r,rs+r";
	asmdef[0x07] = (char*)"srav rd+w,rt+r,rs+r";
	asmdef[0x08] = (char*)"jr rs+r";
	asmdef[0x09] = (char*)"jalr rs+r";
	asmdef[0x0A] = (char*)"movz rd+w,rs+r,rt+r";
	asmdef[0x0B] = (char*)"movn rd+w,rs+r,rt+r";
	//asmdef[0x0C] (char*)= "SYSCALL";
	asmdef[0x0D] = (char*)"BREAK ";
	asmdef[0x0F] = (char*)"SYNC ";

	asmdef[0x10] = (char*)"mthi rd+r";
	asmdef[0x11] = (char*)"mfhi rs+r";
	asmdef[0x12] = (char*)"mtlo rd+r";
	asmdef[0x13] = (char*)"mflo rs+r";
	asmdef[0x14] = (char*)"dsllv rd+w,rt+r,rs+r";
	asmdef[0x16] = (char*)"dsrlv rd+w,rt+r,rs+r";
	asmdef[0x17] = (char*)"dsrav rd+w,rt+r,rs+r";
	asmdef[0x18] = (char*)"mult rs+r,rt+r";
	asmdef[0x19] = (char*)"multu rs+r,rt+r";
	asmdef[0x1A] = (char*)"div rs+r,rt+r";
	asmdef[0x1B] = (char*)"divu rs+r,rt+r";
	asmdef[0x1C] = (char*)"dmult rs+r,rt+r";
	asmdef[0x1D] = (char*)"dmultu rs+r,rt+r";
	asmdef[0x1E] = (char*)"ddiv rs+r,rt+r";
	asmdef[0x1F] = (char*)"ddivu rs+r,rt+r";

	asmdef[0x20] = (char*)"add rd+w,rs+r,rt+r";
	asmdef[0x21] = (char*)"addu rd+w,rs+r,rt+r";
	asmdef[0x22] = (char*)"sub rd+w,rs+r,rt+r";
	asmdef[0x23] = (char*)"subu rd+w,rs+r,rt+r";
	asmdef[0x24] = (char*)"and rd+w,rs+r,rt+r";
	asmdef[0x25] = (char*)"or rd+w,rs+r,rt+r";
	asmdef[0x26] = (char*)"xor rd+w,rs+r,rt+r";
	asmdef[0x27] = (char*)"nor rd+w,rs+r,rt+r";
	asmdef[0x28] = (char*)"MFSA ";
	asmdef[0x29] = (char*)"MTSA ";
	asmdef[0x2A] = (char*)"slt rd+w,rs+r,rt+r";
	asmdef[0x2B] = (char*)"sltu rd+w,rs+r,rt+r";
	asmdef[0x2C] = (char*)"dadd rd+w,rs+r,rt+r";
	asmdef[0x2D] = (char*)"daddu rd+w,rs+r,rt+r";
	asmdef[0x2E] = (char*)"dsub rd+w,rs+r,rt+r";
	asmdef[0x2F] = (char*)"dsubu rd+w,rs+r,rt+r";

	asmdef[0x38] = (char*)"dsll rd+w,rt+r,sh";
	asmdef[0x3A] = (char*)"dsrl rd+w,rt+r,sh";
	asmdef[0x3B] = (char*)"dsra rd+w,rt+r,sh";
	asmdef[0x3C] = (char*)"dsll32 rd+w,rt+r,sh";
	asmdef[0x3E] = (char*)"dsrl32 rd+w,rt+r,sh";
	asmdef[0x3F] = (char*)"dsra32 rd+w,rt+r,sh";
	
	//asmdef[0x41] = "bltz rs+r,rt+r,of";
	asmdef[0x42] = (char*)"j ad";
	asmdef[0x43] = (char*)"jal ad";
	asmdef[0x44] = (char*)"beq rs+r,rt+r,of";
	asmdef[0x45] = (char*)"bne rs+r,rt+r,of";
	asmdef[0x46] = (char*)"blez rs+r,rt+r,of";
	asmdef[0x47] = (char*)"bgtz rs+r,rt+r,of";
	asmdef[0x48] = (char*)"addi rt+w,rs+r,i";
	asmdef[0x49] = (char*)"addiu rt+w,rs+r,i";
	asmdef[0x4A] = (char*)"slti rt+w,rs+r,i";
	asmdef[0x4B] = (char*)"sltiu rt+w,rs+r,i";
	asmdef[0x4C] = (char*)"andi rt+w,rs+r,i";;
	asmdef[0x4D] = (char*)"ori rt+w,rs+r,i";
	asmdef[0x4E] = (char*)"xori rt+w,rs+r,i";
	asmdef[0x4F] = (char*)"lui rt+w,i";

	asmdef[0x54] = (char*)"beql rs+r,rt+r,of";
	asmdef[0x55] = (char*)"bnel rs+r,rt+r,of";
	asmdef[0x56] = (char*)"blezl rs+r,of";
	asmdef[0x57] = (char*)"bgtzl rs+r,of";
	asmdef[0x5E] = (char*)"lq rt+w,i(rs+r)";
	asmdef[0x5F] = (char*)"sq rt+w,i(rs+r)";

	asmdef[0x60] = (char*)"lb rt+w,i(rs+r)";
	asmdef[0x61] = (char*)"lh rt+w,i(rs+r)";
	asmdef[0x63] = (char*)"lw rt+w,i(rs+r)";
	asmdef[0x64] = (char*)"lbu rt+w,i(rs+r)";
	asmdef[0x65] = (char*)"lhu rt+w,i(rs+r)";
	asmdef[0x67] = (char*)"lwu rt+w,i(rs+r)";
	asmdef[0x68] = (char*)"sb rt+r,i(rs+r)";
	asmdef[0x69] = (char*)"sh rt+r,i(rs+r)";
	asmdef[0x6B] = (char*)"sw rt+r,i(rs+r)";

	asmdef[0x71] = (char*)"lwc1 ft+w,i(rs+r)";
	asmdef[0x77] = (char*)"ld rt+w,i(rs+r)";
	asmdef[0x79] = (char*)"swc1 ft+r,i(rs+r)";
	asmdef[0x7F] = (char*)"sd rt+r,i(rs+r)";

#define MAKEDEF(num, code, mask, string) {asmdef[num] = string; asmMask[num] = mask; asmCode[num] = code;}
	// COP0
	MAKEDEF(0x80, 0x46000030, 0xFFE007FF, (char*)"c.f.s fs+r,ft+r");
	MAKEDEF(0x81, 0x46000032, 0xFFE007FF, (char*)"c.eq.s fs+r,ft+r");
	MAKEDEF(0x82, 0x46000034, 0xFFE007FF, (char*)"c.lt.s fs+r,ft+r");
	MAKEDEF(0x83, 0x46000036, 0xFFE007FF, (char*)"c.le.s fs+r,ft+r");

	MAKEDEF(0x84, 0x46800020, 0xFFFF003F, (char*)"cvt.s.w fd+w,fs+r");
	MAKEDEF(0x85, 0x46000024, 0xFFFF003F, (char*)"cvt.w.s fd+w,fs+r");
	MAKEDEF(0x86, 0x46000005, 0xFFFF003F, (char*)"abs.s fd+w,fs+r");
										  
	MAKEDEF(0x87, 0x46000000, 0xFFE0003F, (char*)"add.s fd+w,fs+r,ft+r");
	MAKEDEF(0x88, 0x46000001, 0xFFE0003F, (char*)"sub.s fd+w,fs+r,ft+r");
	MAKEDEF(0x89, 0x46000003, 0xFFE0003F, (char*)"div.s fd+w,fs+r,ft+r");
	MAKEDEF(0x8A, 0x46000002, 0xFFE0003F, (char*)"mul.s fd+w,fs+r,ft+r");

	MAKEDEF(0x8B, 0x46000030, 0xFFE0003F, (char*)"min.s fd+w,fs+r,ft+r");
	MAKEDEF(0x8C, 0x46000028, 0xFFE0003F, (char*)"max.s fd+w,fs+r,ft+r");

	MAKEDEF(0x8D, 0x46000004, 0xFFE0F83F, (char*)"sqrt.s fd+w,fs+r,ft+r");

	MAKEDEF(0x8E, 0x46000006, 0xFFFF003F, (char*)"mov.s fd+w,fs+r,ft+r");
	
	MAKEDEF(0x8F, 0x44400000, 0xFFE007FF, (char*)"cfc1 rt+w,fs+r");
	MAKEDEF(0x90, 0x44C00000, 0xFFE007FF, (char*)"ctc1 rt+r,fs+w");
		
	MAKEDEF(0x91, 0x44800000, 0xFFE007FF, (char*)"mtc1 rt+r,fs+w");
	MAKEDEF(0x92, 0x44000000, 0xFFE007FF, (char*)"mfc1 rt+w,fs+r");

	// REGIMM (originally 0x41)
	MAKEDEF(0xA0, 0x04000000, 0xFC1F0000, (char*)"bltz rs+r,of");
	MAKEDEF(0xA1, 0x04010000, 0xFC1F0000, (char*)"bgez rs+r,of");
	MAKEDEF(0xA2, 0x04020000, 0xFC1F0000, (char*)"bltzl rs+r,of");
	MAKEDEF(0xA3, 0x04030000, 0xFC1F0000, (char*)"bgezl rs+r,of");
	MAKEDEF(0xA4, 0x04100000, 0xFC1F0000, (char*)"bltzal rs+r,of");
	MAKEDEF(0xA5, 0x04110000, 0xFC1F0000, (char*)"bgezal rs+r,of");
	MAKEDEF(0xA6, 0x04120000, 0xFC1F0000, (char*)"bltzall rs+r,of");
	MAKEDEF(0xA7, 0x04130000, 0xFC1F0000, (char*)"bgezall rs+r,of")
	MAKEDEF(0xA8, 0x1C000000, 0xFC1F0000, (char*)"bgtz rs+r,of");
	
	operations[0x00] = (char*)"rd = rt << _h";     // sll
	sc_operations[0x00] = (char*)"rd <<= _h";
	operations[0x02] = (char*)"rd = rt >> _h";     // srl
	sc_operations[0x02] = (char*)"rd >>= _h";
	operations[0x03] = (char*)"rd = rt >> _h";     // sra
	sc_operations[0x03] = (char*)"rd >>= _h";
	operations[0x04] = (char*)"rd = rt << rs";     // sllv
	sc_operations[0x04] = (char*)"rd <<= rs";
	operations[0x06] = (char*)"rd = rt >> rs";     // srlv
	sc_operations[0x06] = (char*)"rd >>= rs";
	operations[0x07] = (char*)"rd = rt >> rs";     // srav
	sc_operations[0x07] = (char*)"rd >>= rs";
	operations[0x08] = (char*)"goto rs";           // jr
	operations[0x09] = (char*)"rs()";              // jalr
	operations[0x0A] = (char*)"if (! rt) rd = rs"; // movz
	operations[0x0B] = (char*)"if (rt) rd = rs";   // movn
	//operations[0x0C] (char*)= "SYSCALL";           // syscall
	operations[0x0D] = (char*)"BREAK";             // break (needs work)
	operations[0x0F] = (char*)"SYNC";              // sync

	operations[0x10] = (char*)"rd = HI";                   // mthi
	operations[0x11] = (char*)"HI = rs";                   // mfhi
	operations[0x12] = (char*)"rd = LO";                   // mtlo
	operations[0x13] = (char*)"LO = rs";                   // mflo
	operations[0x14] = (char*)"rd = rt << rs (double)";    // dsllv
	sc_operations[0x14] = (char*)"rd <<= rs (double)";
	operations[0x16] = (char*)"rd = rt >> rs (double)";    // dsrlv
	sc_operations[0x16] = (char*)"rd >>= rs (double)";
	operations[0x17] = (char*)"rd = rt >> rs (double)";    // dsrav
	sc_operations[0x17] = (char*)"rd >>= rs (double)";
	operations[0x18] = (char*)"LOHI = rs * rt";            // mult
	operations[0x19] = (char*)"LOHI = rs * rt (unsigned)"; // multu
	operations[0x1A] = (char*)"LOHI = rs / rt";            // div
	operations[0x1B] = (char*)"LOHI = rs / rt (unsigned)"; // divu

	operations[0x20] = (char*)"rd = rs + rt";                   // add
	sc_operations[0x20] = (char* )"rd += rt";
	sc2_operations[0x20] = (char*)"rd = rt";
	operations[0x21] = (char*)"rd = rs + rt (unsigned)";        // addu
	sc_operations[0x21] = (char*)"rd += rt (unsigned)";
	sc2_operations[0x21] = (char*)"rd = rt (unsigned)";
	operations[0x22] = (char*)"rd = rs - rt";                   // sub
	sc_operations[0x22] = (char*)"rd -= rt";
	sc2_operations[0x22] = (char*)"rd = -rt";
	operations[0x23] = (char*)"rd = rs - rt (unsigned)";        // subu
	sc_operations[0x23] = (char*)"rd -= rt (unsigned)";
	sc2_operations[0x23] = (char*)"rd = -rt (unsigned)";        // Umm, what?
	operations[0x24] = (char*)"rd = rs & rt";                   // and
	sc_operations[0x24] = (char*)"rd &= rt";
	operations[0x25] = (char*)"rd = rs | rt";                   // or
	sc_operations[0x25] = (char*)"rd |= rt";
	sc2_operations[0x25] = (char*)"rd = rt";
	operations[0x26] = (char*)"rd = rs ^ rt";                   // xor
	sc_operations[0x26] = (char*)"rd ^= rt";
	operations[0x27] = (char*)"rd = ~(rs | rt)";                // nor
	operations[0x28] = (char*)"MFSA?";                          // mfsa
	operations[0x29] = (char*)"MTSA?";                          // mtsa
	operations[0x2A] = (char*)"rd = (rs < rt)";                 // slt
	operations[0x2B] = (char*)"rd = (rs < rt) (unsigned)";      // sltu
	operations[0x2C] = (char*)"rd = rs + rt (double)";          // dadd
	sc_operations[0x2C] = (char*)"rd += rt (double)";
	sc2_operations[0x2C] = (char*)"rd = rt (double)";
	operations[0x2D] = (char*)"rd = rs + rt (unsigned double)"; // daddu
	sc_operations[0x2D] = (char*)"rd += rt (unsigned double)";
	sc2_operations[0x2D] = (char*)"rd = rt (unsigned double)";
	operations[0x2E] = (char*)"rd = rs - rt (double)";          // dsub
	sc_operations[0x2E] = (char*)"rd -= rt (double)";
	sc2_operations[0x2E] = (char*)"rd = -rt (double)";
	operations[0x2F] = (char*)"rd = rs - rt (unsigned double)"; // dsubu
	sc_operations[0x2F] = (char*)"rd -= rt (unsigned double)";
	sc2_operations[0x2F] = (char*)"rd = -rt (unsigned double)"; // What? Needs figuring. Why would this ever appear?

	operations[0x38] = (char*)"rd = rt << _h (double)";   // dsll
	sc_operations[0x38] = (char*)"rd <<= _h (double)";
	operations[0x3A] = (char*)"rd = rt >> _h (double)";   // dsrl
	sc_operations[0x3A] = (char*)"rd >>= _h (double)";
	operations[0x3B] = (char*)"rd = rt >> _h (double)";   // dsra
	sc_operations[0x3B] = (char*)"rd >>= _h (double)";
	operations[0x3C] = (char*)"rd = rt << _h (double32)"; // dsll32
	sc_operations[0x3C] = (char*)"rd <<= _h (double32)";
	operations[0x3E] = (char*)"rd = rt >> _h (double32)"; // dsrl32
	sc_operations[0x3E] = (char*)"rd >>= _h (double32)";
	operations[0x3F] = (char*)"rd = rt >> _h (double32)"; // dsra32
	sc_operations[0x3F] = (char*)"rd >>= _h (double32)";

	// Immediate operations
	operations[0x48] = (char*)"rt = rs + _i";            // addi
	sc_operations[0x48] = (char*)"rt += _i";
	sc2_operations[0x48] = (char*)"rt = _i";
	operations[0x49] = (char*)"rt = rs + _i (unsigned)"; // addiu (everyone's favourite!... okay, well my favourite anyway.)
	sc_operations[0x49] = (char*)"rt += _i (unsigned)";
	sc2_operations[0x49] = (char*)"rt = _i (unsigned)";
	operations[0x4A] = (char*)"rt = (rs < _i)";          // slti
	operations[0x4B] = (char*)"rt = (rs < _i)";          // Um, sltiu?
	operations[0x4C] = (char*)"rt = rs & _i";            // andi
	sc_operations[0x4C] = (char*)"rt &= _i";
	operations[0x4D] = (char*)"rt = rs | _i";            // ori
	sc_operations[0x4D] = (char*)"rt |= _i";
	sc2_operations[0x4D] = (char*)"rt = _i";
	operations[0x4E] = (char*)"rt = rs ^ _i";            // xori
	sc_operations[0x4E] = (char*)"rt ^= _i";
	operations[0x4F] = (char*)"rt = _i0000";             // lui (everyone's other favourite!... will you STOP looking at me like that?!)

	operations[0x51] = (char*)"rt = fd";                        // mfc1
	// TODO: For some odd reason, there are more floating-point operations to deal with that share the same ID. Ew.
	operations[0x58] = (char*)"rt = rs + _i (dword)";          // daddi
	sc_operations[0x58] = (char*)"rt += _i (dword)";
	sc2_operations[0x58] = (char*)"rt = _i (dword)";
	operations[0x59] = (char*)"rt = rs + _i (unsigned dword)"; // daddiu
	sc_operations[0x59] = (char*)"rt += _i (unsigned dword)";
	sc2_operations[0x59] = (char*)"rt = _i (unsigned dword)";
	operations[0x5E] = (char*)"rt = (quad) rs[_i]";             // lq
	operations[0x5F] = (char*)"rs[_i] = (quad) rt";             // sq
	operations[0x60] = (char*)"rt = (char) rs[_i]";             // lb

	operations[0x61] = (char*)"rt = (short) rs[_i]";          // lh
	operations[0x63] = (char*)"rt = (int) rs[_i]";            // lw
	operations[0x64] = (char*)"rt = (unsigned char) rs[_i]";  // lbu 
	operations[0x65] = (char*)"rt = (unsigned short) rs[_i]"; // lhu
	operations[0x67] = (char*)"rt = (unsigned int) rs[_i]";   // lwu
	operations[0x68] = (char*)"rs[_i] = (char) rt";           // sb
	operations[0x69] = (char*)"rs[_i] = (short) rt";          // sh
	operations[0x6B] = (char*)"rs[_i] = (int) rt";            // sw

	operations[0x71] = (char*)"ft = (float) rs[_i]";  // lwc1
	operations[0x77] = (char*)"rt = (dword) rs[_i]"; // ld
	operations[0x79] = (char*)"rs[_i] = (float) ft";  // swc1
	operations[0x7F] = (char*)"rs[_i] = (dword) rt"; // sd
}

int Get_Reads(unsigned int code, char* registers)
{
	unsigned short note = notes[GET_OPERATION(code)];
	int total = 0;

	if (note & READS_RS)
		registers[total ++] = GET_RS(code);
	if (note & READS_RT)
		registers[total ++] = GET_RT(code);
	if (note & READS_RD)
		registers[total ++] = GET_RD(code);

	return total;
}

int Get_Writes(unsigned int code, char* registers)
{
	unsigned short note = notes[GET_OPERATION(code)];
	int total = 0;

	if (note & WRITES_RS)
		registers[total ++] = GET_RS(code);
	if (note & WRITES_RT)
		registers[total ++] = GET_RT(code);
	if (note & WRITES_RD)
		registers[total ++] = GET_RD(code);

	return total;
}

bool Reads(unsigned int code, char reg)
{
	int id = GET_OPERATION(code);

	if (! notes[id])
		return 0; // Undetermined!

	if (notes[id] & READS_RS) {if (GET_RS(code) == reg) return 1;}
	if (notes[id] & READS_RT) {if (GET_RT(code) == reg) return 1;}
	if (notes[id] & READS_RD) {if (GET_RD(code) == reg) return 1;}

	return 0;
}

bool Writes(unsigned int code, char reg)
{
	int id = GET_OPERATION(code);

	if (! notes[id])
		return 0;

	if (notes[id] & WRITES_RS) {if (GET_RS(code) == reg) return 1;}
	if (notes[id] & WRITES_RT) {if (GET_RT(code) == reg) return 1;}
	if (notes[id] & WRITES_RD) {if (GET_RD(code) == reg) return 1;}

	return 0;
}
