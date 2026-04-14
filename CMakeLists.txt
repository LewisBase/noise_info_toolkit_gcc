cmake_minimum_required(VERSION 3.14)
project(noise_info_toolkit_gcc VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_TESTS "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)

# Find packages
find_package(Threads REQUIRED)
find_package(SQLite3 QUIET)  # Optional - not needed for core library

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Core source files (only noise calculation modules)
set(CORE_SOURCES
    src/noise_processor.cpp
    src/dose_calculator.cpp
    src/signal_utils.cpp
    src/iir_filter.cpp
)

# Create library
add_library(noise_toolkit STATIC ${CORE_SOURCES})

target_link_libraries(noise_toolkit
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    # Noise processor unit tests
    add_executable(test_noise_processor tests/test_noise_processor.cpp)
    target_link_libraries(test_noise_processor noise_toolkit)
    add_test(NAME test_noise_processor COMMAND test_noise_processor)
    
    # Dose calculator unit tests (standalone, no library needed)
    add_executable(dose_validator dose_validator.cpp)
    target_link_libraries(dose_validator Threads::Threads)
    add_test(NAME dose_validator COMMAND dose_validator)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_executable(noise_toolkit_example examples/main.cpp)
    target_link_libraries(noise_toolkit_example noise_toolkit)
endif()

# Install
install(TARGETS noise_toolkit
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/ DESTINATION include)
