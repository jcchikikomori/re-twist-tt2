#pragma once

// Text Twist 2 `.tree` dictionary format
// ======================================
//
// A `.tree` file is a pre-order serialized trie in plain ASCII, with no
// header, no length, no checksum and no trailing newline.
//
//     Node := [a-z]* ('0' | '1') Node*
//
// Each node writes its sorted child letters, then a flag ('1' = the path
// to this node is a complete word, '0' = it isn't), then each child
// subtree depth-first in the same order. The pre-order walk enumerates
// the words in alphabetical order; the count of `'1'` bytes is the
// dictionary size.
//
// The two files shipped with the game are
//   * data/TextTwist2Words.tree  (common / rack, ~25 004 words)
//   * data/DamBeaversWords.tree  (full    / guess validator, ~116 757 words)
// and one file is a strict subset of the other.
//
// Round-tripping a file through `decode → encode` is byte-identical: the
// format is canonical (children sorted, no trailing data). That
// property is what makes the dictionary updateable from a flat word
// list.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tt2 {

class TreeFormatError : public std::runtime_error {
public:
    TreeFormatError(std::string msg, std::size_t offset)
        : std::runtime_error(std::move(msg) + " at byte " + std::to_string(offset)),
          offset_(offset) {}
    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

class WordTree {
public:
    static WordTree parse(std::string_view bytes) {
        Parser p{bytes};
        WordTree t;
        t.root_ = p.read_node();
        if (p.pos != bytes.size()) {
            throw TreeFormatError("trailing data after root", p.pos);
        }
        t.count_ = p.word_count;
        return t;
    }

    // Build a canonical tree from a word list. Duplicates are ignored.
    // Words that contain anything outside `a..z` raise
    // `std::invalid_argument`.
    static WordTree from_words(const std::vector<std::string>& words) {
        WordTree t;
        for (const auto& w : words) {
            if (w.empty() || !is_tree_word(w)) {
                throw std::invalid_argument(
                    "from_words: invalid word: \"" + w + "\"");
            }
            Node* cur = &t.root_;
            for (char ch : w) {
                auto it = cur->find(static_cast<std::uint8_t>(ch));
                if (it == cur->kids.end()) {
                    cur->kids.push_back(Node::make(static_cast<std::uint8_t>(ch)));
                    std::sort(cur->kids.begin(), cur->kids.end(),
                              [](const Node& a, const Node& b) {
                                  return a.letter < b.letter;
                              });
                    it = cur->find(static_cast<std::uint8_t>(ch));
                }
                cur = &*it;  // iterator's value is the Node itself
            }
            if (!cur->end) {
                cur->end = true;
                ++t.count_;
            }
        }
        return t;
    }

    std::size_t size() const noexcept { return count_; }

    bool contains(std::string_view word) const noexcept {
        const Node* cur = &root_;
        for (char ch : word) {
            auto it = cur->find(static_cast<std::uint8_t>(ch));
            if (it == cur->kids.end()) return false;
            cur = &*it;
        }
        return !word.empty() && cur->end;
    }

    // All words, in pre-order (alphabetical for a canonical tree).
    std::vector<std::string> words() const {
        std::vector<std::string> out;
        out.reserve(count_);
        std::string buf;
        walk(&root_, buf, out);
        return out;
    }

    std::vector<std::string> words_by_length(std::size_t min_len,
                                             std::size_t max_len) const {
        auto all = words();
        std::vector<std::string> out;
        out.reserve(all.size() / 4);
        for (const auto& w : all) {
            if (w.size() >= min_len && w.size() <= max_len) out.push_back(w);
        }
        return out;
    }

    // Every dictionary word that can be spelled from `rack`
    // (multiset-aware: each rack letter is used at most once). Mirrors
    // the recursive walk at 0x43dd80 in the original: a DFS in
    // rack-letter order that **skips letters already tried at the same
    // depth**, so each word is reported exactly once. The output is in
    // DFS order, not sorted.
    std::vector<std::string> sub_anagrams(std::string_view rack) const {
        std::vector<std::string> out;
        std::string rack_owned(rack);
        std::string prefix;
        prefix.reserve(rack.size());
        walk_anagrams(&root_, prefix, rack_owned, out);
        return out;
    }

    // Serialize back to `.tree` bytes. Byte-identical to the input
    // when the input came from a canonical `encode`.
    std::string serialize() const {
        std::string out;
        out.reserve(count_ * 6);  // heuristic
        write(&root_, out);
        return out;
    }

    static bool is_tree_word(std::string_view w) noexcept {
        if (w.empty()) return false;
        for (char c : w) if (c < 'a' || c > 'z') return false;
        return true;
    }

private:
    struct Node {
        std::uint8_t letter = 0;  // ignored for the root
        bool end = false;
        std::vector<Node> kids;

        static Node make(std::uint8_t l) {
            Node n;
            n.letter = l;
            return n;
        }

        std::vector<Node>::iterator find(std::uint8_t l) {
            for (auto it = kids.begin(); it != kids.end(); ++it) {
                if (it->letter == l) return it;
            }
            return kids.end();
        }
        std::vector<Node>::const_iterator find(std::uint8_t l) const {
            for (auto it = kids.begin(); it != kids.end(); ++it) {
                if (it->letter == l) return it;
            }
            return kids.end();
        }
    };

    Node root_;
    std::size_t count_ = 0;

    // Node := [a-z]* ('0'|'1') Node* — all child letters first, then the
    // flag, then every child's subtree in the same order (CONFIRMED at
    // 0x43e070, see docs/reverse-engineering/tree-format.md). Children
    // are kept sorted, which is what makes the encoding canonical.
    static void write(const Node* n, std::string& out) {
        for (const auto& k : n->kids) out.push_back(static_cast<char>(k.letter));
        out.push_back(n->end ? '1' : '0');
        for (const auto& k : n->kids) write(&k, out);
    }

    static void walk(const Node* n, std::string& buf,
                     std::vector<std::string>& out) {
        if (n->end) out.push_back(buf);
        for (const auto& k : n->kids) {
            buf.push_back(static_cast<char>(k.letter));
            walk(&k, buf, out);
            buf.pop_back();
        }
    }

    // Mirrors the original 0x43dd80: at each depth, try each remaining
    // letter; if it's already been tried at this depth, skip (so
    // "letter,letter,..." paths visit each letter once).
    void walk_anagrams(const Node* n, std::string& prefix, std::string& rack,
                       std::vector<std::string>& out) const {
        for (std::size_t i = 0; i < rack.size(); ++i) {
            const char ch = rack[i];
            if (rack.find(ch) != i) continue;  // duplicate letter at this depth
            auto it = n->find(static_cast<std::uint8_t>(ch));
            if (it == n->kids.end()) continue;
            prefix.push_back(ch);
            if (it->end) out.push_back(prefix);
            if (!it->kids.empty()) {
                std::string next_rack;
                next_rack.reserve(rack.size() - 1);
                next_rack.append(rack, 0, i);
                next_rack.append(rack, i + 1, std::string::npos);
                walk_anagrams(&*it, prefix, next_rack, out);
            }
            prefix.pop_back();
        }
    }

    struct Parser {
        std::string_view bytes;
        std::size_t pos = 0;
        std::size_t word_count = 0;

        explicit Parser(std::string_view b) : bytes(b) {}

        // One node: its child letters, its flag, then the children
        // (recursively) in letter order. Mirrors 0x43e070.
        Node read_node() {
            Node n;
            std::vector<std::uint8_t> letters;
            for (;;) {
                if (pos >= bytes.size()) {
                    throw TreeFormatError("unexpected end of data", pos);
                }
                const std::uint8_t b = static_cast<std::uint8_t>(bytes[pos++]);
                if (b == '0' || b == '1') {
                    n.end = (b == '1');
                    if (n.end) ++word_count;
                    break;
                }
                if (b < 'a' || b > 'z') {
                    throw TreeFormatError("invalid byte 0x" + hex(b), pos - 1);
                }
                if (!letters.empty() && b <= letters.back()) {
                    throw TreeFormatError("child letters not strictly ascending", pos - 1);
                }
                letters.push_back(b);
            }
            n.kids.reserve(letters.size());
            for (std::uint8_t l : letters) {
                Node child = read_node();
                child.letter = l;
                n.kids.push_back(std::move(child));
            }
            return n;
        }

        static std::string hex(std::uint8_t b) {
            static const char* d = "0123456789abcdef";
            std::string s(2, '0');
            s[0] = d[(b >> 4) & 0xF];
            s[1] = d[b & 0xF];
            return s;
        }
    };
};

// True when every letter of `word` is in `letters`, each letter of
// `letters` used at most once (a letter-count check, order ignored).
// `word` must be a..z only: uppercase or any other byte makes it false.
// Bytes of `letters` outside a..z are ignored. An empty word is
// spellable from anything; callers that need a non-empty word check
// that first. The one copy of this rule: Board's rack check, the
// Crossword round and the Crossword save check all call it.
inline bool spellable_from(std::string_view word, std::string_view letters) noexcept {
    if (word.size() > letters.size()) return false;
    std::array<int, 26> left{};
    for (char c : letters) {
        if (c >= 'a' && c <= 'z') ++left[static_cast<std::size_t>(c - 'a')];
    }
    for (char c : word) {
        if (c < 'a' || c > 'z' || --left[static_cast<std::size_t>(c - 'a')] < 0) return false;
    }
    return true;
}

}  // namespace tt2
