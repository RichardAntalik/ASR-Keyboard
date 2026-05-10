cmake_minimum_required(VERSION 3.10)
project(ASR_Keyboard_Tool)

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
add_executable(asr_kb asr-kb.c)

# 3. Link everything together
target_link_libraries(asr_kb 
    ${X11_LIBRARIES} 
    ${XEXT_LIB} 
    ${XI_LIB} 
    ${CURL_LIBRARIES} 
    ${ALSA_LIB} 
    ${XTST_LIB} 
    ${PULSE_LIB} 
    pthread
)

# 4. Create an "install" command
install(TARGETS asr_kb DESTINATION /usr/local/bin)