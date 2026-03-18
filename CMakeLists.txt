cmake_minimum_required(VERSION 3.14)
project(noise_info_toolkit VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# Find packages
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Source files
set(CORE_SOURCES
    src/audio_processor.cpp
    src/dose_calculator.cpp
    src/event_detector.cpp
    src/event_processor.cpp
    src/tdms_converter.cpp
    src/time_history_processor.cpp
    src/ring_buffer.cpp
    src/database.cpp
    src/signal_utils.cpp
    src/wav_reader.cpp
    src/iir_filter.cpp
)

# Create library
if(BUILD_SHARED_LIBS)
    add_library(noise_toolkit SHARED ${CORE_SOURCES})
else()
    add_library(noise_toolkit STATIC ${CORE_SOURCES})
endif()

target_link_libraries(noise_toolkit
    Threads::Threads
    SQLite::SQLite3
    ${CMAKE_DL_LIBS}
)

# Example executable
add_executable(noise_toolkit_example examples/main.cpp)
target_link_libraries(noise_toolkit_example noise_toolkit)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS noise_toolkit
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
