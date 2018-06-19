cmake_minimum_required (VERSION 3.6)
project (Intercept-Network)

find_package (Threads)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/CMakeModules)
include(${PROJECT_SOURCE_DIR}/CMakeModules/cxx_compiler_functions.cmake)

option(USE_STATIC_LINKING "USE_STATIC_LINKING" ON)
#This setting enables the use of the engine string type instead of converting to std::string.
#Enabling this results in better performance when handling strings to SQF commands.
option(USE_ENGINE_TYPES "USE_ENGINE_TYPES" ON)

option(USE_64BIT_BUILD "USE_64BIT_BUILD" OFF)
set(INTERCEPT_LINK_TYPE "static")

if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "8")
	set(USE_64BIT_BUILD ON)
endif()

message("GENERATOR USED: '${CMAKE_GENERATOR}'")
message("COMPILER USED: '${CMAKE_CXX_COMPILER_ID}'")

set(CMAKE_CL_64 ${USE_64BIT_BUILD})

if(USE_64BIT_BUILD)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/win64/")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/build/win32/")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INCLUDE_CURRENT_DIR ON) 
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)


set (ZMQ_BUILD_TESTS OFF CACHE BOOL "No ZMQ tests" FORCE)
set (BUILD_TESTS OFF CACHE BOOL "No ZMQ tests" FORCE)

add_subdirectory(3rdParty/libzmq)
add_subdirectory(src)

set_property(GLOBAL PROPERTY INTERCEPT_CLIENT_TARGET ${INTERCEPT_NETWORK_NAME})
include(3rdParty/intercept/CMakeInclude.txt)