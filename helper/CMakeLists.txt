cmake_minimum_required(VERSION 3.21)
set(CMAKE_CONFIGURATION_TYPES Release)
project(EyeFinderBrowser)

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/cef")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

# El sandbox de Chromium está desactivado en el prototipo. Hay que activarlo antes de publicar
# con acceso real a internet.
set(USE_SANDBOX OFF)

find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

set(CEF_TARGET EyeFinderBrowser)
ADD_LOGICAL_TARGET("libcef_lib" "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

add_executable(${CEF_TARGET} WIN32 main.cpp compatibility.manifest)
SET_EXECUTABLE_TARGET_PROPERTIES(${CEF_TARGET})
target_include_directories(${CEF_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../shared")
add_dependencies(${CEF_TARGET} libcef_dll_wrapper)
target_link_libraries(${CEF_TARGET} libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

COPY_FILES("${CEF_TARGET}" "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES("${CEF_TARGET}" "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

# Se vacían antes de copiar: un archivo viejo llegaría a modloader, y este ejecuta cualquier .cs que encuentre.
add_custom_command(TARGET ${CEF_TARGET} POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${CEF_TARGET_OUT_DIR}/ui" "${CEF_TARGET_OUT_DIR}/sites"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../ui" "${CEF_TARGET_OUT_DIR}/ui"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/../sites" "${CEF_TARGET_OUT_DIR}/sites")
