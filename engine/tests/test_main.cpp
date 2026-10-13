// Test runner. The test functions and the `TEST(name) { ... }` macro
// live in `test_harness.hpp`; each `engine/tests/test_*.cpp` file
// includes the harness and registers its own tests with `TEST(name)`.
// This file provides only `main()`.

#include "test_harness.hpp"

#include "tt2/random.hpp"
#include "tt2/word_tree.hpp"
#include "tt2/bag.hpp"
#include "tt2/board.hpp"
#include "tt2/scoring.hpp"
#include "tt2/round_classic.hpp"
#include "tt2/round_lightning.hpp"
#include "tt2/round_crossword.hpp"

TEST(sanity) {
    ASSERT_EQ(1 + 1, 2);
    ASSERT_TRUE(true);
    ASSERT_NE(1, 2);
}

int main() {
    int passed = 0, failed = 0;
    for (const auto& t : tt2_test::registry()) {
        try {
            t.fn();
            std::printf("ok   %s\n", t.name);
            ++passed;
        } catch (const tt2_test::AssertionFailure& e) {
            std::printf("FAIL %s\n  %s\n", t.name, e.what());
            ++failed;
        } catch (const std::exception& e) {
            std::printf("FAIL %s\n  unexpected exception: %s\n", t.name, e.what());
            ++failed;
        }
    }
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
