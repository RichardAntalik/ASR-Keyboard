Always compile and run with timeout before committing
Build by `cmake . && make debug -j 32`
Use `./asr-kb` for testing. Config path is not hardcoded.
When debugging, consider adding debug printfs. It's better to iterate over real data than to endlessly think ooh its' maybe this, but wait, maybe that... 
