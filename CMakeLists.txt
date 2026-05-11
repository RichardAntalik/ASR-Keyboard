cmake_minimum_required(VERSION 3.10)
project(ASR_Keyboard_Tool)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Werror)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# 1. Find the required packages
find_package(X11 REQUIRED)
find_library(XEXT_LIB Xext REQUIRED)
find_library(XI_LIB Xi REQUIRED)
find_package(CURL REQUIRED)
# ALSA and Xtst don't always have "Find" modules, so we find them as libraries
find_library(ALSA_LIB asound REQUIRED)
find_library(XTST_LIB Xtst REQUIRED)
find_library(PULSE_LIB pulse REQUIRED)

# 2. Define the executable
add_executable(asr-kb src/main.cpp src/pulse-recording.cpp src/keyboard-sim.cpp src/config-parsing.cpp)
target_include_directories(asr-kb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 3. Link everything together
target_link_libraries(asr-kb 
    ${X11_LIBRARIES} 
    ${XEXT_LIB} 
    ${XI_LIB} 
    ${CURL_LIBRARIES} 
    ${ALSA_LIB} 
    ${XTST_LIB} 
    ${PULSE_LIB} 
    pthread
    asan
)

# 4. Create an "install" command
install(TARGETS asr-kb DESTINATION /usr/local/bin)