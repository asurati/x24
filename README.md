# x24
This program, x24, aims to be a C compiler. It is implemented in C.

### License
It is distributed under the
[GPL-3.0-or-later](https://spdx.org/licenses/GPL-3.0-or-later.html) license.

### Features
x24 is a work-in-progress. It aims to conform to the C2x standard.

Its preprocessor is able to scan those libc and system headers that it itself
includes in its source, and the headers that are included recursively by them.

It builds a parse tree out of the tokens, to fit the C grammar. It then prints
the parse tree in the form of LISP-like lists.

### Notes
- A non-standard macro, `#define __x86_64__ 1`, was required as one of the
  predefined macros that x24 defines. Without it, the preprocessor attempts to
  include `gnu/stubs-32.h` and fails, as the header isn't present on my
  machine. Of course, if the preprocessor is not subjected to scanning the
  libc headers, it doesn't need to define such macros. Nevertheless, these
  headers provide a good test-case for testing the preprocessor.

- Another non-standard macro, `#define __STRICT_ANSI__ 1`, was also defined to
  avoid bringing in non-standard constructs from the glibc headers.

- The parser relies on the Earley recognizer to generate the item-sets. For the
  items that get added into the item-set because of a reduce-item, it maintains
  a back-edge towards the reduce-item. Once the item-sets are generated, it
  backtracks over them, using the back-edges, to build the parse-tree.
