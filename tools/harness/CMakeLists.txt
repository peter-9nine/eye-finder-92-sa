cmake_minimum_required(VERSION 3.21)
project(EyeFinderHarness CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(EyeFinderHarness
  main.cpp
  "${ROOT}/source/BrowserClient.cpp"
  "${ROOT}/source/OverlayRenderer.cpp"
  "${ROOT}/source/PagerRenderer.cpp"
  "${ROOT}/source/MailStore.cpp"
  "${ROOT}/source/KeyConfig.cpp"
  "${ROOT}/source/Locations.cpp")

set(NLOHMANN "${ROOT}/vendor")
target_include_directories(EyeFinderHarness PRIVATE "${ROOT}/include" "${ROOT}/shared" "${ROOT}/api" "${NLOHMANN}")
target_compile_definitions(EyeFinderHarness PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_link_libraries(EyeFinderHarness PRIVATE d3d9)

# An API double with the real name: what a mod sees, without needing the game.
add_library(ApiDouble SHARED "${ROOT}/source/Api.cpp")
set_target_properties(ApiDouble PROPERTIES OUTPUT_NAME "EyeFinder92SA" SUFFIX ".asi")
target_include_directories(ApiDouble PRIVATE "${ROOT}/include")
target_compile_definitions(ApiDouble PRIVATE NOMINMAX EF_API_TEST)
add_dependencies(EyeFinderHarness ApiDouble)

# The example mod.
add_library(EyeFinderApiDemo SHARED "${ROOT}/examples/ApiDemo/main.cpp")
set_target_properties(EyeFinderApiDemo PROPERTIES SUFFIX ".asi")
target_include_directories(EyeFinderApiDemo PRIVATE "${ROOT}/api")
target_compile_definitions(EyeFinderApiDemo PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
