#ifndef ASSEMBLER_BUFFER_H
#define ASSEMBLER_BUFFER_H

#include "AssemblerBuffer.h"
#include <assert.h>
#include <exception>
#include <stdint.h>
#include <stdio.h>

namespace Compiler {

#define compiler_assert(condition, message) if (!(condition)) { char buffer[1000]; sprintf_s(buffer, "compiler error %d: %s", __LINE__, message); assert(0); throw std::exception(buffer); }

class AssemblerBuffer
{
public:
	AssemblerBuffer(uint32_t initialSize = 0);
	~AssemblerBuffer();

	void reserve(uint32_t);
	void clear();
	uint32_t size() { return usedSize; }
	void appendContentsOf(const AssemblerBuffer&);

	void setByte(uint32_t location, uint8_t value);

	// The template is private and these are given their own names to prevent pushing the wrong size values into the buffer.
	void push8(uint8_t value) { pushInteger<uint8_t>(value); }
	void push32(uint32_t value) { pushInteger<uint32_t>(value); }
#ifdef _M_X64
	void push64(uint64_t value) { pushInteger<uint64_t>(value); }
#endif

	const void* getExecutableAddress() { return allocatedMemory; }

private:
	void* allocatedMemory;
	uint32_t allocatedSize;
	uint32_t usedSize;

	// allocateWritableExecutableMemory only allocates in multiples of this size.
	static uint32_t getPageSize();

	static void* allocateMemory(uint32_t);
	static void freeMemory(void*, uint32_t);

	template <typename integer>
	inline void pushInteger(integer value) {
		reserve(sizeof(integer) + usedSize);
		*reinterpret_cast<integer*>(static_cast<uint8_t*>(allocatedMemory)+usedSize) = value;
		usedSize += sizeof(integer);
	}
};

} // namespace Compiler

#endif
