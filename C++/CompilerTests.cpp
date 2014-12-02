#include "AbstractSyntaxTree.h"

#ifdef NDEBUG
#undef assert
#define assert(x) if(!(x)) *((int*)nullptr) = 0;
#endif

// helpers for testing function calling

namespace Compiler {

static uint32_t __cdecl doStuff32(uint32_t x, uint32_t y, uint32_t z) { return x * (y + 1) + z; }

#ifdef _M_X64
static uint64_t doStuff64(uint64_t x, uint64_t y, uint64_t z) { return x - y + z; }
#endif

static double intParameters(int x, int y, int z, int a, int b, int c)
{
	assert(x == 1);
	assert(y == 2);
	assert(z == 3);
	assert(a == 4);
	assert(b == 5);
	assert(c == 6);
	return 8.8;
}

static int doubleParameters(double x, double y, double z, double a, double b, double c)
{
	assert(x == 1.1);
	assert(y == 2.2);
	assert(z == 3.3);
	assert(a == 4.4);
	assert(b == 5.5);
	assert(c == 6.6);
	return 8;
}

static void mixedParameters(double x, int y, double z, int a, double b, int c)
{
	assert(x == 1.1);
	assert(y == 2);
	assert(z == 3.3);
	assert(a == 4);
	assert(b == 5.5);
	assert(c == 6);
}

__declspec(noinline) static void fiveParameters(int x, int y, int z, int a, int b)
{
	int c; // check stack alignment
#ifdef _M_X64
	// http://msdn.microsoft.com/en-us/library/ms235286.aspx
	// for some reason, when the stack is aligned correctly to 16 bytes this address ends in 0X04 in debug mode and 00 in release mode
#ifdef NDEBUG
	assert((reinterpret_cast<size_t>(&c) % 16) == 0X00);
#else
	assert((reinterpret_cast<size_t>(&c) % 16) == 0X04);
#endif
#else
	// http://msdn.microsoft.com/en-us/library/aa290049.aspx
	assert(!(reinterpret_cast<size_t>(&c) % 4));
#endif
	assert(x == 1);
	assert(y == 2);
	assert(z == 3);
	assert(a == 4);
	assert(b == 5);
}

#ifndef _M_X64
static void checkX87Stack()
{
	// if there is something in the x87 stack that has not been freed and popped properly, one of these will become NAN when loading
	AssemblerBuffer buffer;
	Assembler a(buffer);
	a.push(ImmediateValue64(1.1));
	a.push(ImmediateValue64(2.1));
	a.push(ImmediateValue64(3.1));
	a.push(ImmediateValue64(4.1));
	a.push(ImmediateValue64(5.1));
	a.push(ImmediateValue64(6.1));
	a.push(ImmediateValue64(7.1));
	a.push(ImmediateValue64(8.1));
	a.fld(esp, 0);
	a.fld(esp, 8);
	a.fld(esp, 16);
	a.fld(esp, 24);
	a.fld(esp, 32);
	a.fld(esp, 40);
	a.fld(esp, 48);
	a.fld(esp, 56);
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.add(esp, ImmediateValue32(64));
	a.ret();
	double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
	double d = function();
	assert(function() == 36.800000000000004);
}
static void clearX87Stack()
{
	// if there is something in the x87 stack that has not been freed and popped properly, one of these will become NAN when loading
	AssemblerBuffer buffer;
	Assembler a(buffer);
	a.push(ImmediateValue64(0.0));
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.fld(esp, 0);
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.faddp();
	a.pop64();
	a.ret();
	double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
	double d = function();
	assert(function() == 0);
}
#endif

void AbstractSyntaxTree::runASTUnitTests()
{
	AssemblerBuffer buffer;
	{ // return values
		// return 7;
		buffer.clear();
		ASTReturn* returnValue = new ASTReturn();
		ASTLiteral* constant = new ASTLiteral();
		constant->dataType = Int32;
		constant->intValue = 7;
		returnValue->dataType = Int32;
		returnValue->returnValue = std::unique_ptr<ASTNode>(constant);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(returnValue));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 7);

		// return -7;
		buffer.clear();
		constant->intValue = -7;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == -7);

		// return -1.9;
		buffer.clear();
		constant->dataType = Double;
		returnValue->dataType = Double;
		constant->doubleValue = -1.9;
		tree.compile(buffer);
		double(*doubleFunction)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == -1.9);

		// return 2.3;
		buffer.clear();
		constant->doubleValue = 2.3;
		tree.compile(buffer);
		doubleFunction = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 2.3);
	}
	{ // function call
		// return intParameters(1, 2, 3, 4, 5, 6);
		buffer.clear();

		ASTReturn* ret = new ASTReturn();
		ASTFunctionCall* fun = new ASTFunctionCall();
		fun->dataType = ret->dataType = Double;
		ret->returnValue = std::unique_ptr<ASTNode>(fun);

		fun->functionAddress = intParameters;
		fun->parameters.push_back(std::make_unique<ASTLiteral>(1));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(2));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(3));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(4));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(5));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(6));
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		double retValue = function();
		assert(function() == 8.8);

		// return doubleParameters(1.1, 2.2, 3.3, 4.4, 5.5, 6.6);
		buffer.clear();
		fun->functionAddress = doubleParameters;
		fun->parameters.pop_back();// deletes the literals
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.push_back(std::make_unique<ASTLiteral>(1.1));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(2.2));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(3.3));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(4.4));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(5.5));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(6.6));
		fun->dataType = ret->dataType = Int32;
		tree.compile(buffer);
		int(*intFunction)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		int intRetValue = intFunction();
		assert(intFunction() == 8);

		// mixedParameters(1.1, 2, 3.3, 4, 5.5, 6);
		buffer.clear();
		tree.statements.pop_back(); // deletes ret, fun, and the literals
		fun = new ASTFunctionCall();
		ret = new ASTReturn();
		fun->functionAddress = mixedParameters;
		fun->dataType = ret->dataType = None;
		tree.statements.push_back(std::unique_ptr<ASTNode>(fun));
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(1.1));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(2));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(3.3));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(4));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(5.5));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(6));
		tree.compile(buffer);
		void(*voidFunction)() = reinterpret_cast<void(*)()>(buffer.getExecutableAddress());
		voidFunction();

		// fiveParameters(1, 2, 3, 4, 5);
		fiveParameters(1, 2, 3, 4, 5);
		buffer.clear();
		fun->functionAddress = fiveParameters;
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.pop_back();
		fun->parameters.push_back(std::make_unique<ASTLiteral>(1));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(2));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(3));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(4));
		fun->parameters.push_back(std::make_unique<ASTLiteral>(5));
		tree.compile(buffer);
		voidFunction = reinterpret_cast<void(*)()>(buffer.getExecutableAddress());
		voidFunction();
	}
	{ // if statement
		// if(0) return 3; else return -3;
		buffer.clear();
		ASTReturn* return3 = new ASTReturn();
		ASTReturn* return_3 = new ASTReturn();
		ASTLiteral* conditionConstant = new ASTLiteral();
		return3->dataType = return_3->dataType = conditionConstant->dataType = Int32;
		conditionConstant->intValue = 0;
		return3->returnValue = std::make_unique<ASTLiteral>(3);
		return_3->returnValue = std::make_unique<ASTLiteral>(-3);
		ASTIfElse* ifElse = new ASTIfElse();
		ifElse->condition = std::unique_ptr<ASTNode>(conditionConstant);
		ifElse->ifBody.push_back(std::unique_ptr<ASTNode>(return3));
		ifElse->elseBody.push_back(std::unique_ptr<ASTNode>(return_3));
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ifElse));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == -3);

		// if(5) return 3; else return -3;
		buffer.clear();
		conditionConstant->intValue = 5;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 3);
	}
	{ // scope
		// int x = 5;
		// { int x = 6; }
		// return x;
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(5)));
		ASTScope* scope = new ASTScope();
		scope->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(6)));
		tree.statements.push_back(std::unique_ptr<ASTScope>(scope));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Int32));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 5);
	}
	{ // integer binary arithmetic operations
		// return 5 + (8 - 3);
		buffer.clear();
		ASTReturn* ret = new ASTReturn();
		ASTBinaryOperation* secondOperation = new ASTBinaryOperation();
		secondOperation->operationType = ASTBinaryOperation::Add;
		ASTBinaryOperation* firstOperation = new ASTBinaryOperation();
		firstOperation->operationType = ASTBinaryOperation::Subtract;
		ASTLiteral* constant5 = new ASTLiteral();
		ASTLiteral* constant8 = new ASTLiteral();
		ASTLiteral* constant3 = new ASTLiteral();
		ret->dataType = secondOperation->dataType = firstOperation->dataType = constant5->dataType = constant8->dataType = constant3->dataType = Int32;
		constant5->intValue = 5;
		constant8->intValue = 8;
		constant3->intValue = 3;
		ret->returnValue = std::unique_ptr<ASTNode>(secondOperation);
		secondOperation->leftOperand = std::unique_ptr<ASTNode>(constant5);
		secondOperation->rightOperand = std::unique_ptr<ASTNode>(firstOperation);
		firstOperation->leftOperand = std::unique_ptr<ASTNode>(constant8);
		firstOperation->rightOperand = std::unique_ptr<ASTNode>(constant3);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		int returnValue = function();
		assert(function() == 10);

		// return 5 * (8 - 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::Multiply;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 25);

		// return 5 * (8 / 3);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::Divide;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 10);

		// return 5 % (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::Mod;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 1);

		// return 5 | (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::BitwiseOr;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (5 | (8 / 3)));

		// return 5 & (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::BitwiseAnd;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (5 & (8 / 3)));

		// return 5 ^ (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::BitwiseXOr;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (5 ^ (8 / 3)));

		// return 5 << (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::LeftBitShift;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (5 << (8 / 3)));

		// return 5 >> (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::RightBitShift;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (5 >> (8 / 3)));

		// return 5 || (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (int)(5 || (8 / 3)));

		// return 5 && (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (int)(5 && (8 / 3)));

		// return 0 || (8 / 3);
		buffer.clear();
		constant5->intValue = 0;
		secondOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (int)(0 || (8 / 3)));

		// return 0 && (8 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (int)(0 && (8 / 3)));

		// return 5 || (0 / 3);
		buffer.clear();
		constant5->intValue = 5;
		constant8->intValue = 0;
		secondOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (int)(5 || (0 / 3)));

		// return 5 && (0 / 3);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == (5 && (0 / 3)));
	}
	{ // double binary arithmetic operations
		// return 5.5 + (8.3 - 2.2);
		buffer.clear();
		ASTReturn* ret = new ASTReturn();
		ASTBinaryOperation* secondOperation = new ASTBinaryOperation();
		secondOperation->operationType = ASTBinaryOperation::Add;
		ASTBinaryOperation* firstOperation = new ASTBinaryOperation();
		firstOperation->operationType = ASTBinaryOperation::Subtract;
		ASTLiteral* constant5 = new ASTLiteral();
		ASTLiteral* constant8 = new ASTLiteral();
		ASTLiteral* constant2 = new ASTLiteral();
		ret->dataType = secondOperation->dataType = firstOperation->dataType = constant5->dataType = constant8->dataType = constant2->dataType = Double;
		constant5->doubleValue = 5.5;
		constant8->doubleValue = 8.3;
		constant2->doubleValue = 2.2;
		ret->returnValue = std::unique_ptr<ASTNode>(secondOperation);
		secondOperation->leftOperand = std::unique_ptr<ASTNode>(constant5);
		secondOperation->rightOperand = std::unique_ptr<ASTNode>(firstOperation);
		firstOperation->leftOperand = std::unique_ptr<ASTNode>(constant8);
		firstOperation->rightOperand = std::unique_ptr<ASTNode>(constant2);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		double retValue = function();
		assert(function() == 11.600000000000001);

		// return 5.5 * (8.3 - 2.2);
		buffer.clear();
		secondOperation->operationType = ASTBinaryOperation::Multiply;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 33.550000000000004);

		// return 5.5 * (8.3 / 2.2);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::Divide;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 20.75);

		// return 5.5 * (8.3 % 3.2); // not valid in c, casts to ints then performs integer mod
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::Mod;
		firstOperation->dataType = Int32;
		constant2->doubleValue = 3.2;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 11.0);

		// return 5.5 * (8.3 << 3.2); // not valid in c, casts to ints and then performs integer shift
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LeftBitShift;
		firstOperation->dataType = Int32;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * 64);

		// return 5.5 * (8.3 >> 3.2); // not valid in c, casts to ints and then performs integer shift
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::RightBitShift;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * 1);

		// return 5.5 * (8.3 | 3.2); // not valid in c, casts to ints and then performs integer or
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::BitwiseOr;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8 | 3));

		// return 5.5 * (8.3 & 3.2); // not valid in c, casts to ints and then performs integer and
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::BitwiseAnd;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8 & 3));

		// return 5.5 * (8.3 ^ 3.2); // not valid in c, casts to ints and then performs integer xor
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::BitwiseXOr;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8 ^ 3));

		// return 5.5 * (8.3 || 3.2);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8.3 || 3.2));

		// return 5.5 * (8.3 && 3.2);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8.3 && 3.2));

		// return 5.5 * (0.0 || 3.2);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalOr;
		constant8->doubleValue = 0.0;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (0.0 || 3.2));

		// return 5.5 * (0.0 || 3.2);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (0.0 && 3.2));

		// return 5.5 * (0.0 || 0.0);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalOr;
		constant2->doubleValue = 0.0;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (0.0 || 0.0));

		// return 5.5 * (0.0 && 0.0);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (0.0 && 0.0));

		// return 5.5 * (8.3 || 0.0);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalOr;
		constant8->doubleValue = 8.3;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8.3 || 0.0));

		// return 5.5 * (8.3 || 0.0);
		buffer.clear();
		firstOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 5.5 * (8.3 && 0.0));
	}
	{ // mixed int/double binary arithmetic operations

		// Double first, Int32 second

		// return 5.6 + 8;
		buffer.clear();
		ASTReturn* ret = new ASTReturn();
		ASTBinaryOperation* binaryOperation = new ASTBinaryOperation();
		binaryOperation->operationType = ASTBinaryOperation::Add;
		ASTLiteral* constant5 = new ASTLiteral();
		ASTLiteral* constant8 = new ASTLiteral();
		ret->dataType = binaryOperation->dataType = constant5->dataType = constant8->dataType = Double;
		constant5->doubleValue = 5.6;
		constant8->dataType = Int32;
		constant8->intValue = 8;
		ret->returnValue = std::unique_ptr<ASTNode>(binaryOperation);
		binaryOperation->leftOperand = std::unique_ptr<ASTNode>(constant5);
		binaryOperation->rightOperand = std::unique_ptr<ASTNode>(constant8);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		double retValue = function();
		assert(function() == 13.6);

		// return 5.6 * 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Multiply;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 44.8);

		// return 5.6 / 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Divide;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0.7);

		// return 5.6 - 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Subtract;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == -2.4000000000000004);

		// return 5.6 % 8; // not valid in c, casts to int then performs integer mod and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Mod;
		ret->dataType = binaryOperation->dataType = Int32;
		tree.compile(buffer);
		int(*intFunction)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		int retValueInt = intFunction();
		assert(intFunction() == 5);

		// return 5.6 | 8; // not valid in c, casts to int then performs integer or and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::BitwiseOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 | 8));

		// return 5.6 & 8; // not valid in c, casts to int then performs integer and and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::BitwiseAnd;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 & 8));

		// return 5.6 ^ 8; // not valid in c, casts to int then performs integer xor and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::BitwiseXOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 ^ 8));

		// return 5.6 && 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5.6 && 8));

		// return 5.6 || 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5.6 || 8));

		// return 0.0 && 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		constant5->doubleValue = 0.0;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(0.0 && 8));

		// return 0.0 || 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(0.0 || 8));

		// return 0.0 && 0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		constant8->intValue = 0;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(0.0 && 0));

		// return 0.0 || 0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(0.0 || 0));

		// return 5.6 && 0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		constant5->doubleValue = 5.6;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5.6 && 0));

		// return 5.6 || 0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5.6 || 0));

		// Int32 first, Double second

		// return 5 - 8.3;
		buffer.clear();
		ret->dataType = binaryOperation->dataType = Double;
		binaryOperation->operationType = ASTBinaryOperation::Subtract;
		constant8->dataType = Double;
		constant5->dataType = Int32;
		constant8->doubleValue = 8.3;
		constant5->intValue = 5;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == -3.3000000000000007);

		// return 5 / 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Divide;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0.60240963855421681);

		// return 5 * 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Multiply;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 41.5);

		// return 5 + 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Add;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 13.3);

		// return 5 % 8.3; // not valid in c, casts to int then performs integer mod and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::Mod;
		ret->dataType = binaryOperation->dataType = Int32;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == 5);

		// return 5 | 8.3; // not valid in c, casts to int then performs integer or and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::BitwiseOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 | 8));

		// return 5 & 8.3; // not valid in c, casts to int then performs integer and and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::BitwiseAnd;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 & 8));

		// return 5 & 8.3; // not valid in c, casts to int then performs integer xor and returns an int
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::BitwiseXOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 ^ 8));

		// return 5 && 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5 && 8.3));

		// return 5 || 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5 || 8.3));

		// return 0 && 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		constant5->intValue = 0;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(0 && 8.3));

		// return 0 || 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(0 || 8.3));

		// return 0 && 0.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		constant8->doubleValue = 0.0;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (0 && 0.0));

		// return 0 || 0.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (0 || 0.0));

		// return 5 && 8.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalAnd;
		constant5->intValue = 5;
		ret->dataType = binaryOperation->dataType = Int32;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (5 && 0.0));

		// return 5 || 0.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LogicalOr;
		ret->dataType = binaryOperation->dataType = Int32;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValueInt = intFunction();
		assert(intFunction() == (int)(5 || 0.0));
	}
	{ // binary comparison operations

		// Int32/Int32 comparisons

		// return 5 == 8;
		buffer.clear();
		ASTReturn* ret = new ASTReturn();
		ASTBinaryOperation* binaryOperation = new ASTBinaryOperation();
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		ASTLiteral* left = new ASTLiteral();
		ASTLiteral* right = new ASTLiteral();
		ret->dataType = binaryOperation->dataType = left->dataType = right->dataType = Int32;
		left->intValue = 5;
		right->intValue = 8;
		ret->returnValue = std::unique_ptr<ASTNode>(binaryOperation);
		binaryOperation->leftOperand = std::unique_ptr<ASTNode>(left);
		binaryOperation->rightOperand = std::unique_ptr<ASTNode>(right);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		int retValue = function();
		assert(function() == 0);

		// return 5 != 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 < 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 <= 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 > 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 >= 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 == 5;
		buffer.clear();
		right->intValue = 5;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 != 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 < 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 <= 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 > 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 >= 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 == -3;
		buffer.clear();
		right->intValue = -3;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 != -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 < -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 <= -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 > -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 >= -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// Double/Double comparisons

		// return 5.5 == 8.8;
		buffer.clear();
		left->dataType = Double;
		right->dataType = Double;
		left->doubleValue = 5.5;
		right->doubleValue = 8.8;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 != 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 < 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 <= 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 > 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 >= 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 == 5.5;
		buffer.clear();
		right->doubleValue = 5.5;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 != 5.5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 < 5.5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 <= 5.5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 > 5.5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 >= 5.5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 == -3.3;
		buffer.clear();
		right->doubleValue = -3.3;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 != -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 < -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 <= -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 > -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 >= -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// Double/Int32 comparisons

		// return 5.5 == 8;
		buffer.clear();
		left->dataType = Double;
		right->dataType = Int32;
		left->doubleValue = 5.5;
		right->intValue = 8;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 != 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 < 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 <= 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.5 > 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 >= 8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.0 == 5;
		buffer.clear();
		left->doubleValue = 5.0;
		right->intValue = 5;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.0 != 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.0 < 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.0 <= 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5.0 > 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.0 >= 5;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return -1.5 == -3;
		buffer.clear();
		left->doubleValue = -1.5;
		right->intValue = -3;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return -1.5 != -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return -1.5 < -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return -1.5 <= -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return -1.5 > -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return -1.5 >= -3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// Int32/Double comparisons

		// return 5 == 8.8;
		buffer.clear();
		left->dataType = Int32;
		right->dataType = Double;
		left->intValue = 5;
		right->doubleValue = 8.8;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 != 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 < 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 <= 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 > 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5.5 >= 8.8;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 == 5.0;
		buffer.clear();
		right->doubleValue = 5.0;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 != 5.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 < 5.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 <= 5.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 > 5.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 >= 5.0;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 == -3.3;
		buffer.clear();
		right->doubleValue = -3.3;
		binaryOperation->operationType = ASTBinaryOperation::Equal;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 != -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::NotEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 < -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 <= -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::LessThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 0);

		// return 5 > -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThan;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);

		// return 5 >= -3.3;
		buffer.clear();
		binaryOperation->operationType = ASTBinaryOperation::GreaterThanOrEqual;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		retValue = function();
		assert(function() == 1);
	}
	{ // unary operations
		// return !0;
		buffer.clear();
		ASTReturn* ret = new ASTReturn();
		ASTUnaryOperation* operation = new ASTUnaryOperation();
		operation->operationType = ASTUnaryOperation::LogicalNot;
		ASTLiteral* operand = new ASTLiteral(0);
		ret->dataType = Int32;
		ret->returnValue = std::unique_ptr<ASTNode>(operation);
		operation->operand = std::unique_ptr<ASTNode>(operand);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 1);

		// return !5;
		buffer.clear();
		operand->intValue = 5;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 0);

		// return ~5;
		buffer.clear();
		operation->operationType = ASTUnaryOperation::BitwiseNot;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == ~5);

		// return -(5);
		buffer.clear();
		operation->operationType = ASTUnaryOperation::Negate;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == -5);

		// return -(5.5);
		buffer.clear();
		ret->dataType = operand->dataType = Double;
		operand->doubleValue = 5.5;
		tree.compile(buffer);
		double(*doubleFunction)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == -5.5);

		// return ~(5.5); // not valid c, but casts to int and does int bitwise not
		buffer.clear();
		operation->operationType = ASTUnaryOperation::BitwiseNot;
		ret->dataType = operation->dataType = Int32;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == ~5);

		// return !5.5;
		buffer.clear();
		operation->operationType = ASTUnaryOperation::LogicalNot;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 0);

		// return !0.0;
		buffer.clear();
		operation->operationType = ASTUnaryOperation::LogicalNot;
		operand->doubleValue = 0.0;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 1);
	}
	{ // casting
		// return (double)(int)(-1);
		buffer.clear();
		ASTReturn* ret = new ASTReturn();
		ASTCast* cast = new ASTCast();
		ASTLiteral* constant = new ASTLiteral(-1);
		ret->dataType = cast->dataType = Double;
		ret->returnValue = std::unique_ptr<ASTNode>(cast);
		cast->valueToCast = std::unique_ptr<ASTNode>(constant);
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::unique_ptr<ASTNode>(ret));
		tree.compile(buffer);
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(function() == -1.0);

		// return (double)(size_t)(-1);
		buffer.clear();
		constant->dataType = Pointer;
		constant->pointerValue = (void*)(size_t)-1;
		tree.compile(buffer);
		function = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(function() == (double)(size_t)(-1));

		// return (size_t)(double)(-1.0);
		buffer.clear();
		constant->dataType = Double;
		constant->doubleValue = -1.0;
		ret->dataType = cast->dataType = Pointer;
		tree.compile(buffer);
		size_t(*pointerFunction)() = reinterpret_cast<size_t(*)()>(buffer.getExecutableAddress());
		assert(pointerFunction() == (size_t)(double)(-1.0));

		// return (size_t)(int)(-1);
		buffer.clear();
		constant->dataType = Int32;
		constant->intValue = -1;
		tree.compile(buffer);
		pointerFunction = reinterpret_cast<size_t(*)()>(buffer.getExecutableAddress());
		assert(pointerFunction() == (size_t)(int)(-1));

		// return (int)(double)(-1.0);
		buffer.clear();
		ret->dataType = cast->dataType = Int32;
		tree.compile(buffer);
		int(*intFunction)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intFunction() == (int)(double)(-1.0));

		// return (int)(size_t)(-1);
		buffer.clear();
		constant->dataType = Pointer;
		constant->pointerValue = (void*)(size_t)-1;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intFunction() == (int)(size_t)(-1));

#ifdef _M_X64 // Be extra careful with the signs of 64-bit values.
		// return (int)(size_t)(0xFFFFFFFF00000001);
		buffer.clear();
		constant->dataType = Pointer;
		constant->pointerValue = (void*)(size_t)0xFFFFFFFF00000001;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intFunction() == (int)(size_t)(0xFFFFFFFF00000001));

		// return (int)(size_t)(0xFFFFFFFF80000001);
		buffer.clear();
		constant->pointerValue = (void*)(size_t)0xFFFFFFFF80000001;
		tree.compile(buffer);
		intFunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intFunction() == (int)(size_t)(0xFFFFFFFF80000001));

		// return (size_t)(int)(0x80000001)
		buffer.clear();
		ret->dataType = cast->dataType = Pointer;
		constant->dataType = Int32;
		constant->intValue = 0x80000001;
		tree.compile(buffer);
		pointerFunction = reinterpret_cast<size_t(*)()>(buffer.getExecutableAddress());
		size_t a = pointerFunction();
		assert(pointerFunction() == (size_t)(int)(0x80000001));

		// return (size_t)(int)(0x00000001)
		buffer.clear();
		constant->intValue = 0x00000001;
		tree.compile(buffer);
		pointerFunction = reinterpret_cast<size_t(*)()>(buffer.getExecutableAddress());
		size_t b = pointerFunction();
		assert(pointerFunction() == (size_t)(int)(0x00000001));
#endif
	}
	{ // getting and setting variables
		// int x = -5;
		// return x;
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(-5)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Int32));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == -5);

		// double x = -5.5;
		// return (int)x;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(-5.5)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Int32));
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == -5);

		// double x = -5.5;
		// return x;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(-5.5)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Double));
		tree.compile(buffer);
		double(*doubleFunction)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == -5.5);

		// double x = 5.5;
		// int y = x = 7.5;
		// return y;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(5.5)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>(7.5))));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("y"), Int32));
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 7);

		// int x = 5.5;
		// double y = x = 7.5;
		// return y;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(5.5)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "y", std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>(7.5))));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("y"), Double));
		tree.compile(buffer);
		doubleFunction = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 7.0);

		// double x = 5.5;
		// int y = 7;
		// double z = 6.7;
		// return x;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(5.5)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTLiteral>(7)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "z", std::make_unique<ASTLiteral>(6.7)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Double));
		tree.compile(buffer);
		doubleFunction = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 5.5);

		// double x = 5.5;
		// int y = 7;
		// double z = 6.7;
		// return y;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(5.5)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTLiteral>(7)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "z", std::make_unique<ASTLiteral>(6.7)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("y"), Int32));
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 7);

		// double x = 5.5;
		// int y = 7;
		// double z = 6.7;
		// return z;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(5.5)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTLiteral>(7)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "z", std::make_unique<ASTLiteral>(6.7)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("z"), Double));
		tree.compile(buffer);
		doubleFunction = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 6.7);
	}
	{ // for loop
		// double x = 0.77;
		// for(int y = 0; y<=10; y+=2)
		//     x = x + y;
		// return x;
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "x", std::make_unique<ASTLiteral>(0.77)));
		ASTForLoop* forLoop = new ASTForLoop(
			std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTLiteral>(0)), // initializer
			std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::LessThanOrEqual, std::make_unique<ASTGetLocalVar>("y"), std::make_unique<ASTLiteral>(10)), // condition
			std::make_unique<ASTSetLocalVar>("y", std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Add, std::make_unique<ASTGetLocalVar>("y"), std::make_unique<ASTLiteral>(2)))); // incrementer
		forLoop->body.push_back(std::make_unique<ASTSetLocalVar>("x",
			std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Add,
			std::make_unique<ASTGetLocalVar>("x"),
			std::make_unique<ASTGetLocalVar>("y"))));
		tree.statements.push_back(std::unique_ptr<ASTForLoop>(forLoop));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Double));
		tree.compile(buffer);
		double(*doubleFunction)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 30.77);

		// double x = 0.77;
		// for(int y = 0; y<=10; y+=2) {
		//     x = x + y;
		//     int z; // test variable allocation
		//     if (y == 4)
		//         break;
		//     else
		//         continue;
		//     x = 0; // should never be reached
		//     int w; // test variable allocation
		// }
		// return x;
		buffer.clear();
		ASTIfElse* ifElse = new ASTIfElse(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Equal, std::make_unique<ASTGetLocalVar>("y"), std::make_unique<ASTLiteral>(4)));
		ifElse->ifBody.push_back(std::make_unique<ASTBreak>());
		ifElse->elseBody.push_back(std::make_unique<ASTContinue>());
		forLoop->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "z"));
		forLoop->body.push_back(std::unique_ptr<ASTIfElse>(ifElse));
		forLoop->body.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>(0.0)));
		forLoop->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "w"));
		tree.compile(buffer);
		doubleFunction = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 6.77);

		// double x = 0.77;
		// for(;;) {
		//    if (x > 1)
		//        break;
		//    x*=2;
		// }
		// return x;
		buffer.clear();
		forLoop->initializer = nullptr;
		forLoop->condition = nullptr;
		forLoop->incrementer = nullptr;
		forLoop->body.clear();
		forLoop->continues.clear();
		forLoop->breaks.clear();
		ifElse = new ASTIfElse(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::GreaterThan, std::make_unique<ASTGetLocalVar>("x"), std::make_unique<ASTLiteral>(1)));
		ifElse->ifBody.push_back(std::make_unique<ASTBreak>());
		forLoop->body.push_back(std::unique_ptr<ASTIfElse>(ifElse));
		forLoop->body.push_back(std::make_unique<ASTSetLocalVar>("x",
			std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Multiply,
			std::make_unique<ASTGetLocalVar>("x"),
			std::make_unique<ASTLiteral>(2))));
		tree.compile(buffer);
		doubleFunction = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(doubleFunction() == 1.54);
	}
	{ // while loop
		// int x = 5;
		// int y = 1;
		// while(x) {
		//     y *= x;
		//     x--;
		// }
		// return y;
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(5)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTLiteral>(1)));
		ASTWhileLoop* whileLoop = new ASTWhileLoop(std::make_unique<ASTGetLocalVar>("x"));
		whileLoop->body.push_back(std::make_unique<ASTSetLocalVar>("y",
			std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Multiply,
			std::make_unique<ASTGetLocalVar>("y"),
			std::make_unique<ASTGetLocalVar>("x"))));
		whileLoop->body.push_back(std::make_unique<ASTSetLocalVar>("x",
			std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Subtract,
			std::make_unique<ASTGetLocalVar>("x"),
			std::make_unique<ASTLiteral>(1))));
		tree.statements.push_back(std::unique_ptr<ASTWhileLoop>(whileLoop));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("y"), Int32));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 120);

		// int x = 5;
		// int y = 1;
		// while(x) {
		//     y *= x;
		//     x--;
		//     int z; // test variable allocation
		//     if (x == 3)
		//         break;
		//     else
		//         continue;
		//     y = 0; // should never be reached
		//     int w; // test variable allocation
		// }
		buffer.clear();
		ASTIfElse* ifElse = new ASTIfElse(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Equal, std::make_unique<ASTGetLocalVar>("x"), std::make_unique<ASTLiteral>(3)));
		ifElse->ifBody.push_back(std::make_unique<ASTBreak>());
		ifElse->elseBody.push_back(std::make_unique<ASTContinue>());
		whileLoop->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "z"));
		whileLoop->body.push_back(std::unique_ptr<ASTIfElse>(ifElse));
		whileLoop->body.push_back(std::make_unique<ASTSetLocalVar>("y", std::make_unique<ASTLiteral>(0)));
		whileLoop->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "w"));
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 20);
	}
	{ // switch statement
		// int x = 1;
		// switch(x) {
		// case 1:
		//     x = 17;
		//     break;
		// case 2:
		//     x = -5;
		// case 3:
		//     x++;
		//     break;
		// }
		// return x;
		buffer.clear();
		AbstractSyntaxTree tree;
		ASTLiteral* initialValue = new ASTLiteral(1);
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::unique_ptr<ASTLiteral>(initialValue)));
		ASTSwitch* s = new ASTSwitch(std::make_unique<ASTGetLocalVar>("x"));
		s->body.push_back(std::make_unique<ASTCase>(1));
		s->body.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>(17)));
		s->body.push_back(std::make_unique<ASTBreak>());
		s->body.push_back(std::make_unique<ASTCase>(2));
		s->body.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>(-5)));
		s->body.push_back(std::make_unique<ASTCase>(3));
		s->body.push_back(std::make_unique<ASTSetLocalVar>("x",
			std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Add,
			std::make_unique<ASTGetLocalVar>("x"),
			std::make_unique<ASTLiteral>(1))));
		s->body.push_back(std::make_unique<ASTBreak>());
		tree.statements.push_back(std::unique_ptr<ASTSwitch>(s));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Int32));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 17);

		// same, but start with int x = 2;
		buffer.clear();
		initialValue->intValue = 2;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == -4);

		// same, but start with "int x = 3;"
		buffer.clear();
		initialValue->intValue = 3;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 4);

		// same, but start with int x = 4;
		buffer.clear();
		initialValue->intValue = 4;
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 4);

		// add a "default: x = 29;" at the end with no break
		buffer.clear();
		s->body.push_back(std::make_unique<ASTDefault>());
		s->body.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>(29)));
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 29);
	}
	{ // scope
		// int x = 5;
		// { int x = 6; x = x; }
		// return x;
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(5)));
		ASTScope* scope = new ASTScope();
		scope->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(6)));
		scope->body.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTGetLocalVar>("x")));
		tree.statements.push_back(std::unique_ptr<ASTScope>(scope));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Int32));
		tree.compile(buffer);
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 5);

		// int x = 5;
		// { int y = 6; x = y; }
		// return x;
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "x", std::make_unique<ASTLiteral>(5)));
		scope = new ASTScope();
		scope->body.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "y", std::make_unique<ASTLiteral>(6)));
		scope->body.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTGetLocalVar>("y")));
		tree.statements.push_back(std::unique_ptr<ASTScope>(scope));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("x"), Int32));
		tree.compile(buffer);
		function = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(function() == 6);
	}
	{ // strings
		// string x;
		// return;
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "x"));
		tree.statements.push_back(std::make_unique<ASTReturn>(None));
		tree.compile(buffer);
		void(*function)() = reinterpret_cast<void(*)()>(buffer.getExecutableAddress());
		function();

		// string x = "abcde";
		// return x[3];
		buffer.clear();
		tree.statements.clear();
		tree.possibleStringLiterals.insert("abcde");
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "x", std::make_unique<ASTLiteral>("abcde")));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Brackets, std::make_unique<ASTGetLocalVar>("x"), std::make_unique<ASTLiteral>(3)), Int32));
		tree.compile(buffer);
		int(*intfunction)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intfunction() == 'd');

		// string x = "abcde";
		// return x[4.9];
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "x", std::make_unique<ASTLiteral>("abcde")));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Brackets, std::make_unique<ASTGetLocalVar>("x"), std::make_unique<ASTLiteral>(4.9)), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intfunction() == 'e');

		// string x = "abcde";
		// string y = "abcdefgh";
		// string z = "abcdefghijkl";
		// return y[8];
		buffer.clear();
		tree.statements.clear();
		tree.possibleStringLiterals.insert("abcdefgh");
		tree.possibleStringLiterals.insert("abcdefghijkl");
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "x", std::make_unique<ASTLiteral>("abcde")));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "y", std::make_unique<ASTLiteral>("abcdefgh")));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "z", std::make_unique<ASTLiteral>("abcdefghijkl")));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Brackets, std::make_unique<ASTGetLocalVar>("y"), std::make_unique<ASTLiteral>(8)), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intfunction() == '\0');

		// string x = "abcde";
		// string y;
		// y = "ABCDE";
		// x = y;
		// return x[1];
		buffer.clear();
		tree.statements.clear();
		tree.possibleStringLiterals.insert("ABCDE");
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "x", std::make_unique<ASTLiteral>("abcde")));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "y"));
		tree.statements.push_back(std::make_unique<ASTSetLocalVar>("y", std::make_unique<ASTLiteral>("ABCDE")));
		tree.statements.push_back(std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTGetLocalVar>("y")));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Brackets, std::make_unique<ASTGetLocalVar>("x"), std::make_unique<ASTLiteral>(1)), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intfunction() == 'B');

		// string x = "abcde";
		// string y = x = "ABCDE";
		// return y[1];
		buffer.clear();
		tree.statements.clear();
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "x", std::make_unique<ASTLiteral>("abcde")));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(String, "y", std::make_unique<ASTSetLocalVar>("x", std::make_unique<ASTLiteral>("ABCDE"))));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTBinaryOperation>(ASTBinaryOperation::Brackets, std::make_unique<ASTGetLocalVar>("x"), std::make_unique<ASTLiteral>(2)), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		assert(intfunction() == 'C');
	}
	{ // accessing parameters
		// double function(int a, double b, int c, double d, int e, double f) { 
		// int g = 7;
		// double h = 8.8;
		// return a; 
		// }
		buffer.clear();
		AbstractSyntaxTree tree;
		tree.parameters.push_back(std::pair<DataType, std::string>(Int32, "a"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Double, "b"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Int32, "c"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Double, "d"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Int32, "e"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Double, "f"));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "g", std::make_unique<ASTLiteral>(7)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "h", std::make_unique<ASTLiteral>(8.8)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("a"), Double));
		tree.compile(buffer);
		double(*function)(int, double, int, double, int, double) = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 1.0);

		// same, but returning b
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("b"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 2.2);

		// same, but returning c
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("c"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 3.0);

		// same, but returning d
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("d"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 4.4);

		// same, but returning e
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("e"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 5.0);

		// same, but returning f
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("f"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 6.6);

		// same, but returning g
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("g"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 7.0);

		// same, but returning h
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("h"), Double));
		tree.compile(buffer);
		function = reinterpret_cast<double(*)(int, double, int, double, int, double)>(buffer.getExecutableAddress());
		assert(function(1, 2.2, 3, 4.4, 5, 6.6) == 8.8);

		// int function(double a, int b, double c, int d, double e, int f) { 
		// double g = 7.7;
		// int h = 8;
		// return a; 
		// }
		buffer.clear();
		tree.parameters.clear();
		tree.statements.clear();
		tree.parameters.push_back(std::pair<DataType, std::string>(Double, "a"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Int32, "b"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Double, "c"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Int32, "d"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Double, "e"));
		tree.parameters.push_back(std::pair<DataType, std::string>(Int32, "f"));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Double, "g", std::make_unique<ASTLiteral>(7.7)));
		tree.statements.push_back(std::make_unique<ASTDeclareLocalVar>(Int32, "h", std::make_unique<ASTLiteral>(8)));
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("a"), Int32));
		tree.compile(buffer);
		int(*intfunction)(double, int, double, int, double, int) = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 1);

		// same, but returning b
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("b"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 2);

		// same, but returning c
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("c"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 3);

		// same, but returning d
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("d"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 4);

		// same, but returning e
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("e"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 5);

		// same, but returning f
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("f"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 6);

		// same, but returning g
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("g"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 7);

		// same, but returning h
		buffer.clear();
		tree.statements.pop_back();
		tree.statements.push_back(std::make_unique<ASTReturn>(std::make_unique<ASTGetLocalVar>("h"), Int32));
		tree.compile(buffer);
		intfunction = reinterpret_cast<int(*)(double, int, double, int, double, int)>(buffer.getExecutableAddress());
		assert(intfunction(1.1, 2, 3.3, 4, 5.5, 6) == 8);
	}
#ifndef _M_X64
	checkX87Stack();
#endif
}

void Assembler::runAssemblerUnitTests()
{
	AssemblerBuffer buffer;
	Assembler assembler(buffer);
	{ // move and return
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(0x12345678));
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 0x12345678);
	}
	{ // push and pop registers
		buffer.clear();
		assembler.push(edi);
		assembler.mov(edi, esi);
		assembler.pop(edi);
		assembler.mov(eax, ImmediateValue32(0x12345678));
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 0x12345678);
	}
	{ // push and pop small immediate values
		buffer.clear();
		assembler.push(ImmediateValue32(127));
		assembler.pop(eax);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 127);
	}
	{ // push and pop large immediate values
		buffer.clear();
		assembler.push(ImmediateValue32(128));
		assembler.pop(eax);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 128);
	}
	{ // add registers
		buffer.clear();
		assembler.mov(ecx, ImmediateValue32(7));
		assembler.mov(eax, ImmediateValue32(5));
		assembler.add(eax, ecx);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 12);
	}
	{ // subtract registers
		buffer.clear();
		assembler.mov(ecx, ImmediateValue32(7));
		assembler.mov(eax, ImmediateValue32(5));
		assembler.sub(eax, ecx);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == -2);
	}
	{ // integer multiplication
		buffer.clear();
		assembler.mov(ecx, ImmediateValue32(-7));
		assembler.mov(eax, ImmediateValue32(9));
		assembler.imul(eax, ecx);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == -63);
	}
	{ // integer division
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(-70));
		assembler.cdq();
		assembler.mov(ecx, ImmediateValue32(9));
		assembler.idiv(ecx);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == -70 / 9);
	}
	{ // integer division remainder
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(-70));
		assembler.cdq();
		assembler.mov(ecx, ImmediateValue32(9));
		assembler.idiv(ecx);
		assembler.mov(eax, edx);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == -70 % 9);
	}
#ifdef _M_X64
	{ // add and subtract extended registers
		buffer.clear();
		assembler.mov(r8, ImmediateValue32(7));
		assembler.mov(r9, ImmediateValue32(8));
		assembler.add(r8, r9); // r8 has 15 in it after this
		assembler.mov(r9, ImmediateValue32(16));
		assembler.sub(r8, r9); // r8 has -1 in it after this
		assembler.mov(eax, r8);
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == -1);
	}
#endif
	{ // unconditional jumps
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(123));
		// each unconditional jump statement is 5 bytes
		assembler.jmp(Always, 5); // executed first
		assembler.jmp(Always, -5);
		assembler.jmp(Always, 133); // executed second
		assembler.jmp(Always, 133); // executed fourth
		for (int i = 0; i < 128; i++)
			assembler.ret(); // each return statement is 1 byte
		assembler.jmp(Always, -138); // executed third
		assembler.mov(eax, ImmediateValue32(456)); // executed fifth
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 456);
	}
	{ // conditional jumps and cmp
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(100));
		assembler.mov(ecx, ImmediateValue32(100));
		assembler.cmp(eax, ecx);
		// each conditional jump statement is 6 bytes
		assembler.jmp(NotEqual, -6); // These would repeat forever if eax and ecx were equal.
		assembler.jmp(LessThan, -6);
		assembler.jmp(GreaterThan, -6);
		assembler.jmp(Equal, 6); // this skips the next jump
		assembler.jmp(Equal, -6); // this would repeat forever if it were hit, but it's skipped by the previous jump
		assembler.jmp(GreaterThanOrEqual, 1); // this should skip the return
		assembler.ret();
		assembler.jmp(LessThanOrEqual, 1); // this should skip the return
		assembler.ret();
		assembler.cmp(ecx, ImmediateValue32(102));
		assembler.jmp(Equal, -6); // this would repeat forever if it were true
		assembler.jmp(NotEqual, 1); // this should skip the return
		assembler.ret();
		assembler.jmp(GreaterThan, -6); // this would repeat forever if it were true
		assembler.jmp(GreaterThanOrEqual, -6); // this would repeat forever if it were true
		assembler.jmp(LessThan, 1); // this should skip the return
		assembler.ret();
		assembler.jmp(LessThanOrEqual, 1); // this should skip the return
		assembler.ret();
		assembler.mov(eax, ImmediateValue32(101));
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 101);
	}
	{ // move to and from the stack
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(0));
		for (int32_t i = 1; i <= 200; i++) {
			assembler.mov(ecx, ImmediateValue32(i)); // put i into ecx
			assembler.mov(esp, -i * sizeof(uint32_t), ecx, false); // put ecx onto the next spot on the stack
		}
		for (int32_t i = 1; i <= 200; i++) {
			assembler.mov(ecx, esp, -i * sizeof(uint32_t), false); // get the value from the stack
			assembler.add(eax, ecx);
		}
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 200 * 201 / 2);
#ifdef _M_X64
		buffer.clear();
		assembler.mov(eax, ImmediateValue32(0));
		for (int32_t i = 1; i <= 200; i++) {
			assembler.mov(ecx, ImmediateValue64(static_cast<uint64_t>(i))); // put i into ecx
			assembler.mov(esp, -i * sizeof(uint64_t), ecx, true); // put ecx onto the next spot on the stack
		}
		assembler.mov(ecx, ImmediateValue32(0xFFFFFFFF));
		assembler.mov(esp, -6, ecx, false); // this writes in the middle 4 bytes of the first 64-bit value on the stack
		for (int32_t i = 1; i <= 200; i++) {
			assembler.mov(ecx, esp, -i * sizeof(uint64_t), true); // get the value from the stack
			assembler.add(eax, ecx);
		}
		assembler.ret();
		uint64_t(*function64)() = reinterpret_cast<uint64_t(*)()>(buffer.getExecutableAddress());
		uint64_t a = function64();
		assert(function64() == 200 * 201 / 2 + 0xFFFF0000); // the other 4 F's were lost when doing 32-bit adds
#endif
	}
	{ // call a function
		buffer.clear();
#ifdef _M_X64
		// Win64 only has one function calling convention, regardless or cdecl, stdcall, etc. decoration (which is ignored)
		// http://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
		assembler.mov(ecx, ImmediateValue64(3ull)); // x
		assembler.mov(edx, ImmediateValue64(5ull)); // y
		assembler.mov(r8, ImmediateValue64(7ull)); // z
		assembler.sub(esp, ImmediateValue32(32)); // shadow space
		assembler.mov(r9, ImmediateValuePtr(reinterpret_cast<uintptr_t>(doStuff32)));
		assembler.call(r9);
		assembler.add(esp, ImmediateValue32(32));
		assembler.ret();
#else
		assembler.push(ImmediateValue32(7)); // z (cdecl is right to left)
		assembler.push(ImmediateValue32(5)); // y
		assembler.push(ImmediateValue32(3)); // x
		assembler.mov(ecx, ImmediateValuePtr(reinterpret_cast<uintptr_t>(doStuff32)));
		assembler.call(ecx);
		assembler.pop(); // cdecl, so I need to clean up the stack
		assembler.pop();
		assembler.pop();
		assembler.ret();
#endif
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 25);
	}
#ifdef _M_X64
	{ // mov cleaning out high bits
		buffer.clear();
		assembler.mov(eax, ImmediateValue64(0x1234567812345678ull));
		assembler.mov(eax, ImmediateValue32(0x00000000));
		assembler.ret();
		unsigned(*function)() = reinterpret_cast<unsigned(*)()>(buffer.getExecutableAddress());
		assert(function() == 0x00000000);
	}
	{ // call a function with large parameters and return value
		buffer.clear();
		// Win64 only has one function calling convention, regardless or cdecl, stdcall, etc. decoration (which is ignored)
		// http://msdn.microsoft.com/en-us/library/9z1stfyw.aspx
		assembler.mov(r8, ecx); // z
		assembler.mov(edx, ImmediateValue64(10000000000000000ull)); // y
		assembler.mov(ecx, ImmediateValue64(10000000000000001ull)); // x
		assembler.sub(esp, ImmediateValue32(32)); // shadow space
		assembler.mov(r9, ImmediateValuePtr(reinterpret_cast<uintptr_t>(doStuff64)));
		assembler.call(r9);
		assembler.add(esp, ImmediateValue32(32));
		assembler.ret();
		uint64_t(*function)(uint64_t) = reinterpret_cast<uint64_t(*)(uint64_t)>(buffer.getExecutableAddress());
		assert(function(10000000000000003) == 10000000000000004);
	}
	{ // move large values
		buffer.clear();
		assembler.mov(eax, ImmediateValue64(0x01234567890ABCDEFull));
		assembler.ret();
		uint64_t(*function)() = reinterpret_cast<uint64_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 0x01234567890ABCDEF);
	}
	{ // move large values to extended registers
		buffer.clear();
		assembler.mov(eax, ImmediateValue64(0x01234567890ABCDEFull));
		assembler.push(r9);
		assembler.mov(r9, ImmediateValue64(0xFFFFFFFFFFFFFFFF));
		assembler.pop(r9);
		assembler.ret();
		uint64_t(*function)() = reinterpret_cast<uint64_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 0x01234567890ABCDEF);
	}
	{ // push and pop extended registers
		buffer.clear();
		assembler.push(r9);
		assembler.mov(r9, r15);
		assembler.pop(r9);
		assembler.mov(eax, ImmediateValue32(0x12345678));
		assembler.ret();
		uint32_t(*function)() = reinterpret_cast<uint32_t(*)()>(buffer.getExecutableAddress());
		assert(function() == 0x12345678);
	}
	{ // mulsd and addsd
		buffer.clear();
		assembler.mulsd(xmm0, xmm1);
		assembler.addsd(xmm0, xmm2);
		assembler.ret();
		double(*function)(double, double, double) = reinterpret_cast<double(*)(double, double, double)>(buffer.getExecutableAddress());
		assert(function(1.2, 2.3, 3.4) == 1.2 * 2.3 + 3.4);
	}
	{ // divsd and subsd
		buffer.clear();
		assembler.divsd(xmm0, xmm1);
		assembler.subsd(xmm0, xmm2);
		assembler.ret();
		double(*function)(double, double, double) = reinterpret_cast<double(*)(double, double, double)>(buffer.getExecutableAddress());
		assert(function(1.2, 2.3, 3.4) == 1.2 / 2.3 - 3.4);
	}
	{ // cvttsd2si
		buffer.clear();
		assembler.cvttsd2si(eax, xmm0);
		assembler.ret();
		int(*function)(double) = reinterpret_cast<int(*)(double)>(buffer.getExecutableAddress());
		assert(function(1.1) == 1);
		assert(function(0.9) == 0);
		assert(function(-0.0) == 0);
		assert(function(-0.1) == 0);
		assert(function(-1.1) == -1);
	}
	{ // cvttsd2si
		buffer.clear();
		assembler.cvtsi2sd(xmm0, ecx);
		assembler.ret();
		double(*function)(int) = reinterpret_cast<double(*)(int)>(buffer.getExecutableAddress());
		assert(function(1) == 1.0);
		assert(function(0) == 0.0);
		assert(function(-1) == -1.0);
	}
	{ // addsd, mulsd
		buffer.clear();
		assembler.addsd(xmm0, xmm1);
		assembler.mulsd(xmm0, xmm2);
		assembler.ret();
		double(*function)(double, double, double) = reinterpret_cast<double(*)(double, double, double)>(buffer.getExecutableAddress());
		assert(function(1.5, 1.7, 1.9) == 6.08);
	}
	{ // movsd
		buffer.clear();
		double d = 1.5;
		assembler.mov(eax, ImmediateValue64(*reinterpret_cast<uint64_t*>(&d)));
		assembler.push(eax);
		assembler.movsd(xmm2, esp, 0);
		assembler.pop();
		assembler.movsd(xmm1, xmm0);
		assembler.mulsd(xmm0, xmm2);
		assembler.mulsd(xmm0, xmm1);
		assembler.ret();
		double(*function)(double) = reinterpret_cast<double(*)(double)>(buffer.getExecutableAddress());
		assert(function(1.7) == 4.335 /* 1.5 * 1.7 * 1.7 */);
	}
	{ // push and pop with double registers
		buffer.clear();
		double d = 1.7;
		assembler.push(ImmediateValue64(*reinterpret_cast<uint64_t*>(&d)));
		assembler.pop(xmm0);
		assembler.ret();
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		assert(function() == 1.7);
	}
#else
	{ // push 64 bit values
		buffer.clear();
		double d = 1.7;
		assembler.push(ImmediateValue64(*reinterpret_cast<uint64_t*>(&d)));
		assembler.fld(esp, 0);
		assembler.pop64();
		assembler.ret();
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		double returnValue = function();
		assert(function() == 1.7);
	}
	{ // floating point operations
		buffer.clear();
		double d1 = 1.5;
		double d2 = 1.7;
		double d3 = 1.9;
		assembler.mov(eax, ImmediateValue32(reinterpret_cast<uint32_t*>(&d1)[1]));
		assembler.push(eax);
		assembler.mov(eax, ImmediateValue32(reinterpret_cast<uint32_t*>(&d1)[0]));
		assembler.push(eax);
		assembler.mov(eax, ImmediateValue32(reinterpret_cast<uint32_t*>(&d2)[1]));
		assembler.push(eax);
		assembler.mov(eax, ImmediateValue32(reinterpret_cast<uint32_t*>(&d2)[0]));
		assembler.push(eax);
		assembler.mov(eax, ImmediateValue32(reinterpret_cast<uint32_t*>(&d3)[1]));
		assembler.push(eax);
		assembler.mov(eax, ImmediateValue32(reinterpret_cast<uint32_t*>(&d3)[0]));
		assembler.push(eax);
		assembler.fld(esp, 0);
		assembler.fld(esp, 8);
		assembler.fld(esp, 16);
		assembler.pop();
		assembler.pop();
		assembler.pop();
		assembler.pop();
		assembler.pop();
		assembler.pop();
		assembler.fmulp();
		assembler.faddp();
		assembler.ret();
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		double returnValue = function();
		assert(function() == 4.4499999999999993 /* 1.5 * 1.7 + 1.9 */);
	}
	{ // convert from double to int
		buffer.clear();
		double d = -1.7;
		assembler.push(ImmediateValue64(*reinterpret_cast<uint64_t*>(&d)));
		assembler.cvttsd2si(eax, esp, 0);
		assembler.pop64();
		assembler.ret();
		int(*function)() = reinterpret_cast<int(*)()>(buffer.getExecutableAddress());
		int returnValue = function();
		assert(function() == (int)(-1.7));
	}
	{ // convert from int to double
		buffer.clear();
		assembler.push(ImmediateValue32(-77));
		assembler.fild(esp, 0);
		assembler.pop();
		assembler.ret();
		double(*function)() = reinterpret_cast<double(*)()>(buffer.getExecutableAddress());
		double returnValue = function();
		assert(function() == -77.0);
	}

#endif

#ifndef _M_X64
	checkX87Stack();
#endif
}

} // namespace Compiler
