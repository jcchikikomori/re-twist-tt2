// SHA-256 (FIPS 180-4). Expected digests are typed in as hex literals:
// the NIST examples, and Python's hashlib for the padding boundaries.
#include "test_harness.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "tt2/sha256.hpp"

using namespace tt2;

namespace {
std::string hex_of(std::string_view s) { return to_hex(sha256(s)); }

std::filesystem::path temp_dir(const char* tag) {
    std::random_device rd;
    const auto p = std::filesystem::temp_directory_path() /
                   ("tt2_sha256_test_" + std::string(tag) + "_" + std::to_string(rd()));
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p;
}
}  // namespace

TEST(sha256_nist_examples) {
    ASSERT_EQ(hex_of(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    ASSERT_EQ(hex_of("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    ASSERT_EQ(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

TEST(sha256_padding_boundaries) {
    // 55 bytes is the longest message whose padding fits one block; 56
    // and 64 spill into a second one.
    ASSERT_EQ(hex_of(std::string(55, 'a')), std::string("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"));
    ASSERT_EQ(hex_of(std::string(56, 'a')), std::string("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"));
    ASSERT_EQ(hex_of(std::string(63, 'a')), std::string("7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"));
    ASSERT_EQ(hex_of(std::string(64, 'a')), std::string("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"));
    ASSERT_EQ(hex_of(std::string(65, 'a')), std::string("635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"));
    ASSERT_EQ(hex_of(std::string(119, 'a')), std::string("31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"));
    ASSERT_EQ(hex_of(std::string(120, 'a')), std::string("2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"));
}

TEST(sha256_million_a_in_uneven_chunks) {
    // Chunk sizes cycle through 1..37, so blocks are filled from every offset.
    Sha256 h;
    std::size_t fed = 0;
    for (std::size_t n = 1; fed < 1000000; n = n % 37 + 1) {
        const std::size_t take = std::min(n, 1000000 - fed);
        h.update(std::string(take, 'a'));
        fed += take;
    }
    ASSERT_EQ(to_hex(h.finish()), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

TEST(sha256_split_feed_matches_one_shot) {
    std::string buf(200, '\0');
    for (std::size_t i = 0; i < buf.size(); ++i) buf[i] = static_cast<char>(i * 7 + 3);
    const auto whole = sha256(buf);
    Sha256 h;   // reused: finish() resets it
    for (std::size_t cut = 0; cut <= buf.size(); ++cut) {
        h.update(buf.data(), cut);
        h.update(buf.data() + cut, buf.size() - cut);
        ASSERT_TRUE(h.finish() == whole);
    }
}

TEST(sha256_one_bit_flip_changes_the_digest) {
    std::string a(100, 'x');
    std::string b = a;
    b[50] = static_cast<char>(b[50] ^ 0x01);
    ASSERT_NE(hex_of(a), hex_of(b));
    ASSERT_NE(hex_of("abc"), hex_of("abd"));
    ASSERT_NE(hex_of(""), hex_of(std::string(1, '\0')));   // a NUL byte is still a byte
}

TEST(sha256_file_matches_memory_across_chunks) {
    // 64 KiB * 2 + 123 bytes: two full read chunks and a short tail.
    const auto dir = temp_dir("file");
    std::string data(64 * 1024 * 2 + 123, '\0');
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<char>((i * 31) ^ (i >> 8));
    const auto path = dir / "blob.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    const auto got = sha256_file(path);
    ASSERT_TRUE(got.has_value());
    ASSERT_TRUE(*got == sha256(data));

    const auto empty = dir / "empty.bin";
    { std::ofstream out(empty, std::ios::binary); }
    const auto e = sha256_file(empty);
    ASSERT_TRUE(e.has_value());
    ASSERT_EQ(to_hex(*e), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    std::filesystem::remove_all(dir);
}

TEST(sha256_file_has_no_value_for_missing_or_directory) {
    const auto dir = temp_dir("missing");
    ASSERT_TRUE(!sha256_file(dir / "nope.ogg").has_value());   // not the empty-input hash
    ASSERT_TRUE(!sha256_file(dir).has_value());                 // a directory is not a file
    std::filesystem::remove_all(dir);
}
