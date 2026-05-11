Always compile and run with timeout before committing

Use `./asr-kb -i 1 -c config.json` for testing. Config path is not hardcoded.

For debugging segfaults: add `-fsanitize=address` to CMakeLists.txt C_FLAGS and CXX_FLAGS, rebuild, and run the binary to get ASan crash report.

No compile warnings — treat all compiler warnings as errors.