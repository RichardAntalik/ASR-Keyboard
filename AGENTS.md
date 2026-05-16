Always compile and run with timeout before committing

Build by `cmake . && make -j 32`

Use `./asr-kb` for testing. Config path is not hardcoded.

For debugging segfaults: add `-fsanitize=address` to CMakeLists.txt C_FLAGS and CXX_FLAGS, rebuild, and run the binary to get ASan crash report.

