#!/usr/bin/env python3
"""Encode a word list (one per line) into Text Twist 2 *.tree format.

usage: tree_encode.py <words.txt> <out.tree>
Words are lowercased/stripped; duplicates removed. Byte output is canonical
(children in ascending byte order), matching the original game files.
"""
import sys


def encode(words):
    root = {}
    END = None
    for w in words:
        node = root
        for c in w:
            node = node.setdefault(c, {})
        node[END] = True
    out = []
    stack = [root]
    while stack:
        node = stack.pop()
        kids = sorted(k for k in node if k is not END)
        out.append("".join(kids))
        out.append("1" if END in node else "0")
        for k in reversed(kids):
            stack.append(node[k])
    return "".join(out).encode("ascii")


def main(argv):
    if len(argv) != 2:
        sys.exit("usage: tree_encode.py <words.txt> <out.tree>")
    words = {l.strip().lower() for l in open(argv[0], encoding="ascii")}
    words.discard("")
    for w in words:
        if not w.isalpha() or not w.isascii():
            sys.exit(f"invalid word (letters a-z only): {w!r}")
    open(argv[1], "wb").write(encode(words))


if __name__ == "__main__":
    main(sys.argv[1:])
