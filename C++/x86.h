#ifndef X86_H
#define X86_H

namespace Compiler {

// Registers for storing integers and pointers
enum IntRegister : uint8_t {
	eax = 0, // accumulator
	ecx = 1, // counter
	edx = 2, // data (cdq and idiv change values in this register)
	ebx = 3, // base (callee saved)
	esp = 4, // stack pointer (not a general purpose register)
	ebp = 5, // base pointer (not a general purpose register)
	esi = 6, // source index (callee saved)
	edi = 7, // destination index (callee saved)
#ifdef _M_X64
	r8 = 8, // extended registers above edi are only available on x86_64 processors
	r9 = 9, // (operations using them need a rex prefix)
	r10 = 10,
	r11 = 11,
	r12 = 12, // (callee saved)
	r13 = 13, // (callee saved)
	r14 = 14, // (callee saved)
	r15 = 15, // (callee saved)
#endif
};

// Registers for storing doubles and floats
#ifdef _M_X64
enum DoubleRegister : uint8_t {
	xmm0 = 0,
	xmm1 = 1,
	xmm2 = 2,
	xmm3 = 3,
	xmm4 = 4,
	xmm5 = 5,
	xmm6 = 6,
	xmm7 = 7,
	xmm8 = 8,
	xmm9 = 9,
	xmm10 = 10,
	xmm11 = 11,
	xmm12 = 12,
	xmm13 = 13,
	xmm14 = 14,
	xmm15 = 15
};
#endif	

enum Condition : uint8_t {
	Always = 0xFF, // just to be different than the other conditions whose values matter

	// unsigned comparison (and floating point comparison)
	// see https://courses.engr.illinois.edu/ece390/books/artofasm/CH14/CH14-5.html
	Below = 0x82,
	NotBelow = 0x83,
	AboveOrEqual = 0x83,
	BelowOrEqual = 0x86,
	NotBelowOrEqual = 0x87,
	Above = 0x87,

	Zero = 0x84,
	Equal = 0x84,
	NonZero = 0x85,
	NotEqual = 0x85,

	// signed comparison
	LessThan = 0x8C,
	GreaterThanOrEqual = 0x8D,
	LessThanOrEqual = 0x8E,
	GreaterThan = 0x8F,
};

} // namespace Compiler

#endif
