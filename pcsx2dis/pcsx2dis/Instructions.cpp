#define op(id, a, b, c, d) \
{Inst* ref = &instructions[0x##id]; ref->type = a, ref->mnemonic = b; ref->c_operator = c; ref->c_shortcut_operator = d;}

#define MICTYPE_ARITHMETIC  0x01
#define MICTYPE_JUMP        0x02
#define MICTYPE_CONDITIONAL 0x04
#define MICTYPE_UNSIGNED    0x08

struct Inst
{
	int type;
	char* mnemonic;

	char* c_operator;
	char* c_shortcut_operator;
};

Inst instructions[0x7F];

void Converter_Setup()
{
op(20, MICTYPE_ARITHMETIC, (char*)"add", (char*)"+", (char*)"+=");
op(21, MICTYPE_ARITHMETIC | MICTYPE_UNSIGNED, (char*)"addu", (char*)"+", (char*)"+=");
op(22, MICTYPE_ARITHMETIC, (char*)"sub", (char*)"-", (char*)"-=");
op(23, MICTYPE_ARITHMETIC | MICTYPE_UNSIGNED, (char*)"subu", (char*)"-", (char*)"-=");
}
