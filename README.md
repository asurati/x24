# x24
This program, number x24, is a C Preprocessor, implemented in C.

### License
It is distributed under the
[GPL-3.0-or-later](https://spdx.org/licenses/GPL-3.0-or-later.html) license.

### Features
x24 is a work-in-progress. It aims to conform to the C2x standard.

At present, it is able to scan those GCC and system headers that it itself
includes in its source, and the headers that are included recursively by them.

### Notes
- A non-standard macro, `#define __x86_64__ 1`, was required as one of the
  predefined macros that x24 defines. Without it, the preprocessor attempts to
  include `gnu/stubs-32.h` and fails, as the header isn't present on my
  machine. At the moment, this is the only non-standard macro that was
  required. Of course, if the preprocessor is not subjected to scanning the GCC
  headers, it doesn't need to define such macros. Nevertheless, the GCC and the
  system headers provide a good test-case for testing the preprocessor.
