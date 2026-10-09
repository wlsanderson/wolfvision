cmake_minimum_required(VERSION 3.16)
project(wolfvision LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# clang-tidy uses the real C++17 flags and include paths from this database.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(common src/common/clock.cpp src/common/udp.cpp)
target_include_directories(common PUBLIC src)
target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic)

add_library(telemetry src/telemetry/protocol.cpp src/telemetry/measurement.cpp)
target_include_directories(telemetry PUBLIC src)
target_compile_options(telemetry PRIVATE -Wall -Wextra -Wpedantic)

add_library(simulation src/simulation/protocol.cpp src/simulation/sources.cpp
  src/simulation/reassembly.cpp src/simulation/options.cpp)
target_link_libraries(simulation PUBLIC common telemetry)
target_compile_options(simulation PRIVATE -Wall -Wextra -Wpedantic)

foreach(app simulation_tx simulation_rx)
  add_executable(${app} src/${app}.cpp)
  target_link_libraries(${app} PRIVATE simulation)
  target_compile_options(${app} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

# CTest stays fast and independent of Python, FFmpeg, and socket permissions.
# Run host integration explicitly with tools/simulation_smoke.py through uv.
include(CTest)
if(BUILD_TESTING)
  foreach(module common telemetry simulation)
    add_executable(${module}_tests tests/${module}_tests.cpp)
    target_link_libraries(${module}_tests PRIVATE ${module})
    target_compile_options(${module}_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${module}_tests COMMAND ${module}_tests)
  endforeach()
endif()
