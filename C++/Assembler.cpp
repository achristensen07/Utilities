#include "Assembler.h"

namespace Compiler {

const bool is64Bit = sizeof(void*) == 8;

Assembler::Assembler(AssemblerBuffer& buffer) : buffer(buffer) {};

void Assembler::push(IntRegister reg)
{
	// 0x50 pushes eax, 0x51 pushes ecx, ... 0x57 pushes edi
	const uint8_t pushOpcode = 0x50;
	rexPrefixIfNeeded(false, false, false, needsRexPrefix(reg));
	buffer.push8(pushOpcode + (reg % 8));
}

void Assembler::pop(IntRegister reg)
{
	// 0x58 pops eax, 0x59 pops ecx, ... 0x5F pops edi
	const uint8_t popOpcode = 0x58;
	rexPrefixIfNeeded(false, false, false, needsRexPrefix(reg));
	buffer.push8(popOpcode + (reg % 8));
}

void Assembler::pop()
{
	// just move the stack pointer without using the value from the stack
	add(esp, ImmediateValue32(sizeof(void*)));
}

void Assembler::pop64()
{
	add(esp, ImmediateValue32(8));
}

void Assembler::lea(IntRegister destination, IntRegister source, int32_t offset)
{
	rexPrefixIfNeeded(is64Bit, needsRexPrefix(destination), false, needsRexPrefix(source));
	const uint8_t leaOpcode1 = 0x8D;
	const uint8_t leaEspSuffix = 0x24;
	buffer.push8(leaOpcode1);
	if (offset == 0 && (source % 8) != ebp) { // ebp and r13 have no 0-offset opcode for some reason.
		buffer.push8(((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(leaEspSuffix);
	} else if (offset < 0x7F && offset >= -0XFF) {
		const uint8_t leaSmallOffsetOpcode2 = 0x40;
		buffer.push8(leaSmallOffsetOpcode2 + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(leaEspSuffix);
		buffer.push8(static_cast<signed char>(offset));
	} else {
		const uint8_t leaLargeOffsetOpcode2 = 0x80;
		buffer.push8(leaLargeOffsetOpcode2 + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(leaEspSuffix);
		buffer.push32(offset);
	}
}

uint32_t Assembler::movOperationSize(ImmediateValue32 value)
{
	return 5;
}

void Assembler::mov(IntRegister reg, ImmediateValue32 value)
{
	// 0xB8 is to eax, 0xB9 is to ecx, ... 0xBF is to edi
	const uint8_t moveImmediateValueOpcode = 0xB8;
	rexPrefixIfNeeded(false, false, false, needsRexPrefix(reg));
	buffer.push8(moveImmediateValueOpcode + (reg % 8));
	buffer.push32(value);
}

void Assembler::mov(IntRegister to, IntRegister from)
{
	const uint8_t moveOpcode = 0x8B;
	const uint8_t registerToRegisterCode = 0xC0;
	rexPrefixIfNeeded(is64Bit, needsRexPrefix(to), false, needsRexPrefix(from));
	buffer.push8(moveOpcode);
	buffer.push8(registerToRegisterCode | ((to % 8) << 3) | ((from % 8) << 0));
}

void Assembler::mov(IntRegister destination, IntRegister source, int32_t offset, bool move64Bits)
{
	rexPrefixIfNeeded(move64Bits, needsRexPrefix(destination), false, needsRexPrefix(source));
	const uint8_t moveOpcode1 = 0x8B;
	const uint8_t moveEspSuffix = 0x24;
	buffer.push8(moveOpcode1);

	if (!offset && (source % 8) != ebp) { // ebp and r13 have no 0-offset opcode
		const uint8_t moveOpcode2NoOffset = 0x00;
		buffer.push8(moveOpcode2NoOffset + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(moveEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t moveOpcode2SmallOffset = 0x40;
		buffer.push8(moveOpcode2SmallOffset + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(moveEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t moveOpcode2LargeOffset = 0x80;
		buffer.push8(moveOpcode2LargeOffset + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(moveEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::mov(IntRegister destination, int32_t offset, IntRegister source, bool move64Bits)
{
	rexPrefixIfNeeded(move64Bits, needsRexPrefix(source), false, needsRexPrefix(destination));
	const uint8_t moveOpcode1 = 0x89;
	const uint8_t moveEspSuffix = 0x24;
	buffer.push8(moveOpcode1);

	if (!offset && (destination % 8) != ebp) { // ebp and r13 have no 0-offset opcode
		const uint8_t moveOpcode2NoOffset = 0x00;
		buffer.push8(moveOpcode2NoOffset + ((source % 8) << 3) + (destination % 8));
		if ((destination % 8) == esp)
			buffer.push8(moveEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t moveOpcode2SmallOffset = 0x40;
		buffer.push8(moveOpcode2SmallOffset + ((source % 8) << 3) + (destination % 8));
		if ((destination % 8) == esp)
			buffer.push8(moveEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t moveOpcode2LargeOffset = 0x80;
		buffer.push8(moveOpcode2LargeOffset + ((source % 8) << 3) + (destination % 8));
		if ((destination % 8) == esp)
			buffer.push8(moveEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::add(IntRegister reg, ImmediateValue32 value)
{
	compiler_assert(reg == esp, "pointer-sized add used with 32-bit value in non-stack-pointer register");
	const uint8_t addImmediateValueOpcode2 = 0xC0;

	if (value <= 0x7F) {
		const uint8_t addSmallImmediateValueOpcode1 = 0x83;
		rexPrefixIfNeeded(is64Bit, false, false, needsRexPrefix(reg));
		buffer.push8(addSmallImmediateValueOpcode1);
		buffer.push8(addImmediateValueOpcode2 + (reg % 8));
		buffer.push8(static_cast<uint8_t>(value));
	} else if (reg == eax) {
		const uint8_t addLargeImmediateValueEaxOpcode = 0x05;
		buffer.push8(addLargeImmediateValueEaxOpcode);
		buffer.push32(value);
	} else {
		const uint8_t addLargeImmediateValueOpcode1 = 0x81;
		rexPrefixIfNeeded(is64Bit, false, false, needsRexPrefix(reg));
		buffer.push8(addLargeImmediateValueOpcode1);
		buffer.push8(addImmediateValueOpcode2 + (reg % 8));
		buffer.push32(value);
	}
}

void Assembler::add(IntRegister reg1, IntRegister reg2)
{
	compiler_assert(reg1 != esp, "32-bit add used with pointer-sized value in stack pointer register");
	compiler_assert(reg2 != esp, "32-bit add used with pointer-sized value in stack pointer register");
	const uint8_t addRegistersOpcode1 = 0x03;
	const uint8_t addRegistersOpcode2 = 0xC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(addRegistersOpcode1);
	buffer.push8(addRegistersOpcode2 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::sub(IntRegister reg, ImmediateValue32 value)
{
	compiler_assert(reg == esp, "pointer-sized subtract used with 32-bit value in non-stack-pointer register");
	const uint8_t subtractImmediateValueOpcode2 = 0xE8;
	if (value <= 0x7F) {
		const uint8_t subtractSmallImmediateValueOpcode1 = 0x83;
		rexPrefixIfNeeded(is64Bit, false, false, needsRexPrefix(reg));
		buffer.push8(subtractSmallImmediateValueOpcode1);
		buffer.push8(subtractImmediateValueOpcode2 + (reg % 8));
		buffer.push8(static_cast<uint8_t>(value));
	} else if (reg == eax) {
		const uint8_t subtractLargeImmediateValueEaxOpcode = 0x2D;
		buffer.push8(subtractLargeImmediateValueEaxOpcode);
		buffer.push32(value);
	} else {
		const uint8_t subtractLargeImmediateValueOpcode1 = 0x81;
		rexPrefixIfNeeded(is64Bit, false, false, needsRexPrefix(reg));
		buffer.push8(subtractLargeImmediateValueOpcode1);
		buffer.push8(subtractImmediateValueOpcode2 + (reg % 8));
		buffer.push32(value);
	}
}

void Assembler::sub(IntRegister reg1, IntRegister reg2)
{
	compiler_assert(reg1 != esp, "32-bit subtract used with pointer-sized value in stack pointer register");
	compiler_assert(reg2 != esp, "32-bit subtract used with pointer-sized value in stack pointer register");
	const uint8_t subRegistersOpcode1 = 0x2B;
	const uint8_t subRegistersOpcode2 = 0xC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(subRegistersOpcode1);
	buffer.push8(subRegistersOpcode2 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::cdq()
{
	const uint8_t cdqOpcode = 0x99;
	buffer.push8(cdqOpcode);
}

void Assembler::and(IntRegister reg1, IntRegister reg2)
{
	const uint8_t andOpcode1 = 0x23;
	const uint8_t andOpcode2 = 0xC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(andOpcode1);
	buffer.push8(andOpcode2 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::or(IntRegister reg1, IntRegister reg2)
{
	const uint8_t orOpcode1 = 0x0B;
	const uint8_t orOpcode2 = 0xC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(orOpcode1);
	buffer.push8(orOpcode2 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::xor(IntRegister reg1, IntRegister reg2)
{
	const uint8_t orOpcode1 = 0x33;
	const uint8_t orOpcode2 = 0xC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(orOpcode1);
	buffer.push8(orOpcode2 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::shl(IntRegister reg1, IntRegister reg2)
{
	compiler_assert(reg1 == eax && reg2 == ecx, "unsupported register shift");
	buffer.push8(0xD3); // shl eax, cl
	buffer.push8(0xE0);
}

void Assembler::sar(IntRegister reg1, IntRegister reg2)
{
	compiler_assert(reg1 == eax && reg2 == ecx, "unsupported register shift");
	buffer.push8(0xD3); // sar eax, cl
	buffer.push8(0xF8);
}

void Assembler::idiv(IntRegister reg)
{
	const uint8_t idivOpcode1 = 0xF7;
	const uint8_t idivOpcode2 = 0xF8;
	rexPrefixIfNeeded(false, needsRexPrefix(reg), false, false);
	buffer.push8(idivOpcode1);
	buffer.push8(idivOpcode2 + (reg % 8));
}

void Assembler::imul(IntRegister reg1, IntRegister reg2)
{
	const uint8_t imulOpcode1 = 0x0F;
	const uint8_t imulOpcode2 = 0xAF;
	const uint8_t imulOpcode3 = 0XC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(imulOpcode1);
	buffer.push8(imulOpcode2);
	buffer.push8(imulOpcode3 + ((reg1 % 8) << 8) + (reg2 % 8));
}

void Assembler::push(ImmediateValue32 value)
{
	if (value <= 0x7F) {
		const uint8_t pushSmallImmediateValueOpcode = 0x6A;
		buffer.push8(pushSmallImmediateValueOpcode);
		buffer.push8(static_cast<uint8_t>(value));
	} else {
		const uint8_t pushLargeImmediateValueOpcode = 0x68;
		buffer.push8(pushLargeImmediateValueOpcode);
		buffer.push32(value);
	}
}

void Assembler::push(ImmediateValue64 value)
{
#ifdef _M_X64
	sub(esp, ImmediateValue32(8));
	buffer.push32(0x042444C7); // C7 44 24 04 means "mov dword ptr [rsp + 4], (32-bit immediate value follows)"
	buffer.push32(value >> 32);
	buffer.push8(0xC7); // C7 04 24 means "mov dword ptr[rsp], (32-bit immediate value follows)"
	buffer.push8(0x04);
	buffer.push8(0x24);
	buffer.push32(value & 0xFFFFFFFF);
#else
	push(ImmediateValue32(value >> 32)); // decrements esp by 4
	push(ImmediateValue32(value & 0xFFFFFFFF)); // decrements esp by 4
#endif
}

#ifdef _M_X64

void Assembler::mov(IntRegister reg, ImmediateValue64 value)
{
	// 0xB8 is to eax, 0xB9 is to ecx, ... 0xBF is to edi
	const uint8_t moveImmediateValueOpcode = 0xB8;
	rexPrefixIfNeeded(true, false, false, needsRexPrefix(reg));
	buffer.push8(moveImmediateValueOpcode + (reg % 8));
	buffer.push64(value);
}

void Assembler::push(DoubleRegister reg)
{
	sub(esp, ImmediateValue32(8));
	movsd(esp, 0, reg);
}

void Assembler::pop(DoubleRegister reg)
{
	movsd(reg, esp, 0);
	add(esp, ImmediateValue32(8));
}

uint32_t Assembler::comisdOperationSize()
{
	return 4;
}

void Assembler::comisd(DoubleRegister reg1, DoubleRegister reg2)
{
	const uint8_t comisdOpcode1 = 0x66;
	const uint8_t comisdOpcode2 = 0x0F;
	const uint8_t comisdOpcode3 = 0x2F;
	const uint8_t comisdOpcode4 = 0xC0;
	buffer.push8(comisdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(comisdOpcode2);
	buffer.push8(comisdOpcode3);
	buffer.push8(comisdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::movsd(DoubleRegister to, DoubleRegister from)
{
	const uint8_t movsdOpcode1 = 0xF2;
	const uint8_t movsdOpcode2 = 0x0F;
	const uint8_t movsdOpcode3 = 0x10;
	const uint8_t movsdOpcode4 = 0xC0;
	buffer.push8(movsdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(to), false, needsRexPrefix(from));
	buffer.push8(movsdOpcode2);
	buffer.push8(movsdOpcode3);
	buffer.push8(movsdOpcode4 + ((to % 8) << 3) + (from % 8));
}

void Assembler::movsd(DoubleRegister destination, IntRegister source, int32_t offset)
{
	const uint8_t movsdSourceOffsetOpcode1 = 0xF2;
	const uint8_t movsdSourceOffsetOpcode2 = 0x0F;
	const uint8_t movsdSourceOffsetOpcode3 = 0x10;
	const uint8_t movsdSourceOffsetEspSuffix = 0x24;
	buffer.push8(movsdSourceOffsetOpcode1);
	rexPrefixIfNeeded(true, needsRexPrefix(destination), false, needsRexPrefix(source));
	buffer.push8(movsdSourceOffsetOpcode2);
	buffer.push8(movsdSourceOffsetOpcode3);
	if (!offset && (source % 8) != ebp) { // ebp and r13 have no 0-offset opcode
		const uint8_t movsdSourceOffsetOpcode4NoOffset = 0x00;
		buffer.push8(movsdSourceOffsetOpcode4NoOffset + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(movsdSourceOffsetEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t movsdSourceOffsetOpcode4SmallOffset = 0x40;
		buffer.push8(movsdSourceOffsetOpcode4SmallOffset + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(movsdSourceOffsetEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t movsdSourceOffsetOpcode4LargeOffset = 0x80;
		buffer.push8(movsdSourceOffsetOpcode4LargeOffset + ((destination % 8) << 3) + (source % 8));
		if ((source % 8) == esp)
			buffer.push8(movsdSourceOffsetEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::movsd(IntRegister destination, int32_t offset, DoubleRegister source)
{
	const uint8_t movsdSourceOffsetOpcode1 = 0xF2;
	const uint8_t movsdSourceOffsetOpcode2 = 0x0F;
	const uint8_t movsdSourceOffsetOpcode3 = 0x11;
	const uint8_t movsdSourceOffsetEspSuffix = 0x24;
	buffer.push8(movsdSourceOffsetOpcode1);
	rexPrefixIfNeeded(true, needsRexPrefix(source), false, needsRexPrefix(destination));
	buffer.push8(movsdSourceOffsetOpcode2);
	buffer.push8(movsdSourceOffsetOpcode3);
	if (!offset && (destination % 8) != ebp) { // ebp and r13 have no 0-offset opcode
		const uint8_t movsdSourceOffsetOpcode4NoOffset = 0x00;
		buffer.push8(movsdSourceOffsetOpcode4NoOffset + ((source % 8) << 3) + (destination % 8));
		if ((destination % 8) == esp)
			buffer.push8(movsdSourceOffsetEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t movsdSourceOffsetOpcode4SmallOffset = 0x40;
		buffer.push8(movsdSourceOffsetOpcode4SmallOffset + ((source % 8) << 3) + (destination % 8));
		if ((destination % 8) == esp)
			buffer.push8(movsdSourceOffsetEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t movsdSourceOffsetOpcode4LargeOffset = 0x80;
		buffer.push8(movsdSourceOffsetOpcode4LargeOffset + ((source % 8) << 3) + (destination % 8));
		if ((destination % 8) == esp)
			buffer.push8(movsdSourceOffsetEspSuffix);
		buffer.push32(offset);
	}
}

#else

uint32_t Assembler::fldOperationSize(IntRegister source, int32_t offset)
{
	if (!offset && source != ebp)
		return 2 + (source == esp);
	else if (-128 <= offset && offset <= 127)
		return 3 + (source == esp);
	else
		return 6 + (source == esp);
}

void Assembler::fld(IntRegister source, int32_t offset)
{
	const uint8_t fldOpcode1 = 0xDD;
	const uint8_t fldEspSuffix = 0x24;
	buffer.push8(fldOpcode1);
	if (!offset && source != ebp) { // ebp has no 0-offset opcode
		const uint8_t fldNoOffsetOpcode2 = 0x00;
		buffer.push8(fldNoOffsetOpcode2 + source);
		if (source == esp)
			buffer.push8(fldEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t fldSmallOffsetOpcode2 = 0x40;
		buffer.push8(fldSmallOffsetOpcode2 + source);
		if (source == esp)
			buffer.push8(fldEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t fldLargeOffsetOpcode2 = 0x80;
		buffer.push8(fldLargeOffsetOpcode2 + source);
		if (source == esp)
			buffer.push8(fldEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::fstp(IntRegister destination, int32_t offset)
{
	const uint8_t fstpOpcode1 = 0xDD;
	const uint8_t fstpEspSuffix = 0x24;
	buffer.push8(fstpOpcode1);
	if (!offset && destination != ebp) { // ebp has no 0-offset opcode
		const uint8_t fstpNoOffsetOpcode2 = 0x18;
		buffer.push8(fstpNoOffsetOpcode2 + destination);
		if (destination == esp)
			buffer.push8(fstpEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t fstpSmallOffsetOpcode2 = 0x58;
		buffer.push8(fstpSmallOffsetOpcode2 + destination);
		if (destination == esp)
			buffer.push8(fstpEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t fstpLargeOffsetOpcode2 = 0x98;
		buffer.push8(fstpLargeOffsetOpcode2 + destination);
		if (destination == esp)
			buffer.push8(fstpEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::fild(IntRegister source, int32_t offset)
{
	const uint8_t fildOpcode1 = 0xDB;
	const uint8_t fildEspSuffix = 0x24;
	buffer.push8(fildOpcode1);
	if (!offset && source != ebp) { // ebp has no 0-offset opcode
		const uint8_t fildNoOffsetOpcode2 = 0x00;
		buffer.push8(fildNoOffsetOpcode2 + source);
		if (source == esp)
			buffer.push8(fildEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t fildSmallOffsetOpcode2 = 0x40;
		buffer.push8(fildSmallOffsetOpcode2 + source);
		if (source == esp)
			buffer.push8(fildEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t fildLargeOffsetOpcode2 = 0x80;
		buffer.push8(fildLargeOffsetOpcode2 + source);
		if (source == esp)
			buffer.push8(fildEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::cvttsd2si(IntRegister destination, IntRegister source, int32_t offset)
{
	const uint8_t cvttsd2siOpcode1 = 0xF2;
	const uint8_t cvttsd2siOpcode2 = 0x0F;
	const uint8_t cvttsd2siOpcode3 = 0x2C;
	const uint8_t cvttsd2siEspSuffix = 0x24;
	buffer.push8(cvttsd2siOpcode1);
	buffer.push8(cvttsd2siOpcode2);
	buffer.push8(cvttsd2siOpcode3);
	if (!offset && source != ebp) { // ebp has no 0-offset opcode
		const uint8_t cvttsd2siNoOffsetOpcode4 = 0x00;
		buffer.push8(cvttsd2siNoOffsetOpcode4 + (destination << 3) + source);
		if (source == esp)
			buffer.push8(cvttsd2siEspSuffix);
	} else if (-128 <= offset && offset <= 127) {
		const uint8_t cvttsd2siSmallOffsetOpcode4 = 0x40;
		buffer.push8(cvttsd2siSmallOffsetOpcode4 + (destination << 3) + source);
		if (source == esp)
			buffer.push8(cvttsd2siEspSuffix);
		buffer.push8(static_cast<uint8_t>(offset));
	} else {
		const uint8_t cvttsd2siLargeOffsetOpcode4 = 0x80;
		buffer.push8(cvttsd2siLargeOffsetOpcode4 + (destination << 3) + source);
		if (source == esp)
			buffer.push8(cvttsd2siEspSuffix);
		buffer.push32(offset);
	}
}

void Assembler::fmulp()
{
	// fmulp st(1), st
	buffer.push8(0xDE);
	buffer.push8(0xC9);
}

uint32_t Assembler::x87CompareAndPopDoublesOperationSize()
{
	return 6;
}

void Assembler::x87CompareAndPopDoubles(IntRegister mustBeEax)
{
	// This puts flags in ax temporarily, which changes what is in eax.  
	// I just want you to have to have eax as a parameter to see this from the call.
	compiler_assert(mustBeEax == eax, "x87CompareAndPopDoubles requires eax right now"); 
	buffer.push8(0xDE); // compp
	buffer.push8(0xD9);

	buffer.push8(0x9B); // wait

	buffer.push8(0xDF); // fnstsw ax
	buffer.push8(0xE0);

	buffer.push8(0x9E); // sahf
}

void Assembler::x87Pop()
{
	buffer.push8(0xDD); // ffree st(0)
	buffer.push8(0xC0);
	buffer.push8(0xD9); // fincstp
	buffer.push8(0xF7);
}

void Assembler::faddp()
{
	// faddp st(1), st
	buffer.push8(0xDE);
	buffer.push8(0xC1);
}

void Assembler::fdivp()
{
	// fdivp st(1), st
	buffer.push8(0xDE);
	buffer.push8(0xF1);
}

void Assembler::fsubp()
{
	// fsubp st(1), st
	buffer.push8(0xDE);
	buffer.push8(0xE1);
}

#endif

void Assembler::setJumpDistance(uint32_t location, int32_t distance)
{
	buffer.setByte(location + 0, static_cast<uint8_t>(distance >> 0));
	buffer.setByte(location + 1, static_cast<uint8_t>(distance >> 8));
	buffer.setByte(location + 2, static_cast<uint8_t>(distance >> 16));
	buffer.setByte(location + 3, static_cast<uint8_t>(distance >> 24));
}

uint32_t Assembler::jmpOperationSize(Condition condition)
{
	return condition == Always ? 5 : 6;
}

Assembler::JumpDistanceLocation Assembler::jmp(Condition condition, int32_t distance)
{
	uint32_t previousSize = buffer.size();
	if (condition == Always) {
		const uint8_t largeJumpOpcode = 0xE9;
		buffer.push8(largeJumpOpcode);
		buffer.push32(distance);
		return previousSize + 1; // location of jump distance
	} else {
		const uint8_t largeJumpConditionOpcode1 = 0x0F;
		buffer.push8(largeJumpConditionOpcode1);
		buffer.push8(condition);
		buffer.push32(distance);
		return previousSize + 2; // location of jump distance
	}
}

void Assembler::cmp(IntRegister reg1, IntRegister reg2)
{
	const uint8_t compareOpcode1 = 0x3B;
	const uint8_t compareOpcode2 = 0xC0;
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(compareOpcode1);
	buffer.push8(compareOpcode2 | (reg1 << 3) | (reg2 << 0));
}

uint32_t Assembler::cmpOperationSize(IntRegister reg, ImmediateValue32 value)
{
	if (value <= 0x7F)
		return 3;
	else if (reg == eax)
		return 5;
	else
		return 6;
}

void Assembler::cmp(IntRegister reg, ImmediateValue32 value)
{
	const uint8_t compareImmediateValueOpcode = 0xF8;
	rexPrefixIfNeeded(false, false, false, needsRexPrefix(reg));
	if (value <= 0x7F) {
		const uint8_t compareSmallImmediateValuePrefix = 0x83;
		buffer.push8(compareSmallImmediateValuePrefix);
		buffer.push8(compareImmediateValueOpcode + (reg % 8));
		buffer.push8(static_cast<uint8_t>(value));
	} else if (reg == eax) {
		const uint8_t compareLargeImmediateValueEAXOpcode = 0x3D;
		buffer.push8(compareLargeImmediateValueEAXOpcode);
		buffer.push32(value);
	} else {
		const uint8_t compareLargeImmediateValuePrefix = 0x81;
		buffer.push8(compareLargeImmediateValuePrefix);
		buffer.push8(compareImmediateValueOpcode + (reg % 8));
		buffer.push32(value);
	}
}

void Assembler::ret()
{
	const uint8_t returnOpcode = 0xC3;
	buffer.push8(returnOpcode);
}

void Assembler::call(IntRegister reg)
{
	const uint8_t callOpcode1 = 0xFF;
	const uint8_t callOpcode2 = 0xD0;
	rexPrefixIfNeeded(false, false, false, needsRexPrefix(reg));
	buffer.push8(callOpcode1);
	buffer.push8(callOpcode2 + (reg % 8));
}

#ifdef _M_X64
void Assembler::addsd(DoubleRegister reg1, DoubleRegister reg2)
{
	const uint8_t addsdOpcode1 = 0xF2;
	const uint8_t addsdOpcode2 = 0x0F;
	const uint8_t addsdOpcode3 = 0x58;
	const uint8_t addsdOpcode4 = 0xC0;
	buffer.push8(addsdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(addsdOpcode2);
	buffer.push8(addsdOpcode3);
	buffer.push8(addsdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::mulsd(DoubleRegister reg1, DoubleRegister reg2)
{
	const uint8_t mulsdOpcode1 = 0xF2;
	const uint8_t mulsdOpcode2 = 0x0F;
	const uint8_t mulsdOpcode3 = 0x59;
	const uint8_t mulsdOpcode4 = 0xC0;
	buffer.push8(mulsdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(mulsdOpcode2);
	buffer.push8(mulsdOpcode3);
	buffer.push8(mulsdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::divsd(DoubleRegister reg1, DoubleRegister reg2)
{
	const uint8_t divsdOpcode1 = 0xF2;
	const uint8_t divsdOpcode2 = 0x0F;
	const uint8_t divsdOpcode3 = 0x5E;
	const uint8_t divsdOpcode4 = 0xC0;
	buffer.push8(divsdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(divsdOpcode2);
	buffer.push8(divsdOpcode3);
	buffer.push8(divsdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::subsd(DoubleRegister reg1, DoubleRegister reg2)
{
	const uint8_t subsdOpcode1 = 0xF2;
	const uint8_t subsdOpcode2 = 0x0F;
	const uint8_t subsdOpcode3 = 0x5C;
	const uint8_t subsdOpcode4 = 0xC0;
	buffer.push8(subsdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2));
	buffer.push8(subsdOpcode2);
	buffer.push8(subsdOpcode3);
	buffer.push8(subsdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::cvtsi2sd(DoubleRegister reg1, IntRegister reg2)
{
	const uint8_t cvtsi2sdOpcode1 = 0xF2;
	const uint8_t cvtsi2sdOpcode2 = 0x0F;
	const uint8_t cvtsi2sdOpcode3 = 0x2A;
	const uint8_t cvtsi2sdOpcode4 = 0xC0;
	buffer.push8(cvtsi2sdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2)); // w is false because we are converting to 32-bit integers
	buffer.push8(cvtsi2sdOpcode2);
	buffer.push8(cvtsi2sdOpcode3);
	buffer.push8(cvtsi2sdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}

void Assembler::cvttsd2si(IntRegister reg1, DoubleRegister reg2)
{
	const uint8_t cvtsi2sdOpcode1 = 0xF2;
	const uint8_t cvtsi2sdOpcode2 = 0x0F;
	const uint8_t cvtsi2sdOpcode3 = 0x2C;
	const uint8_t cvtsi2sdOpcode4 = 0xC0;
	buffer.push8(cvtsi2sdOpcode1);
	rexPrefixIfNeeded(false, needsRexPrefix(reg1), false, needsRexPrefix(reg2)); // w is false because we are converting from 32-bit integers
	buffer.push8(cvtsi2sdOpcode2);
	buffer.push8(cvtsi2sdOpcode3);
	buffer.push8(cvtsi2sdOpcode4 + ((reg1 % 8) << 3) + (reg2 % 8));
}
#endif

// x86 doesn't use 64-bit operands or extended registers
// x86_64 requires a prefix byte indicating the use of a 64-bit operand or the use of r8 - r15
// http://wiki.osdev.org/X86-64_Instruction_Encoding#REX_prefix
void Assembler::rexPrefixIfNeeded(bool w, bool r, bool x, bool b)
{
#ifdef _M_X64
	const uint8_t rexPrefix = 0x40;
	if (w || r || x || b)
		buffer.push8(rexPrefix
		| (static_cast<uint8_t>(w) << 3)
		| (static_cast<uint8_t>(r) << 2)
		| (static_cast<uint8_t>(x) << 1)
		| (static_cast<uint8_t>(b) << 0));
#else
	compiler_assert(!w, "x86 should never need a rex prefix");
	compiler_assert(!r, "x86 should never need a rex prefix");
	compiler_assert(!x, "x86 should never need a rex prefix");
	compiler_assert(!b, "x86 should never need a rex prefix");
#endif
}

bool Assembler::needsRexPrefix(IntRegister r)
{
#ifdef _M_X64
	return r >= r8;
#else
	compiler_assert(r <= edi, "register out of range");
	return false;
#endif
}

#ifdef _M_X64
bool Assembler::needsRexPrefix(DoubleRegister r)
{
	return r >= xmm8;
}
#endif

} // namespace Compiler
