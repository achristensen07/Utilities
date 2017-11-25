// The following license applies to all parts of this file.
/*************************************************
Copyright (c) 2017, Alex Christensen
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the FreeBSD Project.
*************************************************/

#pragma once
#include <array>
#include <assert.h>
#include <stdint.h>

namespace SHA2 {

template<typename RegisterType, size_t DigestSize, const std::array<RegisterType, 8>& InitialHash, size_t Rounds, const std::array<RegisterType, Rounds>& RoundConstants, const std::array<size_t, 12>& ShiftConstants>
class SHA2 {
public:
    std::array<RegisterType, DigestSize> digest() { finalize(); return digestTemplate<DigestSize>(); }
    void addBytes(const void* input, size_t length) {
		assert(!finalized);
		const uint8_t* bytes = reinterpret_cast<const uint8_t*>(input);
		const uint8_t* end = bytes + length;
		this->length += length;

		if (bufferContents) {
			while (bufferContents < BlockSizeBytes && bytes < end)
				buffer[bufferContents++] = *bytes++;
			if (bufferContents == BlockSizeBytes) {
				addBlock(buffer);
				bufferContents = 0;
			}
		}
		while (end - bytes >= BlockSizeBytes) {
			addBlock(bytes);
			bytes += BlockSizeBytes;
		}
		while (bytes < end)
			buffer[bufferContents++] = *bytes++;
	}

private:
    void finalize() {
		if (finalized)
			return;
		finalized = true;

		const uint8_t* bytes = buffer;
        size_t i = 0;
		while (i + BlockSizeBytes <= bufferContents) {
			addBlock(bytes + i);
			i += BlockSizeBytes;
		}

		uint8_t finalBlocks[BlockSizeBytes * 2];
		size_t j = 0;
		while (i < bufferContents)
			finalBlocks[j++] = bytes[i++];
		finalBlocks[j++] = 0x80;
		uint64_t l = length * 8;
		static constexpr size_t PaddingSize = sizeof(RegisterType) == 4 ? sizeof(uint64_t) : 2 * sizeof(uint64_t);
		if (j < BlockSizeBytes - PaddingSize) {
			while (j < BlockSizeBytes - sizeof(uint64_t))
				finalBlocks[j++] = 0x00;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 0] = l >> 56;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 1] = l >> 48;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 2] = l >> 40;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 3] = l >> 32;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 4] = l >> 24;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 5] = l >> 16;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 6] = l >> 8;
			finalBlocks[BlockSizeBytes - sizeof(uint64_t) + 7] = l;
			addBlock(finalBlocks);
		} else {
			while (j < 2 * BlockSizeBytes - sizeof(uint64_t))
				finalBlocks[j++] = 0x00;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 0] = l >> 56;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 1] = l >> 48;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 2] = l >> 40;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 3] = l >> 32;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 4] = l >> 24;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 5] = l >> 16;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 6] = l >> 8;
			finalBlocks[2 * BlockSizeBytes - sizeof(uint64_t) + 7] = l;
			addBlock(finalBlocks);
			addBlock(finalBlocks + BlockSizeBytes);
		}
		bufferContents = 0;
    }

    template<size_t ArrayLength, typename std::enable_if_t<ArrayLength == 6>* = nullptr>
    std::array<RegisterType, ArrayLength> digestTemplate() { return { h0, h1, h2, h3, h4, h5 }; }
    template<size_t ArrayLength, typename std::enable_if_t<ArrayLength == 7>* = nullptr>
    std::array<RegisterType, ArrayLength> digestTemplate() { return { h0, h1, h2, h3, h4, h5, h6 }; }
    template<size_t ArrayLength, typename std::enable_if_t<ArrayLength == 8>* = nullptr>
    std::array<RegisterType, ArrayLength> digestTemplate() { return { h0, h1, h2, h3, h4, h5, h6, h7 }; }

    template<typename Integer>
	Integer rotateRight(Integer bits, size_t bitsToRotate)
	{
		return (bits >> bitsToRotate) | (bits << (sizeof(Integer) * 8 - bitsToRotate));
	}

    template<typename Integer>
    Integer readBits(const uint8_t* bytes)
	{
		Integer value = bytes[0];
		for (size_t i = 1; i < sizeof(Integer); ++i) {
			value <<= 8;
			value |= bytes[i];
		}
		return value;
	}

    void addBlock(const uint8_t* block)
	{
		RegisterType w[Rounds];
		for (size_t i = 0; i < 16; ++i)
			w[i] = readBits<RegisterType>(block + i * sizeof(RegisterType));
		for (size_t i = 16; i < Rounds; ++i) {
			RegisterType s0 = rotateRight(w[i - 15], ShiftConstants[0]) ^ rotateRight(w[i - 15], ShiftConstants[1]) ^ (w[i - 15] >> ShiftConstants[2]);
			RegisterType s1 = rotateRight(w[i - 2], ShiftConstants[3]) ^ rotateRight(w[i - 2], ShiftConstants[4]) ^ (w[i - 2] >> ShiftConstants[5]);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}
		RegisterType a = h0;
		RegisterType b = h1;
		RegisterType c = h2;
		RegisterType d = h3;
		RegisterType e = h4;
		RegisterType f = h5;
		RegisterType g = h6;
		RegisterType h = h7;
		for (size_t i = 0; i < Rounds; ++i) {
			RegisterType S1 = rotateRight(e, ShiftConstants[6]) ^ rotateRight(e, ShiftConstants[7]) ^ rotateRight(e, ShiftConstants[8]);
			RegisterType ch = (e & f) ^ ((~e) & g);
			RegisterType t1 = h + S1 + ch + RoundConstants[i] + w[i];
			RegisterType S0 = rotateRight(a, ShiftConstants[9]) ^ rotateRight(a, ShiftConstants[10]) ^ rotateRight(a, ShiftConstants[11]);
			RegisterType maj = (a & b) ^ (a & c) ^ (b & c);
			RegisterType t2 = S0 + maj;
			h = g;
			g = f;
			f = e;
			e = d + t1;
			d = c;
			c = b;
			b = a;
			a = t1 + t2;
		}
		h0 += a;
		h1 += b;
		h2 += c;
		h3 += d;
		h4 += e;
		h5 += f;
		h6 += g;
		h7 += h;
	}

	static constexpr size_t BlockSizeBytes = 16 * sizeof(RegisterType);
    RegisterType h0 { InitialHash[0] };
    RegisterType h1 { InitialHash[1] };
    RegisterType h2 { InitialHash[2] };
    RegisterType h3 { InitialHash[3] };
    RegisterType h4 { InitialHash[4] };
    RegisterType h5 { InitialHash[5] };
    RegisterType h6 { InitialHash[6] };
    RegisterType h7 { InitialHash[7] };
    
    uint64_t length { 0 };
    uint8_t buffer[BlockSizeBytes];
    size_t bufferContents { 0 };
    bool finalized { false };
};

constexpr std::array<uint32_t, 64> RoundConstants32 = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

constexpr std::array<uint64_t, 80> RoundConstants64 = {
    0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc, 0x3956c25bf348b538, 
	0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118, 0xd807aa98a3030242, 0x12835b0145706fbe, 
	0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2, 0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 
	0xc19bf174cf692694, 0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65, 
	0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5, 0x983e5152ee66dfab, 
	0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4, 0xc6e00bf33da88fc2, 0xd5a79147930aa725, 
	0x06ca6351e003826f, 0x142929670a0e6e70, 0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 
	0x53380d139d95b3df, 0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b, 
	0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30, 0xd192e819d6ef5218, 
	0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8, 0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 
	0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8, 0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 
	0x682e6ff3d6b2b8a3, 0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec, 
	0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b, 0xca273eceea26619c, 
	0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178, 0x06f067aa72176fba, 0x0a637dc5a2c898a6, 
	0x113f9804bef90dae, 0x1b710b35131c471b, 0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 
	0x431d67c49c100d4c, 0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817
};

constexpr std::array<size_t, 12> ShiftConstants32 = { 7, 18, 3, 17, 19, 10, 6, 11, 25, 2, 13, 22 };
constexpr std::array<size_t, 12> ShiftConstants64 = { 1, 8, 7, 19, 61, 6, 14, 18, 41, 28, 34, 39 };
constexpr std::array<uint32_t, 8> InitialSHA224Hash = { 0xc1059ed8, 0x367cd507, 0x3070dd17, 0xf70e5939, 0xffc00b31, 0x68581511, 0x64f98fa7, 0xbefa4fa4 };
constexpr std::array<uint32_t, 8> InitialSHA256Hash = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
constexpr std::array<uint64_t, 8> InitialSHA314Hash = { 0xcbbb9d5dc1059ed8, 0x629a292a367cd507, 0x9159015a3070dd17, 0x152fecd8f70e5939, 0x67332667ffc00b31, 0x8eb44a8768581511, 0xdb0c2e0d64f98fa7, 0x47b5481dbefa4fa4 };
constexpr std::array<uint64_t, 8> InitialSHA512Hash = { 0x6a09e667f3bcc908, 0xbb67ae8584caa73b, 0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1, 0x510e527fade682d1, 0x9b05688c2b3e6c1f, 0x1f83d9abfb41bd6b, 0x5be0cd19137e2179 };

using SHA224 = SHA2<uint32_t, 7, InitialSHA224Hash, 64, RoundConstants32, ShiftConstants32>;
using SHA256 = SHA2<uint32_t, 8, InitialSHA256Hash, 64, RoundConstants32, ShiftConstants32>;
using SHA314 = SHA2<uint64_t, 6, InitialSHA314Hash, 80, RoundConstants64, ShiftConstants64>;
using SHA512 = SHA2<uint64_t, 8, InitialSHA512Hash, 80, RoundConstants64, ShiftConstants64>;

}
