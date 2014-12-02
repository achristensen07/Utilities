#include "AbstractSyntaxTree.h"
#include "Assembler.h"
#include <algorithm>

namespace Compiler {

StackOffset AbstractSyntaxTree::parameterStackOffset;
std::vector<std::map<std::string, std::pair<DataType, StackOffset>>> AbstractSyntaxTree::scopes;
std::vector<const ASTNode*> AbstractSyntaxTree::scopeParents;
StackOffset AbstractSyntaxTree::stackOffset;
std::map<std::string, StackOffset> AbstractSyntaxTree::stringLiteralLocations;

#define MAKE_PAGE_ALLOCATOR(classname) \
	static PageAllocator<sizeof(classname)> classname##allocator; \
	void* classname::operator new(size_t size) { assert(size == sizeof(classname)); return classname##allocator.allocate(); } \
	void classname::operator delete(void* ptr) { classname##allocator.deallocate(ptr); }

MAKE_PAGE_ALLOCATOR(ASTCast);
MAKE_PAGE_ALLOCATOR(ASTSwitch);
MAKE_PAGE_ALLOCATOR(ASTForLoop);
MAKE_PAGE_ALLOCATOR(ASTLiteral);
MAKE_PAGE_ALLOCATOR(ASTIfElse);
MAKE_PAGE_ALLOCATOR(ASTContinue);
MAKE_PAGE_ALLOCATOR(ASTBreak);
MAKE_PAGE_ALLOCATOR(ASTDefault);
MAKE_PAGE_ALLOCATOR(ASTBinaryOperation);
MAKE_PAGE_ALLOCATOR(ASTUnaryOperation);
MAKE_PAGE_ALLOCATOR(ASTCase);
MAKE_PAGE_ALLOCATOR(ASTFunctionCall);
MAKE_PAGE_ALLOCATOR(ASTDeclareLocalVar);
MAKE_PAGE_ALLOCATOR(ASTGetLocalVar);
MAKE_PAGE_ALLOCATOR(ASTSetLocalVar);
MAKE_PAGE_ALLOCATOR(ASTReturn);
MAKE_PAGE_ALLOCATOR(ASTScope);
MAKE_PAGE_ALLOCATOR(ASTWhileLoop);

const bool is64Bit = sizeof(void*) == 8;

void AbstractSyntaxTree::pushPossibleStringLiterals(AssemblerBuffer& buffer)
{
	Assembler a(buffer);
	for (const std::string& s : possibleStringLiterals) {

		// make sure pointer-multiple-length strings are null-terminated, too
		if (!(s.size() % sizeof(void*))) {
			a.push(ImmediateValuePtr(static_cast<uintptr_t>(0)));
			stackOffset += sizeof(void*);
		}

		for (size_t i = 0; i < s.size(); i += sizeof(void*)) {
			size_t j = ((s.size() + sizeof(void*) - 1) / sizeof(void*)) * sizeof(void*) - i;
			// push pointer-sized blocks of the string onto the stack
			uintptr_t block =
				((j - 1 < s.size() ? s[j - 1] : 0) << 24) +
				((j - 2 < s.size() ? s[j - 2] : 0) << 16) +
				((j - 3 < s.size() ? s[j - 3] : 0) << 8) +
				((j - 4 < s.size() ? s[j - 4] : 0) << 0);
#ifdef _M_X64
			block <<= 32;
			block +=
				((j - 5 < s.size() ? s[j - 5] : 0) << 24) +
				((j - 6 < s.size() ? s[j - 6] : 0) << 16) +
				((j - 7 < s.size() ? s[j - 7] : 0) << 8) +
				((j - 8 < s.size() ? s[j - 8] : 0) << 0);
#endif
			a.push(ImmediateValuePtr(block));
			stackOffset += sizeof(void*);
		}
		compiler_assert(stringLiteralLocations.find(s) == stringLiteralLocations.end(), "duplicate possible string literal found");
		stringLiteralLocations[s] = stackOffset;
	}
}

void AbstractSyntaxTree::processParameters(AssemblerBuffer& buffer)
{
	Assembler a(buffer);
	parameterStackOffset = -static_cast<StackOffset>(sizeof(void*)); // the return address is at stack offset 0.  the parameters are just before it.
	compiler_assert(scopes.size() == 1, "no scope when processing parameters");
	compiler_assert(scopes[0].size() == 0, "non-empty scope when processing parameters");
	for (std::pair<DataType, std::string>& parameterInfo : parameters) {
		DataType dataType = parameterInfo.first;
		std::string name = parameterInfo.second;
		compiler_assert(scopes[0].find(name) == scopes[0].end(), "duplicate parameter name");
		scopes[0][name] = std::pair<DataType, StackOffset>(dataType, parameterStackOffset);
		switch (dataType) {
		case Double:
			parameterStackOffset -= sizeof(double);
			break;
		case Int32:
		case Pointer:
		case CharStar:
			parameterStackOffset -= sizeof(void*);
			break;
		default:
			compiler_assert(0, "invalid parameter type");
			break;
		}
	}
#ifdef _M_X64
	// move register parameters from registers to shadow space on the stack for consistent accessing (which is after the return address pointer)
	if (parameters.size() > 0) {
		if (parameters[0].first == Double)
			a.movsd(esp, 8, xmm0);
		else
			a.mov(esp, 8, ecx, true);
	}
	if (parameters.size() > 1) {
		if (parameters[1].first == Double)
			a.movsd(esp, 16, xmm1);
		else
			a.mov(esp, 16, edx, true);
	}
	if (parameters.size() > 2) {
		if (parameters[2].first == Double)
			a.movsd(esp, 24, xmm2);
		else
			a.mov(esp, 24, r8, true);
	}
	if (parameters.size() > 3) {
		if (parameters[3].first == Double)
			a.movsd(esp, 32, xmm3);
		else
			a.mov(esp, 32, r9, true);
	}
	// other parameters are already on the stack above the shadow space
#endif
}

void AbstractSyntaxTree::compile(AssemblerBuffer& buffer)
{
	try {
		Assembler a(buffer);

		stringLiteralLocations.clear();
		stackOffset = 0;
		scopes.clear();
		scopeParents.clear();

		incrementScope(nullptr); // function scope has no ASTNode parent
		processParameters(buffer);
		StackOffset originalParameterStackOffset = parameterStackOffset;
		compiler_assert(parameterStackOffset <= 0, "parameter stack offset must be non-positive");
		pushPossibleStringLiterals(buffer);
		StackOffset stringLiteralsSizeOnStack = stackOffset;
		for (const std::unique_ptr<ASTNode>& statement : statements) {
			statement->compile(buffer);
#ifndef _M_X64
			if (statement->dataType == Double && statement->nodeType() != Return)
				a.x87Pop();
#endif
		}
		deallocateVariablesAndDecrementScope(buffer);

		compiler_assert(parameterStackOffset == originalParameterStackOffset, "parameter stack offset changed");
		compiler_assert(scopeParents.size() == 0, "extra scope parents");
		compiler_assert(stackOffset == stringLiteralsSizeOnStack, "extra room on stack");
		compiler_assert(scopes.size() == 0, "extra scopes");
	}
	catch (std::exception& exception) {
		std::string message = exception.what();
		stackOffset = 0;
		stringLiteralLocations.clear();
		scopes.clear();
		scopeParents.clear();
		throw exception;
	}
}

AbstractSyntaxTree::~AbstractSyntaxTree()
{
}

ASTNode::~ASTNode()
{
}

static int stringBracketHelper(std::string* address, int index)
{
	assert(address->size() == strlen(address->c_str())); // not compile_assert because that would hurt performance
	return (*address)[index];
}

static const char* stringCStrHelper(std::string* address)
{
	assert(address->size() == strlen(address->c_str())); // not compile_assert because that would hurt performance
	return address->c_str();
}

static void stringConstructorHelper(std::string* address)
{
	new(address)std::string(); // placement new because we already have the memory allocated on the stack
	assert(address->size() == strlen(address->c_str())); // not compile_assert because that would hurt performance
}

static void stringConstructorHelperCharStar(std::string* address, char* initialValue)
{
	new(address)std::string(initialValue); // placement new because we already have the memory allocated on the stack
	assert(address->size() == strlen(address->c_str())); // not compile_assert because that would hurt performance
	assert(address->size() == strlen(initialValue)); // not compile_assert because that would hurt performance
	assert(strcmp(address->c_str(), initialValue) == 0); // not compile_assert because that would hurt performance
}

static void stringDestructorHelper(std::string* address)
{
	using std::string;
	assert(address->size() == strlen(address->c_str())); // not compile_assert because that would hurt performance
	address->~string(); // call destructor explicitly (inverse of placement new)
}

static std::string* stringAssignmentHelper(std::string* address, char* valueToAssign)
{
	assert(address->size() == strlen(address->c_str())); // not compile_assert because that would hurt performance
	*address = valueToAssign; // call operator=
	assert(strcmp(address->c_str(), valueToAssign) == 0); // not compile_assert because that would hurt performance
	return address;
}

bool varInfoComparator(const std::pair<DataType, StackOffset>& a, const std::pair<DataType, StackOffset>& b)
{
	compiler_assert(a.second != b.second, "two variables have same stack location");
	return a.second > b.second;
}

static uint32_t deallocateVariables(AssemblerBuffer& buffer, size_t scopeIndex)
{
	compiler_assert(scopeIndex < AbstractSyntaxTree::scopes.size(), "scope out of range");
	const std::map<std::string, std::pair<DataType, StackOffset>>& scope = AbstractSyntaxTree::scopes[scopeIndex];
	Assembler a(buffer);
	uint32_t totalSize = 0;
	std::vector<std::pair<DataType, StackOffset>> varInfos;
	for (auto iterator = scope.begin(); iterator != scope.end(); iterator++)
		varInfos.push_back(std::pair<DataType, StackOffset>(iterator->second.first, iterator->second.second));

	std::sort(varInfos.begin(), varInfos.end(), varInfoComparator); // reverse sort variables by stack location to deallocate them in the inverse order of their allocation (later-allocated variables are always lower in the stack)

	for (size_t i = 0; i < varInfos.size(); i++) {
		const std::pair<DataType, StackOffset>& variableInfo = varInfos[i];
		int32_t requiredSize = 0;

		switch (variableInfo.first) {
		case Undetermined:
		case None:
		default:
			compiler_assert(0, "deallocating variable without valid type");
			break;
		case Int32:
			// no destructor
			requiredSize = variableInfo.second < 0 ? sizeof(void*) : sizeof(int32_t); // int parameters take the size of a pointer on the stack, int variabes take 4 bytes
			break;
		case Double:
			// no destructor
			requiredSize = sizeof(double);
			break;
		case Pointer:
			// no destructor
			requiredSize = sizeof(void*);
			break;
		case String:
			requiredSize = sizeof(std::string);
			a.push(eax);
			a.push(ecx);
			AbstractSyntaxTree::stackOffset += 2 * sizeof(void*);
			compiler_assert(variableInfo.second <= AbstractSyntaxTree::stackOffset, "string stack location out of bounds");
			a.lea(ecx, esp, ImmediateValue32(AbstractSyntaxTree::stackOffset - variableInfo.second));
#ifdef _M_X64
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringDestructorHelper)));
			a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
			a.call(eax);
			a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
			a.push(ecx);
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringDestructorHelper)));
			a.call(eax);
			a.pop();
#endif
			a.pop(ecx);
			a.pop(eax);
			AbstractSyntaxTree::stackOffset -= 2 * sizeof(void*);
		}
		totalSize += requiredSize;
		if (i < varInfos.size() - 1) {
			StackOffset thisLocation = variableInfo.second;
			StackOffset nextLocation = varInfos[i + 1].second;
			compiler_assert(thisLocation != 0 && nextLocation != 0, "return address should be at stack offset 0, not a variable");
			bool firstParameter = thisLocation * nextLocation < 0; // opposite signs means this is the first parameter.  The difference between the locations should also skip the return address pointer.
			compiler_assert(nextLocation == thisLocation - requiredSize - firstParameter * sizeof(void*), "stack variable locations don't line up");
		}
	}
	compiler_assert(AbstractSyntaxTree::parameterStackOffset <= 0, "parameter stack offset must be non-positive");
	if (scopeIndex == 0)
		totalSize += AbstractSyntaxTree::parameterStackOffset + sizeof(void*); // don't pop the parameter space on the stack in the top scope.  caller does that. This pointer means the return address pointer at stack location 0.
	a.add(esp, ImmediateValue32(totalSize));
	return totalSize;
}

void ASTReturn::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	if (dataType != None) {
		returnValue->compile(buffer);
		castIfNecessary(dataType, returnValue->dataType, buffer);
	} else
		compiler_assert(!returnValue, "return value None should not have a return value");

	switch (dataType) {
	default:
	case Undetermined:
		compiler_assert(0, "invalid return type");
		return;
	case None:
	case Int32:
	case Pointer:
	case Double:
		StackOffset stackOffsetToRemove = AbstractSyntaxTree::stackOffset;
		for (size_t i = AbstractSyntaxTree::scopes.size(); i > 0; i--)
			stackOffsetToRemove -= deallocateVariables(buffer, i - i);
		a.add(esp, ImmediateValue32(stackOffsetToRemove)); // pop string literals from stack
		a.ret();
		return;
	}
	compiler_assert(0, "return compile failed");
}

void ASTLiteral::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	switch (dataType){
	default:
	case None:
	case Undetermined:
		compiler_assert(0, "undetermined literal type");
		return;
	case Int32:
		a.mov(eax, ImmediateValue32(intValue));
		return;
	case Double:
#ifdef _M_X64
		a.push(ImmediateValue64(doubleValue));
		a.movsd(xmm0, esp, 0);
		a.pop();
		return;
#else
		a.push(ImmediateValue64(doubleValue));
		a.fld(esp, 0);
		a.pop64();
		return;
#endif
	case Pointer:
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(pointerValue)));
		return;
	case CharStar:
		compiler_assert(AbstractSyntaxTree::stringLiteralLocations.find(stringValue) != AbstractSyntaxTree::stringLiteralLocations.end(), "string literal not in possible string literals");
		a.lea(eax, esp, ImmediateValue32(AbstractSyntaxTree::stackOffset - AbstractSyntaxTree::stringLiteralLocations[stringValue]));
		return;
	}
	compiler_assert(0, "string literal compile failed");
}

void ASTBinaryOperation::compile(AssemblerBuffer& buffer) const
{
	leftOperand->compile(buffer);

	// move the left operand onto the c stack
	Assembler a(buffer);
	if (leftOperand->dataType == Int32
		|| leftOperand->dataType == String) {
		a.push(eax);
		AbstractSyntaxTree::stackOffset += sizeof(void*);
	} else {
		compiler_assert(leftOperand->dataType == Double, "binary operation left operand should be int, string, or double");
#ifdef _M_X64
		leftOperand->compile(buffer);
		a.push(xmm0);
#else
		a.sub(esp, ImmediateValue32(sizeof(double)));
		a.fstp(esp, 0);
#endif
		AbstractSyntaxTree::stackOffset += sizeof(double);
	}

	rightOperand->compile(buffer);

	if (leftOperand->dataType == Int32 && rightOperand->dataType == Int32) {
		dataType = Int32;
		AbstractSyntaxTree::stackOffset -= sizeof(void*);
		a.mov(ecx, eax); // right operand is now in ecx
		a.pop(eax); // left operand is now in eax

		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation type");
			return;
		case Add:
			a.add(eax, ecx);
			return;
		case Subtract:
			a.sub(eax, ecx);
			return;
		case Multiply:
			a.imul(eax, ecx);
			return;
		case Divide:
			a.cdq();
			a.idiv(ecx);
			return;
		case Mod:
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			a.cmp(eax, ecx);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			a.cmp(eax, ecx);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			a.cmp(eax, ecx);
			a.jmp(Condition::GreaterThan, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			a.cmp(eax, ecx);
			a.jmp(Condition::GreaterThanOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			a.cmp(eax, ecx);
			a.jmp(Condition::LessThan, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			a.cmp(eax, ecx);
			a.jmp(Condition::LessThanOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			a.and(eax, ecx);
			return;
		case LogicalOr:
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::NotEqual,
				Assembler::cmpOperationSize(ecx, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::NotEqual)
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always)); // skip to moving 1 into eax
			a.cmp(ecx, ImmediateValue32(0));
			a.jmp(Condition::NotEqual,
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always)); // skip to moving 1 into eax
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1))); // skip over moving 1 into eax
			a.mov(eax, ImmediateValue32(1));
			return;
		case LogicalAnd:
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::Equal, 
				Assembler::cmpOperationSize(ecx, ImmediateValue32(0)) 
				+ Assembler::jmpOperationSize(Condition::Equal) 
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always)); // skip to moving 0 into eax
			a.cmp(ecx, ImmediateValue32(0));
			a.jmp(Condition::Equal, 
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always)); // skip to moving 0 into eax
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0))); // skip over moving 0 into eax
			a.mov(eax, ImmediateValue32(0));
			return;
		}
	} else if (leftOperand->dataType == Double && rightOperand->dataType == Double) {
#ifdef _M_X64
		AbstractSyntaxTree::stackOffset -= sizeof(double);
		a.movsd(xmm1, xmm0); // the right operand is now in xmm1
		a.pop(xmm0); // the left operand is now in xmm0

		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation type");
			return;
		case Add:
			dataType = Double;
			a.addsd(xmm0, xmm1);
			return;
		case Subtract:
			dataType = Double;
			a.subsd(xmm0, xmm1);
			return;
		case Multiply:
			dataType = Double;
			a.mulsd(xmm0, xmm1);
			return;
		case Divide:
			dataType = Double;
			a.divsd(xmm0, xmm1);
			return;
		case Mod:
			dataType = Int32;
			// cast the doubles to ints and do the int mod operator.
			a.cvttsd2si(eax, xmm0);
			a.cvttsd2si(ecx, xmm1);
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Below, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::BelowOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Above, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::AboveOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			// cast the doubles to ints and do the int bit shift.
			dataType = Int32;
			a.cvttsd2si(eax, xmm0);
			a.cvttsd2si(ecx, xmm1);
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			// cast the doubles to ints and do the int bit shift.
			dataType = Int32;
			a.cvttsd2si(eax, xmm0);
			a.cvttsd2si(ecx, xmm1);
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			// cast the doubles to ints and do the int bitwise xor.
			dataType = Int32;
			a.cvttsd2si(eax, xmm0);
			a.cvttsd2si(ecx, xmm1);
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			// cast the doubles to ints and do the int bitwise or.
			dataType = Int32;
			a.cvttsd2si(eax, xmm0);
			a.cvttsd2si(ecx, xmm1);
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			// cast the doubles to ints and do the int bitwise and.
			dataType = Int32;
			a.cvttsd2si(eax, xmm0);
			a.cvttsd2si(ecx, xmm1);
			a.and(eax, ecx);
			return;
		case LogicalOr:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm2, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm2);
			a.jmp(Condition::NotEqual, 
				Assembler::comisdOperationSize() 
				+ Assembler::jmpOperationSize(Condition::NotEqual) 
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.comisd(xmm1, xmm2);
			a.jmp(Condition::NotEqual, 
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LogicalAnd:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm2, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm2);
			a.jmp(Condition::Equal, 
				Assembler::comisdOperationSize() 
				+ Assembler::jmpOperationSize(Condition::Equal) 
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.comisd(xmm1, xmm2);
			a.jmp(Condition::Equal, 
				Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0)));
			a.mov(eax, ImmediateValue32(0));
			return;
		}
#else
		AbstractSyntaxTree::stackOffset -= sizeof(double);
		a.fld(esp, 0); // move the left operand from the c stack to st0, pushing the right operand to st1
		a.pop64();

		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation type");
			return;
		case Add:
			dataType = Double;
			a.faddp();
			return;
		case Subtract:
			dataType = Double;
			a.fsubp();
			return;
		case Multiply:
			dataType = Double;
			a.fmulp();
			return;
		case Divide:
			dataType = Double;
			a.fdivp();
			return;
		case Mod:
			// cast the doubles to ints and do the int mod operator.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(2 * sizeof(double)));
			a.fstp(esp, sizeof(double));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.cvttsd2si(eax, esp, sizeof(double));
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			dataType = Int32;
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			dataType = Int32;
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			dataType = Int32;
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Above, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			dataType = Int32;
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::AboveOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			dataType = Int32;
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Below, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			dataType = Int32;
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::BelowOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			// cast the doubles to ints and do the int shift.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(2 * sizeof(double)));
			a.fstp(esp, sizeof(double));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.cvttsd2si(eax, esp, 8);
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			// cast the doubles to ints and do the int shift.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(2 * sizeof(double)));
			a.fstp(esp, sizeof(double));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.cvttsd2si(eax, esp, 8);
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			// cast the doubles to ints and do the int xor.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(2 * sizeof(double)));
			a.fstp(esp, sizeof(double));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.cvttsd2si(eax, esp, 8);
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			// cast the doubles to ints and do the int or.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(2 * sizeof(double)));
			a.fstp(esp, sizeof(double));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.cvttsd2si(eax, esp, 8);
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			// cast the doubles to ints and do the int and.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(2 * sizeof(double)));
			a.fstp(esp, sizeof(double));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.cvttsd2si(eax, esp, 8);
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			a.and(eax, ecx);
			return;
		case LogicalOr:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0); // put one operand on the stack for now
			a.fld(esp, sizeof(double)); // load 0.0 into the fpu
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::NotEqual, 
				Assembler::fldOperationSize(esp, 0)
				+ Assembler::fldOperationSize(esp, 8)
				+ Assembler::x87CompareAndPopDoublesOperationSize()
				+ Assembler::jmpOperationSize(Condition::NotEqual)
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.fld(esp, 0); // load 0.0 and the other operand into the fpu
			a.fld(esp, 8);
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::NotEqual, 
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			return;
		case LogicalAnd:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0); // put one operand on the stack for now
			a.fld(esp, sizeof(double)); // load 0.0 into the fpu
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Equal,
				Assembler::fldOperationSize(esp, 0)
				+ Assembler::fldOperationSize(esp, 8)
				+ Assembler::x87CompareAndPopDoublesOperationSize()
				+ Assembler::jmpOperationSize(Condition::Equal)
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.fld(esp, 0); // load 0.0 and the other operand into the fpu
			a.fld(esp, 8);
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Equal,
				Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0)));
			a.mov(eax, ImmediateValue32(0));
			a.add(esp, ImmediateValue32(2 * sizeof(double)));
			return;
		}
#endif
	} else if (leftOperand->dataType == Int32 && rightOperand->dataType == Double) {
		AbstractSyntaxTree::stackOffset -= sizeof(void*);
#ifdef _M_X64
		a.movsd(xmm1, xmm0); // the right operand is now in xmm1
		a.pop(eax);
		a.cvtsi2sd(xmm0, eax); // the double version of the left operand is now in xmm0
		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation");
			return;
		case Add:
			dataType = Double;
			a.addsd(xmm0, xmm1);
			return;
		case Subtract:
			dataType = Double;
			a.subsd(xmm0, xmm1);
			return;
		case Multiply:
			dataType = Double;
			a.mulsd(xmm0, xmm1);
			return;
		case Divide:
			dataType = Double;
			a.divsd(xmm0, xmm1);
			return;
		case Mod:
			// cast the double to an int and do the int mod operator.
			dataType = Int32;
			a.cvttsd2si(ecx, xmm1);
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Below, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::BelowOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Above, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::AboveOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.cvttsd2si(ecx, xmm1);
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.cvttsd2si(ecx, xmm1);
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			// cast the double to an int and do the int xor.
			dataType = Int32;
			a.cvttsd2si(ecx, xmm1);
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			// cast the double to an int and do the int or.
			dataType = Int32;
			a.cvttsd2si(ecx, xmm1);
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			// cast the double to an int and do the int and.
			dataType = Int32;
			a.cvttsd2si(ecx, xmm1);
			a.and(eax, ecx);
			return;
		case LogicalOr:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm0, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm1); // right operand is in xmm1
			a.jmp(Condition::NotEqual, 
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::NotEqual) 
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0)); // left operand is still in eax
			a.jmp(Condition::NotEqual, 
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LogicalAnd:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm0, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm1); // right operand is in xmm1
			a.jmp(Condition::Equal, 
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Equal) 
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0)); // left operand is still in eax
			a.jmp(Condition::Equal, 
				Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0)));
			a.mov(eax, ImmediateValue32(0));
			return;
		}
#else
		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation");
			return;
		case Add:
			dataType = Double;
			a.fild(esp, 0); // convert the left operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.pop();
			a.faddp();
			return;
		case Subtract:
			dataType = Double;
			a.fild(esp, 0); // convert the left operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.pop();
			a.fsubp();
			return;
		case Multiply:
			dataType = Double;
			a.fild(esp, 0); // convert the left operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.pop();
			a.fmulp();
			return;
		case Divide:
			dataType = Double;
			a.fild(esp, 0); // convert the left operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.pop();
			a.fdivp();
			return;
		case Mod:
			// cast the double to an int and do the int mod operator.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.pop64();
			a.pop(eax);
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			dataType = Int32;
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.pop();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			dataType = Int32;
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.pop();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			dataType = Int32;
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.pop();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Above, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			dataType = Int32;
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.pop();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::AboveOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			dataType = Int32;
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.pop();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Below, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			dataType = Int32;
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.pop();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::BelowOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.pop64();
			a.pop(eax);
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.pop64();
			a.pop(eax);
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			// cast the double to an int and do the int xor.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.pop64();
			a.pop(eax);
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			// cast the double to an int and do the int or.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.pop64();
			a.pop(eax);
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			// cast the double to an int and do the int and.
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(ecx, esp, 0);
			a.pop64();
			a.pop(eax);
			a.and(eax, ecx);
			return;
		case LogicalOr:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.fld(esp, 0);
			a.pop64();
			a.x87CompareAndPopDoubles(eax);
			a.pop(eax); // get the left operand from the stack after using eax for comparison
			a.jmp(Condition::NotEqual,
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::NotEqual)
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::NotEqual, 
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LogicalAnd:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.fld(esp, 0);
			a.pop64();
			a.x87CompareAndPopDoubles(eax);
			a.pop(eax); // get the left operand from the stack after using eax for comparison
			a.jmp(Condition::Equal,
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Equal)
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::Equal,
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0)));
			a.mov(eax, ImmediateValue32(0));
			return;
		}
#endif
	} else if (leftOperand->dataType == Double && rightOperand->dataType == Int32) {
		AbstractSyntaxTree::stackOffset -= sizeof(double);
#ifdef _M_X64
		a.pop(xmm0); // the left operand is now in xmm0
		a.cvtsi2sd(xmm1, eax); // the double version of the right operand is now in xmm1
		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation type");
			return;
		case Add:
			dataType = Double;
			a.addsd(xmm0, xmm1);
			return;
		case Subtract:
			dataType = Double;
			a.subsd(xmm0, xmm1);
			return;
		case Multiply:
			dataType = Double;
			a.mulsd(xmm0, xmm1);
			return;
		case Divide:
			dataType = Double;
			a.divsd(xmm0, xmm1);
			return;
		case Mod:
			// cast the double to an int and do the int mod operator.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, xmm0);
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Below, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::BelowOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::Above, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			dataType = Int32;
			a.comisd(xmm1, xmm0);
			a.jmp(Condition::AboveOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, xmm0);
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, xmm0);
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			// cast the double to an int and do the int xor.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, xmm0);
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			// cast the double to an int and do the int or.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, xmm0);
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			// cast the double to an int and do the int and.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, xmm0);
			a.and(eax, ecx);
			return;
		case LogicalOr:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm1, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm1); // left operand is in xmm0
			a.jmp(Condition::NotEqual, 
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::NotEqual) 
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0)); // right operand is still in eax
			a.jmp(Condition::NotEqual, 
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LogicalAnd:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm1, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm1); // left operand is in xmm0
			a.jmp(Condition::Equal, 
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Equal) 
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0)); // right operand is still in eax
			a.jmp(Condition::Equal, 
				Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0)));
			a.mov(eax, ImmediateValue32(0));
			return;
		}
#else
		a.push(eax); // put the right operand on the stack, too.  we often need to load it from the stack while converting to a double
		switch (operationType) {
		default:
		case Invalid:
			compiler_assert(0, "invalid binary operation type");
			return;
		case Add:
			dataType = Double;
			a.fild(esp, 0); // convert the right operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.fld(esp, 4); // load the left operand (which is already a double) from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.faddp();
			return;
		case Subtract:
			dataType = Double;
			a.fild(esp, 0); // convert the right operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.fld(esp, 4); // load the left operand (which is already a double) from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.fsubp();
			return;
		case Multiply:
			dataType = Double;
			a.fild(esp, 0); // convert the right operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.fld(esp, 4); // load the left operand (which is already a double) from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.fmulp();
			return;
		case Divide:
			dataType = Double;
			a.fild(esp, 0); // convert the right operand (which is an integer on the c stack) to a double and move it to the x87 stack registers 
			a.fld(esp, 4); // load the left operand (which is already a double) from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.fdivp();
			return;
		case Mod:
			// cast the double to an int and do the int mod operator.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, esp, 4);
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes).  The result is in eax.
			a.cdq();
			a.idiv(ecx);
			a.mov(eax, edx); // the remainder is in edx after an idiv
			return;
		case Equal:
			dataType = Int32;
			a.fld(esp, 4);
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case NotEqual:
			dataType = Int32;
			a.fld(esp, 4);
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::NotEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThan:
			dataType = Int32;
			a.fld(esp, 4);
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Below, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case GreaterThanOrEqual:
			dataType = Int32;
			a.fld(esp, 4);
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::BelowOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThan:
			dataType = Int32;
			a.fld(esp, 4);
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Above, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LessThanOrEqual:
			dataType = Int32;
			a.fld(esp, 4);
			a.fild(esp, 0); // move the operands from the c stack to the x87 stack registers
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes) from the c stack
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::AboveOrEqual, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case LeftBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, esp, 4);
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes).  The result is in eax.
			a.shl(eax, ecx);
			return;
		case RightBitShift:
			// cast the double to an int and do the int shift.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, esp, 4);
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes).  The result is in eax.
			a.sar(eax, ecx);
			return;
		case BitwiseXOr:
			// cast the double to an int and do the int xor.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, esp, 4);
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes).  The result is in eax.
			a.xor(eax, ecx);
			return;
		case BitwiseOr:
			// cast the double to an int and do the int or.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, esp, 4);
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes).  The result is in eax.
			a.or(eax, ecx);
			return;
		case BitwiseAnd:
			// cast the double to an int and do the int and.
			dataType = Int32;
			a.mov(ecx, eax);
			a.cvttsd2si(eax, esp, 4);
			a.add(esp, ImmediateValue32(4 + 8)); // This pops and discards the left operand (8 bytes) and the right operand (4 bytes).  The result is in eax.
			a.and(eax, ecx);
			return;
		case LogicalOr:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.fld(esp, 8 + 4);
			a.fld(esp, 0);
			a.pop64();
			a.x87CompareAndPopDoubles(eax);
			a.pop(eax); // get the right operand from the stack after using eax for comparison
			a.jmp(Condition::NotEqual,
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::NotEqual)
				+ Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::NotEqual, 
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			a.pop64();
			return;
		case LogicalAnd:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.fld(esp, 8 + 4);
			a.fld(esp, 0);
			a.pop64();
			a.x87CompareAndPopDoubles(eax);
			a.pop(eax); // get the right operand from the stack after using eax for comparison
			a.jmp(Condition::Equal,
				Assembler::cmpOperationSize(eax, ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Equal)
				+ Assembler::movOperationSize(ImmediateValue32(1))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::Equal,
				Assembler::movOperationSize(ImmediateValue32(0))
				+ Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(1));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(0)));
			a.mov(eax, ImmediateValue32(0));
			a.pop64();
			return;
		}
#endif 
	} else if (leftOperand->dataType == String && (rightOperand->dataType == Int32 || rightOperand->dataType == Double)) {
		castIfNecessary(Int32, rightOperand->dataType, buffer); // index is now in eax
		compiler_assert(operationType == Brackets, "string binary operation should be brackets");
		dataType = Int32;
		a.pop(ecx); // pointer to string is now in ecx
		AbstractSyntaxTree::stackOffset -= sizeof(void*);
#ifdef _M_X64
		a.mov(edx, eax); // second argument register (ecx is the first argument register)
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringBracketHelper)));
		a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
		a.call(eax);// this puts the char from the string in eax
		a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
		a.push(eax);// second argument on stack
		a.push(ecx);// first argument on stack
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringBracketHelper)));
		a.call(eax);// this puts the char from the string in eax
		a.pop64();// pop both arguments with one operation
#endif
		return;
	}
	compiler_assert(0, "unsupported binary operation");
}

void ASTUnaryOperation::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	operand->compile(buffer);
	if (operand->dataType == Int32) {
		dataType = Int32;
		switch (operationType) {
		default:
			compiler_assert(0, "invalid unary operation type");
			break;
		case Negate:
			a.mov(ecx, ImmediateValue32(-1));
			a.imul(eax, ecx);
			break;
		case LogicalNot:
			a.cmp(eax, ImmediateValue32(0));
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			break;
		case BitwiseNot:
			a.mov(ecx, ImmediateValue32(~0));
			a.xor(eax, ecx);
			break;
		}
		return;
	} else {
#ifdef _M_X64
		switch (operationType) {
		default:
			compiler_assert(0, "invalid unary operation type");
			break;
		case Negate:
			dataType = Double;
			a.push(ImmediateValue64(-1.0));
			a.movsd(xmm1, esp, 0);
			a.pop();
			a.mulsd(xmm0, xmm1);
			return;
		case LogicalNot:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.movsd(xmm1, esp, 0);
			a.pop();
			a.comisd(xmm0, xmm1);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case BitwiseNot:
			dataType = Int32;
			a.cvttsd2si(eax, xmm0);
			a.mov(ecx, ImmediateValue32(~0));
			a.xor(eax, ecx);
			return;
		}
#else
		switch (operationType) {
		default:
			compiler_assert(0, "invalid unary operation type");
			break;
		case Negate:
			dataType = Double;
			a.push(ImmediateValue64(-1.0));
			a.fld(esp, 0);
			a.pop64();
			a.fmulp();
			return;
		case LogicalNot:
			dataType = Int32;
			a.push(ImmediateValue64(0.0));
			a.fld(esp, 0);
			a.pop64();
			a.x87CompareAndPopDoubles(eax);
			a.jmp(Condition::Equal, Assembler::movOperationSize(ImmediateValue32(0)) + Assembler::jmpOperationSize(Condition::Always));
			a.mov(eax, ImmediateValue32(0));
			a.jmp(Condition::Always, Assembler::movOperationSize(ImmediateValue32(1)));
			a.mov(eax, ImmediateValue32(1));
			return;
		case BitwiseNot:
			dataType = Int32;
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(eax, esp, 0);
			a.pop64();
			a.mov(ecx, ImmediateValue32(0xFFFFFFFF));
			a.xor(eax, ecx);
			return;
		}
#endif
	}
	compiler_assert(0, "unary operation compile failed");
}

void ASTFunctionCall::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	StackOffset parameterSpace = 0;

#ifdef _M_X64
	parameterSpace = (AbstractSyntaxTree::stackOffset + sizeof(void*) + 8 * parameters.size()) % 16; // waste some space before parameters to keep 16-byte alignment
	if (parameterSpace) {
		a.sub(esp, ImmediateValue32(parameterSpace));
		AbstractSyntaxTree::stackOffset += parameterSpace;
	}
#endif

	//parameters are evaluated right to left
	for(size_t i = parameters.size(); i > 0; i--) {
		const std::unique_ptr<ASTNode>& parameter = parameters[i - 1];
		parameter->compile(buffer);
		switch (parameter->dataType) {
		default:
		case None:
		case Undetermined:
			compiler_assert(0, "invalid parameter type");
			break;
		case Int32:
		case Pointer:
			a.push(eax);
			parameterSpace += sizeof(void*);
			AbstractSyntaxTree::stackOffset += sizeof(void*);
			break;
		case Double:
			parameterSpace += sizeof(double);
			AbstractSyntaxTree::stackOffset += sizeof(double);
			a.sub(esp, ImmediateValue32(sizeof(double)));
#ifdef _M_X64
			a.movsd(esp, 0, xmm0);
#else
			a.fstp(esp, 0);
#endif
			break;
		}
	}
#ifdef _M_X64
	// http://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
	if (parameters.size() > 0) {
		switch (parameters[0]->dataType) {
		default:
		case None:
		case Undetermined:
			compiler_assert(0, "invalid parameter type");
			break;
		case Int32:
		case Pointer:
			a.pop(ecx);
			parameterSpace -= sizeof(void*);
			AbstractSyntaxTree::stackOffset -= sizeof(void*);
			break;
		case Double:
			a.pop(xmm0);
			parameterSpace -= sizeof(double);
			AbstractSyntaxTree::stackOffset -= sizeof(double);
			break;
		}
	}
	if (parameters.size() > 1) {
		switch (parameters[1]->dataType) {
		default:
		case None:
		case Undetermined:
			compiler_assert(0, "invalid parameter type");
			break;
		case Int32:
		case Pointer:
			a.pop(edx);
			parameterSpace -= sizeof(void*);
			AbstractSyntaxTree::stackOffset -= sizeof(void*);
			break;
		case Double:
			a.pop(xmm1);
			parameterSpace -= sizeof(double);
			AbstractSyntaxTree::stackOffset -= sizeof(double);
			break;
		}
	}
	if (parameters.size() > 2) {
		switch (parameters[2]->dataType) {
		default:
		case None:
		case Undetermined:
			compiler_assert(0, "invalid parameter type");
			break;
		case Int32:
		case Pointer:
			a.pop(r8);
			parameterSpace -= sizeof(void*);
			AbstractSyntaxTree::stackOffset -= sizeof(void*);
			break;
		case Double:
			a.pop(xmm2);
			parameterSpace -= sizeof(double);
			AbstractSyntaxTree::stackOffset -= sizeof(double);
			break;
		}
	}
	if (parameters.size() > 3) {
		switch (parameters[3]->dataType) {
		default:
		case None:
		case Undetermined:
			compiler_assert(0, "invalid parameter type");
			break;
		case Int32:
		case Pointer:
			a.pop(r9);
			parameterSpace -= sizeof(void*);
			AbstractSyntaxTree::stackOffset -= sizeof(void*);
			break;
		case Double:
			a.pop(xmm3);
			parameterSpace -= sizeof(double);
			AbstractSyntaxTree::stackOffset -= sizeof(double);
			break;
		}
	}
	a.sub(esp, ImmediateValue32(32));
	parameterSpace += 32; // shadow space http://msdn.microsoft.com/en-us/library/zthk2dkh.aspx
	AbstractSyntaxTree::stackOffset += 32;
#endif
	a.mov(eax, ImmediateValuePtr(reinterpret_cast<uintptr_t>(functionAddress)));
	a.call(eax);
	a.add(esp, ImmediateValue32(parameterSpace));
	AbstractSyntaxTree::stackOffset -= parameterSpace;
}

void ASTIfElse::compile(AssemblerBuffer& buffer) const
{
	condition->compile(buffer);
	Assembler a(buffer);
	switch (condition->dataType) {
	case Undetermined:
	default:
		compiler_assert(0, "invalid condition type");
		break;
	case Int32:
	case Pointer:
		a.cmp(eax, ImmediateValue32(0));
		break;
	case Double:
		a.push(ImmediateValue64(0.0));
#ifdef _M_X64
		a.movsd(xmm1, esp, 0);
		a.comisd(xmm1, xmm0);
#else
		a.fld(esp, 0);
		a.x87CompareAndPopDoubles(eax);
#endif
		a.pop64();
		break;
	}
	Assembler::JumpDistanceLocation firstJumpDistanceLocation = a.jmp(Equal, 0); // 0 for now because we don't know the size of the if code yet
	uint32_t sizeBeforeIf = buffer.size();

	AbstractSyntaxTree::incrementScope(this);
	for (const std::unique_ptr<ASTNode>& statement : ifBody) {
		statement->compile(buffer);
#ifndef _M_X64
		if (statement->dataType == Double && statement->nodeType() != Return)
			a.x87Pop();
#endif
	}
	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer);

	uint32_t secondJumpDistanceLocation = a.jmp(Always, 0); // 0 for now because we don't know the size of the else code yet
	uint32_t sizeBeforeElse = buffer.size();

	AbstractSyntaxTree::incrementScope(this);
	for (const std::unique_ptr<ASTNode>& statement : elseBody) {
		statement->compile(buffer);
#ifndef _M_X64
		if (statement->dataType == Double && statement->nodeType() != Return)
			a.x87Pop();
#endif
	}
	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer);

	uint32_t sizeAfterElse = buffer.size();
	a.setJumpDistance(firstJumpDistanceLocation, sizeBeforeElse - sizeBeforeIf);
	a.setJumpDistance(secondJumpDistanceLocation, sizeAfterElse - sizeBeforeElse);
}

void ASTBreak::compile(AssemblerBuffer& buffer) const
{
	// climb up scopeParents to deallocate variables and tell my parent to set my jump distance
	for (size_t i = AbstractSyntaxTree::scopeParents.size(); i > 1; i--) { // we can stop 1 before the top scope (which is never a switch or loop)
		deallocateVariables(buffer, i - 1); // deallocate only the variables allocated so far
		const ASTNode* scopeParent = AbstractSyntaxTree::scopeParents[i - 1];
		compiler_assert(scopeParent, "no scope parent");
		ASTNodeType nodeType = scopeParent->nodeType();
		if (nodeType == Switch) {
			static_cast<const ASTSwitch*>(scopeParent)->breaks.push_back(this);
			break;
		} else if (nodeType == ForLoop) {
			static_cast<const ASTForLoop*>(scopeParent)->breaks.push_back(this);
			break;
		} else if (nodeType == WhileLoop) {
			static_cast<const ASTWhileLoop*>(scopeParent)->breaks.push_back(this);
			break;
		} else
			compiler_assert(nodeType == IfElse || nodeType == Scope, "invalid scope parent type");
	}

	Assembler a(buffer);
	jumpDistanceLocation = a.jmp(Condition::Always, 0); // distance set later by ASTForLoop, ASTWhileLoop, or ASTSwitch
	jumpFromLocation = buffer.size(); // used later by ASTForLoop, ASTWhileLoop, or ASTSwitch for calculating the jump distance
}

void ASTContinue::compile(AssemblerBuffer& buffer) const
{
	// climb up scopeParents to deallocate variables and tell my parent to set my jump distance
	for (size_t i = AbstractSyntaxTree::scopeParents.size(); i > 1; i--) { // we can stop 1 before the top scope (which is never a loop)
		deallocateVariables(buffer, i - 1); // deallocate only the variables allocated so far
		const ASTNode* scopeParent = AbstractSyntaxTree::scopeParents[i - 1];
		compiler_assert(scopeParent, "no scope parent");
		ASTNodeType nodeType = scopeParent->nodeType();
		if (nodeType == ForLoop) {
			static_cast<const ASTForLoop*>(scopeParent)->continues.push_back(this);
			break;
		}
		else if (nodeType == WhileLoop) {
			static_cast<const ASTWhileLoop*>(scopeParent)->continues.push_back(this);
			break;
		}
		else
			compiler_assert(nodeType == IfElse || nodeType == Scope || nodeType == Switch, "invalid scope parent type");
	}

	Assembler a(buffer);
	jumpDistanceLocation = a.jmp(Condition::Always, 0); // distance set later by ASTForLoop or ASTWhileLoop
	jumpFromLocation = buffer.size(); // used later by ASTForLoop or ASTWhileLoop for calculating the jump distance
}

void ASTCase::compile(AssemblerBuffer& buffer) const
{
	beginLocation = buffer.size(); // this is used later by the switch statement

	// climb up scopeParents to tell my parent to compare with me and jump to me
	for (size_t i = AbstractSyntaxTree::scopeParents.size(); i > 1; i--) { // we can stop 1 before the top scope (which is never a switch or loop)
		const ASTNode* scopeParent = AbstractSyntaxTree::scopeParents[i - 1];
		compiler_assert(scopeParent, "no scope parent");
		switch (scopeParent->nodeType()) {
		case Switch:
			static_cast<const ASTSwitch*>(scopeParent)->cases.push_back(this);
			return; // don't keep climbing the tree once we've found a switch
		case ForLoop:
		case WhileLoop:
		case IfElse:
		case Scope:
			break;
		default:
			compiler_assert(0, "invalid scope parent type");
		}
	}
}
	
void ASTDefault::compile(AssemblerBuffer& buffer) const
{
	beginLocation = buffer.size(); // this is used later by the switch statement

	// climb up scopeParents to tell my parent to and jump to me
	for (size_t i = AbstractSyntaxTree::scopeParents.size(); i > 1; i--) { // we can stop 1 before the top scope (which is never a switch)
		const ASTNode* scopeParent = AbstractSyntaxTree::scopeParents[i - 1];
		compiler_assert(scopeParent, "no scope parent");
		switch (scopeParent->nodeType()) {
		case Switch:
			compiler_assert(!static_cast<const ASTSwitch*>(scopeParent)->default, "multiple defaults in switch");
			static_cast<const ASTSwitch*>(scopeParent)->default = this;
			return; // don't keep climbing the tree once we've found a switch
		case ForLoop:
		case WhileLoop:
		case IfElse:
		case Scope:
			break; // defaults don't connect to loops.  Only switches.
		default:
			compiler_assert(0, "invalid scope parent type");
		}
	}
}

// helpers for doing unsigned pointer sized casting
static size_t castDoubleToPointerHelper(double d) { return static_cast<size_t>(d); }
static double castPointerToDoubleHelper(size_t s) { return static_cast<double>(s); }
#ifdef _M_X64
static size_t castInt32ToPointerHelper(int d) { return static_cast<size_t>(d); } // This wouldn't be too hard to do these with assembly, but be careful with sign extending.
#endif

void ASTNode::castIfNecessary(DataType to, DataType from, AssemblerBuffer& buffer)
{
	Assembler a(buffer);
	switch (from) {
	default:
	case Undetermined:
		compiler_assert(0, "invalid cast type");
		return;
	case Int32: // casting from a signed 32-bit integer
		if (to == Int32)
			return; // already correct type
		else if (to == Pointer) {
#ifdef _M_X64
			a.mov(ecx, eax);
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(castInt32ToPointerHelper)));
			a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
			a.call(eax); // this puts the int32 equivalent of the uint64 in eax
			a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
			// casting int32 to uint32 doesn't change any bits
#endif
			return;
		} else {
			compiler_assert(to == Double, "invalid cast type");
#ifdef _M_X64
			a.cvtsi2sd(xmm0, eax); // valueToCast is in eax
			return;
#else
			a.push(eax); // put the valueToCast from eax onto the stack
			a.fild(esp, 0); // convert the value on the stack to a double
			a.pop();
			return;
#endif
		}
	case Pointer: // casting from an unsigned 32- or 64-bit integer
		if (to == Pointer)
			return; // already correct type
		else if (to == Int32) {
#ifdef _M_X64
			a.mov(ecx, ImmediateValue64(static_cast<uint64_t>(0x00000000FFFFFFFF)));
			a.and(eax, ecx); // clean out garbage bits
#else
			// casting uint32 to int32 doesn't change any bits
#endif
			return;
		}
		else {
			compiler_assert(to == Double, "invalid cast type");
#ifdef _M_X64
			a.mov(ecx, eax); // the pointer was in eax.  We want it in ecx (the first argument register)
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(castPointerToDoubleHelper)));
			a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
			a.call(eax); // this puts the double equivalent of the uint64 in xmm0
			a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
			return;
#else
			a.push(eax);
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(castPointerToDoubleHelper)));
			a.call(eax); // this puts the double equivalent of the uint32 in st0
			a.pop();
			return;
#endif
		}
		return;
	case Double:
		if (to == Double)
			return; // already correct type
		else if (to == Int32) {
#ifdef _M_X64
			a.cvttsd2si(eax, xmm0);
#else
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.cvttsd2si(eax, esp, 0);
			a.pop64();
#endif
			return;
		} else {
			compiler_assert(to == Pointer, "invalid cast type");
#ifdef _M_X64
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(castDoubleToPointerHelper)));
			a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
			a.call(eax); // this puts the uint64 equivalent of the double in eax
			a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
			// The double is in st0.  We want it on the stack to do a function call.
			a.sub(esp, ImmediateValue32(sizeof(double)));
			a.fstp(esp, 0);
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(castDoubleToPointerHelper)));
			a.call(eax); // this puts the uint32 equivalent of the uint32 eax
			a.pop64();
#endif
			return;
		}
	case CharStar:
		compiler_assert(to == CharStar, "invalid cast type");
		return;
	case String:
		compiler_assert(to == CharStar, "invalid cast type");
#ifdef _M_X64
		a.mov(ecx, eax); // move the address of the string to the first argument register
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringCStrHelper)));
		a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
		a.call(eax);
		a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
		a.push(eax);
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringCStrHelper)));
		a.call(eax);
		a.pop();
#endif
		return;
	}
	compiler_assert(0, "cast compile failed"); // should not be reached
}

void ASTCast::compile(AssemblerBuffer& buffer) const
{
	valueToCast->compile(buffer);
	castIfNecessary(dataType, valueToCast->dataType, buffer);
}

static std::pair<DataType, StackOffset> findLocalVarInfo(const std::string& variableName)
{
	for (size_t i = AbstractSyntaxTree::scopes.size(); i > 0; i--) {
		const std::map<std::string, std::pair<DataType, StackOffset>>& scope = AbstractSyntaxTree::scopes[i - 1];
		if (scope.find(variableName) != scope.end())
			return scope.at(variableName);
	}
	compiler_assert(0, "variable not declared");
	return std::pair<DataType, StackOffset>(Undetermined, INT_MAX);
}

void ASTGetLocalVar::compile(AssemblerBuffer& buffer) const
{
	std::pair<DataType, StackOffset> info = findLocalVarInfo(name);
	StackOffset stackLocation = info.second;
	dataType = info.first;

	Assembler a(buffer);
	compiler_assert(stackLocation <= AbstractSyntaxTree::stackOffset, "stack location out of bounds");
	switch (dataType) {
	default:
	case Undetermined:
		compiler_assert(0, "invalid variable type");
		return;
	case Pointer:
		a.mov(eax, esp, AbstractSyntaxTree::stackOffset - stackLocation, is64Bit);
		return;
	case Int32:
		a.mov(eax, esp, AbstractSyntaxTree::stackOffset - stackLocation, false);
		return;
	case Double:
#ifdef _M_X64
		a.movsd(xmm0, esp, AbstractSyntaxTree::stackOffset - stackLocation);
#else
		a.fld(esp, AbstractSyntaxTree::stackOffset - stackLocation);
#endif
		return;
	case String:
		a.lea(eax, esp, AbstractSyntaxTree::stackOffset - stackLocation);
		return;
	}
	compiler_assert(0, "get variable compile failed");
}

void ASTDeclareLocalVar::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	uint32_t requiredSize = 0;
	compiler_assert(dataType == None, "Variable declaration should have dataType None.  Set type instead.  Declarations never return a value.");
	switch (type) {
	default:
	case Undetermined:
		compiler_assert(0, "invalid variable declaration type");
		break;
	case Pointer:
		requiredSize = sizeof(void*);
		break;
	case Int32:
		requiredSize = sizeof(int32_t);
		break;
	case Double:
		requiredSize = sizeof(double);
		break;
	case String:
		requiredSize = sizeof(std::string);
		break;
	}
	a.sub(esp, ImmediateValue32(requiredSize));
	AbstractSyntaxTree::stackOffset += requiredSize; // allocate space on the stack for this variable
	std::map<std::string, std::pair<DataType, StackOffset>>& topScope = AbstractSyntaxTree::scopes[AbstractSyntaxTree::scopes.size() - 1];
	compiler_assert(topScope.find(name) == topScope.end(), "duplicate variable name in scope");
	topScope[name] = std::pair<DataType, StackOffset>(type, AbstractSyntaxTree::stackOffset); // save info about this variable in the top scope

	if (initialValue) {
		initialValue->compile(buffer);
		switch (type) {
		default:
		case Undetermined:
			compiler_assert(0, "invalid initial value type");
			break;
		case Pointer:
			castIfNecessary(type, initialValue->dataType, buffer);
			a.mov(esp, 0, eax, is64Bit);
			break;
		case Int32:
			castIfNecessary(type, initialValue->dataType, buffer);
			a.mov(esp, 0, eax, false);
			break;
		case Double:
			castIfNecessary(type, initialValue->dataType, buffer);
#ifdef _M_X64
			a.movsd(esp, 0, xmm0);
#else
			a.fstp(esp, 0);
#endif
			break;
		case String:
			castIfNecessary(CharStar, initialValue->dataType, buffer);
#ifdef _M_X64
			a.mov(edx, eax); // move the char* to edx (the second argument register)
			a.mov(ecx, esp); // move the string address to ecx (the first argument register)
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringConstructorHelperCharStar)));
			a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
			a.call(eax);
			a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
			a.lea(ecx, esp, 0);
			a.push(eax);
			a.push(ecx);
			a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringConstructorHelperCharStar)));
			a.call(eax);
			a.pop64();
#endif
		}
	} else if (type == String) {
#ifdef _M_X64
		a.mov(ecx, esp); // move the string address to ecx (the first argument register)
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringConstructorHelper)));
		a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
		a.call(eax);
		a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
		a.push(esp);
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringConstructorHelper)));
		a.call(eax);
		a.pop();
#endif
	}
}

void ASTSetLocalVar::compile(AssemblerBuffer& buffer) const
{
	std::pair<DataType, StackOffset> info = findLocalVarInfo(name);
	StackOffset stackLocation = info.second;
	dataType = info.first;

	Assembler a(buffer);
	valueToSet->compile(buffer);
	compiler_assert(stackLocation <= AbstractSyntaxTree::stackOffset, "set variable stack location out of range");
	switch (dataType) {
	default:
	case Undetermined:
		compiler_assert(0, "setting invalid variable type");
		return;
	case Pointer:
		castIfNecessary(dataType, valueToSet->dataType, buffer);
		a.mov(esp, AbstractSyntaxTree::stackOffset - stackLocation, eax, is64Bit);
		return;
	case Int32:
		castIfNecessary(dataType, valueToSet->dataType, buffer);
		a.mov(esp, AbstractSyntaxTree::stackOffset - stackLocation, eax, false);
		return;
	case Double:
		castIfNecessary(dataType, valueToSet->dataType, buffer);
#ifdef _M_X64
		a.movsd(esp, AbstractSyntaxTree::stackOffset - stackLocation, xmm0);
#else
		a.fstp(esp, AbstractSyntaxTree::stackOffset - stackLocation);
		a.fld(esp, AbstractSyntaxTree::stackOffset - stackLocation);
#endif
		return;
	case String:
		castIfNecessary(CharStar, valueToSet->dataType, buffer);
#ifdef _M_X64
		a.mov(edx, eax); // move the char* to edx (the second argument register)
		a.lea(ecx, esp, AbstractSyntaxTree::stackOffset - stackLocation);
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringAssignmentHelper)));
		a.sub(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));// shadow space and alignment
		a.call(eax); // puts the pointer to the string back in eax
		a.add(esp, ImmediateValue32(((AbstractSyntaxTree::stackOffset + 8) % 16) + 32));
#else
		a.lea(ecx, esp, AbstractSyntaxTree::stackOffset - stackLocation);
		a.push(eax);
		a.push(ecx);
		a.mov(eax, ImmediateValuePtr(reinterpret_cast<size_t>(stringAssignmentHelper)));
		a.call(eax); // puts the pointer to the string back in eax
		a.pop64();
#endif
		return;
	}
	compiler_assert(0, "set variable compile failed");
}

void ASTForLoop::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	AbstractSyntaxTree::incrementScope(this);// scope for anything declared in the initializer
	if (initializer)
		initializer->compile(buffer);
	uint32_t preConditionLocation = buffer.size();
	Assembler::JumpDistanceLocation conditionJumpLocation;
	if (condition) {
		condition->compile(buffer);
		switch (condition->dataType) {
		default:
		case Undetermined:
			compiler_assert(0, "invalid for loop condition type");
			break;
		case Int32:
		case Pointer:
			a.cmp(eax, ImmediateValue32(0));
			break;
		case Double:
			a.push(ImmediateValue64(0.0));
#ifdef _M_X64
			a.movsd(xmm1, esp, 0);
			a.comisd(xmm1, xmm0);
#else
			a.fld(esp, 0);
			a.x87CompareAndPopDoubles(eax);
#endif
			a.pop64();
			break;
		}
		conditionJumpLocation = a.jmp(Condition::Equal, 0); // We want to jump to after the while loop if the condition is equal to 0.  The jump distance is filled in after compiling body.
	}
	uint32_t postConditionLocation = buffer.size();

	// compile the body
	AbstractSyntaxTree::incrementScope(this);
	for (const std::unique_ptr<ASTNode>& statement : body) {
		statement->compile(buffer);
#ifndef _M_X64
		if (statement->dataType == Double && statement->nodeType() != Return)
			a.x87Pop();
#endif
	}
	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer); // call destructor on anything declared in the body
	uint32_t preIncrementerLocation = buffer.size();
	if (incrementer)
		incrementer->compile(buffer);
	Assembler::JumpDistanceLocation endJumpLocation = a.jmp(Condition::Always, 0);
	uint32_t endLocation = buffer.size();

	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer); // call destructor on anything declared in the initializer
	
	// set the jump distance from the end back to the condition
	compiler_assert(preConditionLocation < endLocation, "invalid jump distance");
	a.setJumpDistance(endJumpLocation, preConditionLocation - endLocation); // this should be a negative jump

	// set the jump distance from the condition (if it was equal to 0) to the end of the loop
	if (condition)
		a.setJumpDistance(conditionJumpLocation, endLocation - postConditionLocation);

	// set the continue jump distances
	for (const ASTContinue* c : continues) {
		compiler_assert(preIncrementerLocation >= c->jumpFromLocation, "invalid jump distance");
		a.setJumpDistance(c->jumpDistanceLocation, preIncrementerLocation - c->jumpFromLocation); // this should be a positive jump to the incrementer
	}

	// set the break jump distances
	for (const ASTBreak* b : breaks) {
		compiler_assert(endLocation > b->jumpFromLocation, "invalid jump distance");
		a.setJumpDistance(b->jumpDistanceLocation, endLocation - b->jumpFromLocation); // this should be a positive jump forward to the end 
	}
}

void ASTWhileLoop::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	AbstractSyntaxTree::incrementScope(this); // scope for anything declared in the condition
	uint32_t preConditionLocation = buffer.size();
	condition->compile(buffer);
	switch (condition->dataType) {
	default:
	case Undetermined:
		compiler_assert(0, "invalid while condition type"); // should not be reached
		break;
	case Int32:
	case Pointer:
		a.cmp(eax, ImmediateValue32(0));
		break;
	case Double:
		a.push(ImmediateValue64(0.0));
#ifdef _M_X64
		a.movsd(xmm1, esp, 0);
		a.comisd(xmm1, xmm0);
#else
		a.fld(esp, 0);
		a.x87CompareAndPopDoubles(eax);
#endif
		a.pop64();
		break;
	}
	Assembler::JumpDistanceLocation conditionJumpLocation = a.jmp(Condition::Equal, 0); // We want to jump to after the while loop if the condition is equal to 0.  The jump distance is filled in after compiling body.
	uint32_t postConditionLocation = buffer.size();

	// compile the body
	AbstractSyntaxTree::incrementScope(this);
	for (const std::unique_ptr<ASTNode>& statement : body) {
		statement->compile(buffer);
#ifndef _M_X64
		if (statement->dataType == Double && statement->nodeType() != Return)
			a.x87Pop();
#endif
	}
	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer); // call destructor on anything declared in the body
	Assembler::JumpDistanceLocation endJumpLocation = a.jmp(Condition::Always, 0);
	uint32_t endLocation = buffer.size();

	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer); // call destructor on anything declared in the condition

	// set the jump distance from the end back to the condition
	a.setJumpDistance(endJumpLocation, preConditionLocation - endLocation); // this should be a negative jump

	// set the jump distance from the condition (if it was equal to 0) to the end of the loop
	a.setJumpDistance(conditionJumpLocation, endLocation - postConditionLocation);

	// set the continue jump distances
	for (const ASTContinue* c : continues)
		a.setJumpDistance(c->jumpDistanceLocation, preConditionLocation - c->jumpFromLocation); // this should be a negative jump back to the condition

	// set the break jump distances
	for (const ASTBreak* b : breaks)
		a.setJumpDistance(b->jumpDistanceLocation, endLocation - b->jumpFromLocation);
}

void ASTScope::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	AbstractSyntaxTree::incrementScope(this);
	for (const std::unique_ptr<ASTNode>& statement : body) {
		statement->compile(buffer);
#ifndef _M_X64
		if (statement->dataType == Double && statement->nodeType() != Return)
			a.x87Pop();
#endif
	}
	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer);
}

void ASTSwitch::compile(AssemblerBuffer& buffer) const
{
	Assembler a(buffer);
	std::vector<Assembler::JumpDistanceLocation> jumpDistanceLocations;
	std::vector<uint32_t> jumpFromLocations;

	AbstractSyntaxTree::incrementScope(this); // scope for anything declared in the valueToCompare

	// Compile the valueToCompare and cast it to an int, then jump with this value in eax
	valueToCompare->compile(buffer);
	castIfNecessary(Int32, valueToCompare->dataType, buffer);

	// Jump over the body to where the case comparisons will be compiled later.
	// We haven't found the cases yet so we don't know how many of them there will be.  Set the jump distance later, too.
	Assembler::JumpDistanceLocation preBodyJumpLocation = a.jmp(Condition::Always, 0); 
	uint32_t preBodyJumpFrom = buffer.size();

	// compile the body
	for (const std::unique_ptr<ASTNode>& statement : body) {
		compiler_assert(statement->nodeType() != DeclareLocalVar, "No local variable declaration allowed in switch statements.  Use additional scope.");
		statement->compile(buffer);
#ifndef _M_X64
		if (statement->dataType == Double && statement->nodeType() != Return)
			a.x87Pop();
#endif
	}
	uint32_t endLocation = buffer.size();

	AbstractSyntaxTree::deallocateVariablesAndDecrementScope(buffer); // call destructor on anything declared in the valueToCompare

	// Once we're done executing the body of the switch statement, we'll want to jump over the comparisons, which we executed first but needed to compile last.
	Assembler::JumpDistanceLocation preComparisonJumpLocation = a.jmp(Condition::Always, 0);
	uint32_t preComparisonJumpFrom = buffer.size();

	// Compile the comparisons down here now that we know how many cases we have and if we have a default.
	for (size_t i = 0; i < cases.size(); i++) {
		a.cmp(eax, ImmediateValue32(cases[i]->compareValue));
		jumpDistanceLocations.push_back(a.jmp(Condition::Equal, 0)); // jump 0 for now and we'll fill in the distance after compiling the body
		jumpFromLocations.push_back(buffer.size());
	}
	Assembler::JumpDistanceLocation defaultJumpDistanceLocation;
	uint32_t defaultJumpFrom;
	if (default) {
		defaultJumpDistanceLocation = a.jmp(Condition::Always, 0); // again, we'll fill in this distance after compiling the body
		defaultJumpFrom = buffer.size();
	}
	uint32_t postComparisonJumpTo = buffer.size();

	// jump over the comparisons after executing the body of the switch statement
	a.setJumpDistance(preComparisonJumpLocation, postComparisonJumpTo - preComparisonJumpFrom);

	// jump to the comparisons after executing valueToCompare
	a.setJumpDistance(preBodyJumpLocation, preComparisonJumpFrom - preBodyJumpFrom);

	// fill in the case jump distances
	for (size_t i = 0; i < cases.size(); i++)
		a.setJumpDistance(jumpDistanceLocations[i], cases[i]->beginLocation - jumpFromLocations[i]);

	// fill in the default jump distance to the default if there is one, otherwise we're already at the end so no jump is necessary or compiled
	if (default)
		a.setJumpDistance(defaultJumpDistanceLocation, default->beginLocation - defaultJumpFrom);

	// fill in the break jump distances
	for (const ASTBreak* b : breaks)
		a.setJumpDistance(b->jumpDistanceLocation, endLocation - b->jumpFromLocation);
}

void AbstractSyntaxTree::incrementScope(const ASTNode* scopeParent)
{
	AbstractSyntaxTree::scopeParents.push_back(scopeParent);
	scopes.push_back(std::map<std::string, std::pair<DataType, StackOffset>>());
}

void AbstractSyntaxTree::deallocateVariablesAndDecrementScope(AssemblerBuffer& buffer)
{
	compiler_assert(scopes.size(), "no scopes");
	AbstractSyntaxTree::stackOffset -= deallocateVariables(buffer, scopes.size() - 1);
	scopes.pop_back();
	AbstractSyntaxTree::scopeParents.pop_back();
}

} // namespace Compiler
