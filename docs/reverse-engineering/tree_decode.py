#!/usr/bin/env python3
"""Decode Text Twist 2 *.tree word files (pre-order serialized trie).

Grammar: file = node ; node = LETTER* FLAG , then one child node per LETTER (pre-order).
FLAG '0' = path so far is not a word, '1' = path so far is a word.
"""
import sys
from collections import Counter


def decode(data: bytes):
    words = []
    pos = 0
    n = len(data)
    # iterative DFS: stack of (prefix, pending child letters)
    def read_node(prefix):
        nonlocal pos
        start = pos
        while data[pos] not in b"01":
            pos += 1
        letters = data[start:pos].decode("ascii")
        flag = data[pos]
        pos += 1
        if flag == ord("1"):
            words.append(prefix)
        return letters

    stack = [("", iter(read_node("")))]
    while stack:
        prefix, it = stack[-1]
        ch = next(it, None)
        if ch is None:
            stack.pop()
            continue
        p = prefix + ch
        stack.append((p, iter(read_node(p))))
    if pos != n:
        raise ValueError(f"trailing data: consumed {pos} of {n} bytes")
    return words  # pre-order == lexicographic order


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        sys.exit("usage: tree_decode.py [--stats] <file.tree>")
    words = decode(open(args[0], "rb").read())
    if "--stats" in argv:
        lens = Counter(len(w) for w in words)
        print(f"words: {len(words)}")
        print(f"min_len: {min(lens)}  max_len: {max(lens)}")
        for L in sorted(lens):
            print(f"  len {L:2d}: {lens[L]}")
    else:
        sys.stdout.write("".join(w + "\n" for w in sorted(words)))


if __name__ == "__main__":
    main(sys.argv[1:])
