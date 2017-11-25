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

#include "SHA2.h"

#include <stdio.h>
#include <vector>

using SHA2::SHA224;
using SHA2::SHA256;
using SHA2::SHA314;
using SHA2::SHA512;

template<typename T>
bool equalDigests(T a, T b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i] != b[i])
            return false;
    return true;
};

// Based on https://www.di-mgt.com.au/sha_testvectors.html
bool largeTest() {
    const char* t = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno";
    const size_t repeat = 16777216;
    std::vector<uint8_t> b;
    b.reserve(repeat * strlen(t));
    for (size_t i = 0; i < repeat; ++i)
        for (size_t j = 0; j < strlen(t); ++j)
            b.push_back(t[j]);
    SHA224 sha224;sha224.addBytes(b.data(), b.size());
    SHA256 sha256;sha256.addBytes(b.data(), b.size());
    SHA314 sha314;sha314.addBytes(b.data(), b.size());
    SHA512 sha512;sha512.addBytes(b.data(), b.size());
    return equalDigests(sha224.digest(), {0xb5989713, 0xca4fe47a, 0x009f8621, 0x980b34e6, 0xd63ed306, 0x3b2a0a2c, 0x867d8a85})
        && equalDigests(sha256.digest(), {0x50e72a0e, 0x26442fe2, 0x552dc393, 0x8ac58658, 0x228c0cbf, 0xb1d2ca87, 0x2ae43526, 0x6fcd055e})
        && equalDigests(sha314.digest(), {0x5441235cc0235341, 0xed806a64fb354742, 0xb5e5c02a3c5cb71b, 0x5f63fb793458d8fd, 0xae599c8cd8884943, 0xc04f11b31b89f023})
        && equalDigests(sha512.digest(), {0xb47c933421ea2db1, 0x49ad6e10fce6c7f9, 0x3d0752380180ffd7, 0xf4629a712134831d, 0x77be6091b819ed35, 0x2c2967a2e2d4fa50, 0x50723c9630691f1a, 0x05a7281dbe6c1086});
}

bool testSHA2()
{
    auto sha224digest = [](const char* string) { SHA224 sha224; sha224.addBytes(string, strlen(string)); return sha224.digest(); };
    auto sha256digest = [](const char* string) { SHA256 sha256; sha256.addBytes(string, strlen(string)); return sha256.digest(); };
    auto sha314digest = [](const char* string) { SHA314 sha314; sha314.addBytes(string, strlen(string)); return sha314.digest(); };
    auto sha512digest = [](const char* string) { SHA512 sha512; sha512.addBytes(string, strlen(string)); return sha512.digest(); };

    std::vector<uint8_t> a;
    a.reserve(1000000);
    for (size_t i = 0; i < 1000000; i++)
        a.push_back('a');
    SHA224 sha224;sha224.addBytes(a.data(), a.size());
    SHA256 sha256;sha256.addBytes(a.data(), a.size());
    SHA314 sha314;sha314.addBytes(a.data(), a.size());
    SHA512 sha512;sha512.addBytes(a.data(), a.size());

    SHA256 buffer;
    buffer.addBytes("a", 1);
    buffer.addBytes("b", 1);
    buffer.addBytes("c", 1);
    if (!equalDigests(sha256digest("abc"), buffer.digest()))
        return false;
        
    std::vector<size_t> sizes = {1, 7, 32, 64, 128, 127, 255, 256, 257, 6040, 1542, 100000, 555555};
    for (size_t adding : sizes) {
        SHA256 buffer;
        size_t added = 0;
        for (size_t i = 0 ; i + adding < 1000000; i += adding) {
            added += adding;
            buffer.addBytes(a.data(), adding);
        }
        buffer.addBytes(a.data(), 1000000 - added);
        if (!equalDigests(sha256.digest(), buffer.digest()))
            return false;
    }

    return equalDigests(sha224digest("abc"), {0x23097d22, 0x3405d822, 0x8642a477, 0xbda255b3, 0x2aadbce4, 0xbda0b3f7, 0xe36c9da7})
        && equalDigests(sha224digest(""), {0xd14a028c, 0x2a3a2bc9, 0x476102bb, 0x288234c4, 0x15a2b01f, 0x828ea62a, 0xc5b3e42f})
        && equalDigests(sha224digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), {0x75388b16, 0x512776cc, 0x5dba5da1, 0xfd890150, 0xb0c6455c, 0xb4f58b19, 0x52522525})
        && equalDigests(sha224digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), {0xc97ca9a5, 0x59850ce9, 0x7a04a96d, 0xef6d99a9, 0xe0e0e2ab, 0x14e6b8df, 0x265fc0b3})
        && equalDigests(sha224.digest(), {0x20794655, 0x980c91d8, 0xbbb4c1ea, 0x97618a4b, 0xf03f4258, 0x1948b2ee, 0x4ee7ad67})

        && equalDigests(sha256digest("abc"), {0xba7816bf, 0x8f01cfea, 0x414140de, 0x5dae2223, 0xb00361a3, 0x96177a9c, 0xb410ff61, 0xf20015ad})
        && equalDigests(sha256digest(""), {0xe3b0c442, 0x98fc1c14, 0x9afbf4c8, 0x996fb924, 0x27ae41e4, 0x649b934c, 0xa495991b, 0x7852b855})
        && equalDigests(sha256digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), {0x248d6a61, 0xd20638b8, 0xe5c02693, 0x0c3e6039, 0xa33ce459, 0x64ff2167, 0xf6ecedd4, 0x19db06c1})
        && equalDigests(sha256digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), {0xcf5b16a7, 0x78af8380, 0x036ce59e, 0x7b049237, 0x0b249b11, 0xe8f07a51, 0xafac4503, 0x7afee9d1})
        && equalDigests(sha256.digest(), {0xcdc76e5c, 0x9914fb92, 0x81a1c7e2, 0x84d73e67, 0xf1809a48, 0xa497200e, 0x046d39cc, 0xc7112cd0})

        && equalDigests(sha314digest("abc"), {0xcb00753f45a35e8b, 0xb5a03d699ac65007, 0x272c32ab0eded163, 0x1a8b605a43ff5bed, 0x8086072ba1e7cc23, 0x58baeca134c825a7})
        && equalDigests(sha314digest(""), {0x38b060a751ac9638, 0x4cd9327eb1b1e36a, 0x21fdb71114be0743, 0x4c0cc7bf63f6e1da, 0x274edebfe76f65fb, 0xd51ad2f14898b95b})
        && equalDigests(sha314digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), {0x3391fdddfc8dc739, 0x3707a65b1b470939, 0x7cf8b1d162af05ab, 0xfe8f450de5f36bc6, 0xb0455a8520bc4e6f, 0x5fe95b1fe3c8452b})
        && equalDigests(sha314digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), {0x09330c33f71147e8, 0x3d192fc782cd1b47, 0x53111b173b3b05d2, 0x2fa08086e3b0f712, 0xfcc7c71a557e2db9, 0x66c3e9fa91746039})
        && equalDigests(sha314.digest(), {0x9d0e1809716474cb, 0x086e834e310a4a1c, 0xed149e9c00f24852, 0x7972cec5704c2a5b, 0x07b8b3dc38ecc4eb, 0xae97ddd87f3d8985})

        && equalDigests(sha512digest("abc"), {0xddaf35a193617aba, 0xcc417349ae204131, 0x12e6fa4e89a97ea2, 0x0a9eeee64b55d39a, 0x2192992a274fc1a8, 0x36ba3c23a3feebbd, 0x454d4423643ce80e, 0x2a9ac94fa54ca49f})
        && equalDigests(sha512digest(""), {0xcf83e1357eefb8bd, 0xf1542850d66d8007, 0xd620e4050b5715dc, 0x83f4a921d36ce9ce, 0x47d0d13c5d85f2b0, 0xff8318d2877eec2f, 0x63b931bd47417a81, 0xa538327af927da3e})
        && equalDigests(sha512digest("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), {0x204a8fc6dda82f0a, 0x0ced7beb8e08a416, 0x57c16ef468b228a8, 0x279be331a703c335, 0x96fd15c13b1b07f9, 0xaa1d3bea57789ca0, 0x31ad85c7a71dd703, 0x54ec631238ca3445})
        && equalDigests(sha512digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu"), {0x8e959b75dae313da, 0x8cf4f72814fc143f, 0x8f7779c6eb9f7fa1, 0x7299aeadb6889018, 0x501d289e4900f7e4, 0x331b99dec4b5433a, 0xc7d329eeb6dd2654, 0x5e96e55b874be909})
        && equalDigests(sha512.digest(), {0xe718483d0ce76964, 0x4e2e42c7bc15b463, 0x8e1f98b13b204428, 0x5632a803afa973eb, 0xde0ff244877ea60a, 0x4cb0432ce577c31b, 0xeb009c5c2c49aa2e, 0x4eadb217ad8cc09b})

        && largeTest();
}

int main(int argc, const char** argv)
{
    printf("TEST PASSED %d\n", testSHA2());
    return 0;
}
