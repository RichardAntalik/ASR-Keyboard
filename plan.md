1       CMakeLists.txt  Remove pthread from target_link_libraries
2       pulse-recording.cpp:23  strncpy(source_name, ...) → source_name = ... (needs source_name as std::string)
3       pulse-recording.cpp:58-61       Index-based volume loop → range-based for
4       pulse-recording.cpp:101-104     Index-based sleep loop → for (int i=0; i<15; i++) is fine, but usleep → std::this_thread::sleep_for
5       pulse-recording.cpp:116 void* record_thread(void*) → void record_thread() or std::future pattern
6       client.cpp:117  (char*)malloc(jlen) → std::vector<char> or std::string
7       screen-manager.cpp:23-28        Index-based output_shift_down → std::rotate or range-based
8       screen-manager.cpp:33-46        Index-based output_render → range-based
9       screen-manager.cpp:131-134      Index-based loop → range-based
10      screen-manager.cpp:64-86        build_shortcut_line uses idx param → pass by reference
