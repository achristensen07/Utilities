#ifndef ABSTRACT_SYNTAX_TREE_H
#define ABSTRACT_SYNTAX_TREE_H

#include "Assembler.h"
#include "PageAllocator.h"
#include <vector>
#include <memory>
#include <map>
#include <set>

namespace Compiler {

enum DataType
{
	Undetermined,
	None,
	Double, // doubles are returned in xmm0 (64-bit) or in st0 (32-bit)
	Int32, // ints, pointers, and char*s, are returned in eax
	Pointer,
	String, // strings are basically std::string wrappers.  A pointer to the std::string (on the stack) is returned in eax
	CharStar, // a pointer to the string data or string literal data
};

enum ASTNodeType
{
	Return,
	Literal,
	BinaryOperation,
	UnaryOperation,
	FunctionCall,
	IfElse,
	Break,
	Continue,
	Case,
	Default,
	ForLoop,
	WhileLoop,
	Switch,
	Cast,
	GetLocalVar,
	SetLocalVar,
	DeclareLocalVar,
	Scope,
};

typedef int32_t StackOffset; // negative stack offsets are parameters' locations, positive stack offsets are local variables or other stuff pushed to the stack

#define PAGE_ALLOCATED \
	void* operator new(size_t size); \
	void operator delete(void* ptr);

struct ASTNode {
	ASTNode() : dataType(Undetermined) {};
	virtual void compile(AssemblerBuffer&) const = 0;
	virtual ~ASTNode() = 0;
	virtual ASTNodeType nodeType() const = 0;
	mutable DataType dataType;

protected:
	static void ASTNode::castIfNecessary(DataType to, DataType from, AssemblerBuffer& buffer); // helper for casting between types
};

struct ASTCast : public ASTNode { 
	PAGE_ALLOCATED
	std::unique_ptr<ASTNode> valueToCast;

	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTCast() { compiler_assert(valueToCast, "cast value should be set"); }
	virtual ASTNodeType nodeType() const { return Cast; }
};

struct ASTGetLocalVar : public ASTNode { 
	PAGE_ALLOCATED
	std::string name;

	ASTGetLocalVar(const std::string& name) : name(name) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTGetLocalVar() {}
	virtual ASTNodeType nodeType() const { return GetLocalVar; }
};

struct ASTSetLocalVar : public ASTNode {
	PAGE_ALLOCATED
	std::string name;
	std::unique_ptr<ASTNode> valueToSet;

	ASTSetLocalVar(const std::string& name, std::unique_ptr<ASTNode> valueToSet) : name(name), valueToSet(std::move(valueToSet)) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTSetLocalVar() { compiler_assert(valueToSet, "local variable value should be set"); }
	virtual ASTNodeType nodeType() const { return SetLocalVar; }
};

struct ASTDeclareLocalVar : public ASTNode {
	PAGE_ALLOCATED
	std::string name;
	DataType type; // This is the type of the variable.  dataType should always be None because the ASTNode should not return a value.
	std::unique_ptr<ASTNode> initialValue; // optional

	ASTDeclareLocalVar(DataType dataType, const std::string& name) : name(name), type(dataType) { this->dataType = None; }
	ASTDeclareLocalVar(DataType dataType, const std::string& name, std::unique_ptr<ASTNode> initialValue) : name(name), type(dataType), initialValue(std::move(initialValue)) { this->dataType = None; }
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTDeclareLocalVar() {}
	virtual ASTNodeType nodeType() const { return DeclareLocalVar; }
};

struct ASTReturn : public ASTNode {
	PAGE_ALLOCATED
	std::unique_ptr<ASTNode> returnValue;

	ASTReturn() {}
	ASTReturn(DataType dataType) { compiler_assert(dataType == None, "return data type must be None"); this->dataType = dataType; }
	ASTReturn(std::unique_ptr<ASTNode> returnValue, DataType dataType) : returnValue(std::move(returnValue)) { this->dataType = dataType; }
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTReturn() { compiler_assert(dataType == None || returnValue, "return must have None data type or a return value"); }
	virtual ASTNodeType nodeType() const { return Return; }

	friend struct AbstractSyntaxTree;
};

struct ASTLiteral : public ASTNode {
	PAGE_ALLOCATED
	union {
		double doubleValue;
		int32_t intValue;
		void* pointerValue;
	};
	std::string stringValue;

	ASTLiteral() {}
	ASTLiteral(int32_t value) : intValue(value) { dataType = Int32; }
	ASTLiteral(double value) : doubleValue(value) { dataType = Double; }
	ASTLiteral(const char* value) : stringValue(value) { dataType = CharStar; }
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTLiteral() {};
	virtual ASTNodeType nodeType() const { return Literal; }
};

struct ASTBinaryOperation : public ASTNode {
	PAGE_ALLOCATED
	enum ASTBinaryOperationType {
		Invalid,
		// arithmetic
		Add,
		Subtract,
		Multiply,
		Divide,
		Mod,
		// comparison
		Equal,
		NotEqual,
		GreaterThan,
		GreaterThanOrEqual,
		LessThan,
		LessThanOrEqual,
		// bitwise
		LeftBitShift,
		RightBitShift,
		BitwiseXOr,
		BitwiseOr,
		BitwiseAnd,
		// logical
		LogicalOr,
		LogicalAnd,
		// operator[]
		Brackets,
	};
	std::unique_ptr<ASTNode> leftOperand;
	std::unique_ptr<ASTNode> rightOperand;
	ASTBinaryOperationType operationType;

	ASTBinaryOperation() { operationType = Invalid; }
	ASTBinaryOperation(ASTBinaryOperationType operationType, std::unique_ptr<ASTNode> leftOperand, std::unique_ptr<ASTNode> rightOperand)
		: operationType(operationType), leftOperand(std::move(leftOperand)), rightOperand(std::move(rightOperand)) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTBinaryOperation() { compiler_assert(leftOperand && rightOperand, "binary operation operands must be set"); }
	virtual ASTNodeType nodeType() const { return BinaryOperation; }
};

struct ASTUnaryOperation : public ASTNode {
	PAGE_ALLOCATED
	enum ASTUnaryOperationType {
		Negate,
		LogicalNot,
		BitwiseNot,
	};
	std::unique_ptr<ASTNode> operand;
	ASTUnaryOperationType operationType;

	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTUnaryOperation() { compiler_assert(operand, "unary operation operand must be set"); }
	virtual ASTNodeType nodeType() const { return UnaryOperation; }
};

struct ASTFunctionCall : public ASTNode {
	PAGE_ALLOCATED
	void* functionAddress;
	std::vector<std::unique_ptr<ASTNode>> parameters;

	ASTFunctionCall() : functionAddress(nullptr) {};
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTFunctionCall() { compiler_assert(functionAddress, "function call must have address"); }
	virtual ASTNodeType nodeType() const { return FunctionCall; }
};

struct ASTIfElse : public ASTNode {
	PAGE_ALLOCATED
	std::unique_ptr<ASTNode> condition;
	std::vector<std::unique_ptr<ASTNode>> ifBody;
	std::vector<std::unique_ptr<ASTNode>> elseBody;

	ASTIfElse() {}
	ASTIfElse(std::unique_ptr<ASTNode> condition) : condition(std::move(condition)) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTIfElse() { compiler_assert(condition, "if statement must have condition"); }
	virtual ASTNodeType nodeType() const { return IfElse; }
};

struct ASTBreak : public ASTNode {
	PAGE_ALLOCATED
	mutable uint32_t jumpFromLocation;
	mutable Assembler::JumpDistanceLocation jumpDistanceLocation;

	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTBreak() { compiler_assert(jumpFromLocation && jumpDistanceLocation, "break must have jump location and distance set while compiling"); }
	virtual ASTNodeType nodeType() const { return Break; }
};

struct ASTContinue : public ASTNode {
	PAGE_ALLOCATED
	mutable uint32_t jumpFromLocation;
	mutable Assembler::JumpDistanceLocation jumpDistanceLocation;

	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTContinue() { compiler_assert(jumpFromLocation && jumpDistanceLocation, "continue must have jump location and distance set while compiling"); }
	virtual ASTNodeType nodeType() const { return Continue; }
};

struct ASTCase : public ASTNode {
	PAGE_ALLOCATED
	mutable uint32_t beginLocation;
	int32_t compareValue;

	ASTCase(int32_t compareValue) : compareValue(compareValue) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTCase() { compiler_assert(beginLocation, "case must have begin location set while compiling"); }
	virtual ASTNodeType nodeType() const { return Case; }
};

struct ASTDefault : public ASTNode {
	PAGE_ALLOCATED
	mutable uint32_t beginLocation;

	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTDefault() { compiler_assert(beginLocation, "default must have begin location set while compiling"); }
	virtual ASTNodeType nodeType() const { return Default; }
};

struct ASTForLoop : public ASTNode {
	PAGE_ALLOCATED
	std::unique_ptr<ASTNode> initializer;
	std::unique_ptr<ASTNode> condition;
	std::unique_ptr<ASTNode> incrementer;
	std::vector<std::unique_ptr<ASTNode>> body;

	ASTForLoop(std::unique_ptr<ASTNode> initializer, std::unique_ptr<ASTNode> condition, std::unique_ptr<ASTNode> incrementer) 
		: initializer(std::move(initializer)), condition(std::move(condition)), incrementer(std::move(incrementer)) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTForLoop() {}
	virtual ASTNodeType nodeType() const { return ForLoop; }

private:
	mutable std::vector<const ASTBreak*> breaks;
	mutable std::vector<const ASTContinue*> continues;
	friend struct ASTBreak;
	friend struct ASTContinue;
	friend struct AbstractSyntaxTree;
};

struct ASTWhileLoop : public ASTNode {
	PAGE_ALLOCATED
	std::unique_ptr<ASTNode> condition;
	std::vector<std::unique_ptr<ASTNode>> body;

	ASTWhileLoop(std::unique_ptr<ASTNode> condition) : condition(std::move(condition)) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTWhileLoop() { compiler_assert(condition, "while loop must have condition"); }
	virtual ASTNodeType nodeType() const { return WhileLoop; }

private:
	mutable std::vector<const ASTBreak*> breaks;
	mutable std::vector<const ASTContinue*> continues;
	friend struct ASTBreak;
	friend struct ASTContinue;
};

struct ASTSwitch : public ASTNode {
	PAGE_ALLOCATED
	std::unique_ptr<ASTNode> valueToCompare;
	std::vector<std::unique_ptr<ASTNode>> body;

	ASTSwitch(std::unique_ptr<ASTNode> valueToCompare) : valueToCompare(std::move(valueToCompare)), default(nullptr) {}
	ASTSwitch() : default(nullptr) {}
	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTSwitch() { compiler_assert(valueToCompare, "switch must have a value to compare"); }
	virtual ASTNodeType nodeType() const { return Switch; }

private:
	mutable std::vector<const ASTCase*> cases;
	mutable std::vector<const ASTBreak*> breaks;
	mutable const ASTDefault* default;
	friend struct ASTCase;
	friend struct ASTBreak;
	friend struct ASTDefault;
};

struct ASTScope : public ASTNode {
	PAGE_ALLOCATED
	std::vector<std::unique_ptr<ASTNode>> body;

	virtual void compile(AssemblerBuffer&) const;
	virtual ~ASTScope() {}
	virtual ASTNodeType nodeType() const { return Scope; }
};

struct AbstractSyntaxTree {
	std::set<std::string> possibleStringLiterals;
	std::vector<std::unique_ptr<ASTNode>> statements;
	std::vector<std::pair<DataType, std::string>> parameters;

	AbstractSyntaxTree() {}
	void compile(AssemblerBuffer&);
	~AbstractSyntaxTree();
	static void runASTUnitTests();

	static std::map<std::string, StackOffset> stringLiteralLocations;
	static std::vector<std::map<std::string, std::pair<DataType, StackOffset>>> scopes;
	static std::vector<const ASTNode*> scopeParents;
	static StackOffset stackOffset;
	static StackOffset parameterStackOffset;
	static void incrementScope(const ASTNode* scopeParent);
	static void deallocateVariablesAndDecrementScope(AssemblerBuffer&);

private:
	void pushPossibleStringLiterals(AssemblerBuffer&);
	void processParameters(AssemblerBuffer&);
};

} // namespace Compiler

#endif
