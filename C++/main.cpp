#include "AbstractSyntaxTree.h"

#ifndef _M_X64
void __declspec(naked) assembly()
{
	__asm {
		ret
			// put 32-bit assembly here, put a break point on ret, and open the disassembly window to see the hex values of different assembly instructions
	}
}
#else
extern "C" void assembly();
#endif

int main(int argc, char *argv[])
{
	assembly();
	Compiler::Assembler::runAssemblerUnitTests();
	Compiler::AbstractSyntaxTree::runASTUnitTests();
	return 0;
}
