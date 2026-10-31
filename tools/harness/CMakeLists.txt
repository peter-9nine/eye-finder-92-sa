cmake_minimum_required(VERSION 3.21)
project(EyeFinderHarness CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(EyeFinderHarness
  main.cpp
  "${ROOT}/source/BrowserClient.cpp"
  "${ROOT}/source/OverlayRenderer.cpp")

target_include_directories(EyeFinderHarness PRIVATE "${ROOT}/include" "${ROOT}/shared")
target_compile_definitions(EyeFinderHarness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(EyeFinderHarness PRIVATE d3d9)
