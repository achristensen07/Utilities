#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <stdint.h>
#include "x86.h"
#include "AssemblerBuffer.h"

namespace Compiler {

// Wrappers to tell the assembler what the value means.
class ImmediateValue32 {
public:
	explicit ImmediateValue32(uint32_t value) : value(value) {};
	uint32_t value;
	operator uint32_t() { return value; }
};

class ImmediateValue64 {
public:
	explicit ImmediateValue64(uint64_t value) : value(value) {};
	explicit ImmediateValue64(double value) : value(*reinterpret_cast<uint64_t*>(&value)) {};
	uint64_t value;
	operator uint64_t() { return value; }
};

#ifdef _M_X64
typedef ImmediateValue64 ImmediateValuePtr;
#else
typedef ImmediateValue32 ImmediateValuePtr;
#endif

// This is an assembler that takes function calls as its input instead of text
// and outputs a memory buffer instead of an object file.
// The parameter order mimics Intel sintax (not AT&T syntax) which is used in Visual Studio.
// The destination is before the source.
class Assembler {
public:
	Assembler(AssemblerBuffer&);

	typedef uint32_t JumpDistanceLocation;

	// data movement
	static uint32_t movOperationSize(ImmediateValue32 value);
	void mov(IntRegister, ImmediateValue32);
	void mov(IntRegister to, IntRegister from);
	void mov(IntRegister destination, IntRegister source, int32_t offset, bool move64Bits);
	void mov(IntRegister destination, int32_t offset, IntRegister source, bool move64Bits);
	void push(IntRegister);
	void push(ImmediateValue32);
	void push(ImmediateValue64);
	void pop();
	void pop64();
	void pop(IntRegister);
	void lea(IntRegister destination, IntRegister source, int32_t offset);
#ifdef _M_X64
	void mov(IntRegister, ImmediateValue64);
	void push(DoubleRegister);
	void pop(DoubleRegister);
#endif

	// arithmetic and logic
	void add(IntRegister, ImmediateValue32); // pointer sized add (for rsp/esp)
	void add(IntRegister, IntRegister); // 32-bit addition
	void sub(IntRegister, ImmediateValue32); // pointer sized subtraction (for rsp/esp)
	void sub(IntRegister, IntRegister); // 32-bit subtraction
	void imul(IntRegister, IntRegister);
	void idiv(IntRegister); // puts quotient in eax and remainder in edx
	void cdq(); // Sign-extends eax into edx (to prepare for idiv)
	void and(IntRegister, IntRegister); // 32-bit bitwise and
	void or(IntRegister, IntRegister); // 32-bit bitwise or
	void xor(IntRegister, IntRegister); // 32-bit bitwise xor
	void shl(IntRegister, IntRegister); // 32-bit signed shift left
	void sar(IntRegister, IntRegister); // 32-bit unsigned shift right

	// control flow
	void cmp(IntRegister, IntRegister);
	void cmp(IntRegister, ImmediateValue32);
	static uint32_t cmpOperationSize(IntRegister reg, ImmediateValue32 value);
	JumpDistanceLocation jmp(Condition, int32_t distance); // jmp, je, jne, jg, jge, jl, jle, ja, jae, jb, jbe
	void setJumpDistance(JumpDistanceLocation location, int32_t distance);
	static uint32_t jmpOperationSize(Condition condition);
	void call(IntRegister);
	void ret();

	// double operations
#ifdef _M_X64
	void cvtsi2sd(DoubleRegister, IntRegister); // Convert Doubleword Integer to Scalar Double-Precision Floating-Point Value
	void cvttsd2si(IntRegister, DoubleRegister); // Convert Scalar Double-Precision Floating-Point Value to Signed Doubleword Integer with Truncation
	void addsd(DoubleRegister, DoubleRegister); // Scalar Double-Precision Floating-Point Add
	void mulsd(DoubleRegister, DoubleRegister); // Scalar Double-Precision Floating-Point Multiply
	void divsd(DoubleRegister, DoubleRegister); // Scalar Double-Precision Floating-Point Divide
	void subsd(DoubleRegister, DoubleRegister); // Scalar Double-Precision Floating-Point Subtract
	void movsd(DoubleRegister to, DoubleRegister from); // Move Scalar Double-Precision Floating-Point Value
	void comisd(DoubleRegister, DoubleRegister); // Compare Scalar Ordered Double-Precision Floating-Point Values and Set EFLAGS
	static uint32_t comisdOperationSize();
	void movsd(DoubleRegister destination, IntRegister source, int32_t offset);
	void movsd(IntRegister destination, int32_t offset, DoubleRegister source);
#else
	void cvttsd2si(IntRegister destination, IntRegister source, int32_t offset);
	static uint32_t fldOperationSize(IntRegister source, int32_t offset);
	void fld(IntRegister source, int32_t offset);
	void fild(IntRegister source, int32_t offset);
	void fstp(IntRegister destination, int32_t offset);
	void fmulp(); // fmulp st(1), st
	void faddp(); // faddp st(1), st
	void fdivp(); // fdivp st(1), st
	void fsubp(); // fsubp st(1), st
	void x87CompareAndPopDoubles(IntRegister mustBeEax);
	static uint32_t x87CompareAndPopDoublesOperationSize();
	void x87Pop();
#endif

	static void runAssemblerUnitTests();

private:

	AssemblerBuffer& buffer;
	void rexPrefixIfNeeded(bool, bool, bool, bool);
	bool needsRexPrefix(IntRegister);
#ifdef _M_X64
	bool needsRexPrefix(DoubleRegister);
#endif
};

} // namespace Compiler

#endif
