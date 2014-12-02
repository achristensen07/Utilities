#include "AssemblerBuffer.h"
#include <new>
#include <stdint.h>
#include <algorithm>

#define NOMINMAX
#include <Windows.h>

namespace Compiler {

uint32_t AssemblerBuffer::getPageSize()
{
	static uint32_t pageSize = 0;
	if (!pageSize) {
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		pageSize = si.dwPageSize;
	}
	// This is probably 4096.
	return pageSize;
}

AssemblerBuffer::~AssemblerBuffer()
{
	if (allocatedMemory)
		freeMemory(allocatedMemory, allocatedSize);
}

void AssemblerBuffer::freeMemory(void* memory, uint32_t size)
{
	if (memory) {
		VirtualFree(memory, size, MEM_DECOMMIT);
		VirtualFree(memory, 0, MEM_RELEASE);
	}
}

void* AssemblerBuffer::allocateMemory(uint32_t size)
{
	if (size) {
		void* memory = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		if (!memory)
			throw std::bad_alloc();
		return memory;
	}
	return nullptr;
}

void AssemblerBuffer::clear()
{
	if (allocatedMemory)
		freeMemory(allocatedMemory, allocatedSize);
	allocatedSize = 0;
	allocatedMemory = nullptr;
	usedSize = 0;
}

AssemblerBuffer::AssemblerBuffer(uint32_t initialSize)
	: allocatedMemory(nullptr)
	, allocatedSize(0)
	, usedSize(0)
{
	reserve(initialSize);
}

void AssemblerBuffer::reserve(uint32_t size)
{
	if (size > allocatedSize) {
		void* oldAllocatedMemory = allocatedMemory;
		uint32_t oldAllocatedSize = allocatedSize;
		uint32_t alignment = getPageSize();
		allocatedSize = std::max<uint32_t>(1024, std::max(2 * allocatedSize, ((size + alignment - 1) / alignment) * alignment));
		allocatedMemory = allocateMemory(allocatedSize);
		if (oldAllocatedMemory) {
			memcpy(allocatedMemory, oldAllocatedMemory, usedSize);
			freeMemory(oldAllocatedMemory, oldAllocatedSize);
		}
	}
}

void AssemblerBuffer::setByte(uint32_t location, uint8_t value)
{
	compiler_assert(location < usedSize, "assembler buffer out of range");
	reinterpret_cast<uint8_t*>(allocatedMemory)[location] = value;
}

void AssemblerBuffer::appendContentsOf(const AssemblerBuffer& other)
{
	reserve(usedSize + other.usedSize);
	memcpy(reinterpret_cast<uint8_t*>(allocatedMemory)+usedSize, other.allocatedMemory, other.usedSize);
	usedSize += other.usedSize;
}

} // namespace Compiler
