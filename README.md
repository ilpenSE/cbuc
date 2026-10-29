# cbuc - C Builds C

- This single-header library aims to build C without any auxiliary programs like CMake, Makefiles, Shell/Batch/Powershell scripts etc.
- `cbuc.h` is so called stb-style single header library. Include it in build script. Then define `CBUC_IMPLEMENTATION` or `CBUC_IMPL` or `CBUC` macro.
- All you have to do, first bootstrap build system by compiling main build script like `cc -o cbuc cbuc.c`

# About

- Made by [ilpeN](https://github.com/ilpenSE)
- License: MIT
