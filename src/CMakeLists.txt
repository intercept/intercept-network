cmake_minimum_required (VERSION 3.1)

set(INTERCEPT_OUTPUT_DIR ${PROJECT_SOURCE_DIR})


set(INTERCEPT_NETWORK_NAME "intercept-network" PARENT_SCOPE)
set(INTERCEPT_NETWORK_NAME "intercept-network")

add_definitions(/DZMQ_STATIC)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")

file(GLOB INTERCEPT_NETWORK_SOURCES *.h *.hpp *.c *.cpp)
SOURCE_GROUP("src" FILES ${INTERCEPT_NETWORK_SOURCES})

file(GLOB INTERCEPT_NETWORK_SOURCES_CLIENT "client/*.h" "client/*.hpp" "client/*.c" "client/*.cpp")
SOURCE_GROUP("src/client" FILES ${INTERCEPT_NETWORK_SOURCES_CLIENT})

file(GLOB INTERCEPT_NETWORK_SOURCES_HOST "host/*.h" "host/*.hpp" "host/*.c" "host/*.cpp")
SOURCE_GROUP("src/host" FILES ${INTERCEPT_NETWORK_SOURCES_HOST})

set(library_sources ${INTERCEPT_NETWORK_SOURCES} ${INTERCEPT_NETWORK_SOURCES_CLIENT} ${INTERCEPT_NETWORK_SOURCES_HOST})
list(REMOVE_ITEM library_sources "${CMAKE_SOURCE_DIR}/src/standalone.cpp")

set(standalone_sources ${INTERCEPT_NETWORK_SOURCES} ${INTERCEPT_NETWORK_SOURCES_CLIENT} ${INTERCEPT_NETWORK_SOURCES_HOST})
list(REMOVE_ITEM standalone_sources "${CMAKE_SOURCE_DIR}/src/client_dll.cpp")


include_directories(${CMAKE_CURRENT_SOURCE_DIR} "${CMAKE_SOURCE_DIR}/3rdParty" "${CMAKE_SOURCE_DIR}/3rdParty/cppzmq" "${CMAKE_SOURCE_DIR}/3rdParty/libzmq/include")

add_library(${INTERCEPT_NETWORK_NAME} SHARED)
target_sources(${INTERCEPT_NETWORK_NAME} PUBLIC ${library_sources})
add_executable(standalone)
target_sources(standalone PUBLIC ${standalone_sources})

target_link_libraries(${INTERCEPT_NETWORK_NAME} libzmq-static)
target_link_libraries(standalone libzmq-static)

target_link_libraries (${INTERCEPT_NETWORK_NAME} ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries (standalone ${CMAKE_THREAD_LIBS_INIT})


if (SODIUM_FOUND)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ${SODIUM_LIBRARIES})
target_link_libraries (standalone ${SODIUM_LIBRARIES})
endif ()
if (HAVE_WS2_32)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ws2_32)
target_link_libraries (standalone ws2_32)
elseif (HAVE_WS2)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ws2)
target_link_libraries (standalone ws2)
endif ()

if (HAVE_RPCRT4)
target_link_libraries (${INTERCEPT_NETWORK_NAME} rpcrt4)
target_link_libraries (standalone rpcrt4)
endif ()

if (HAVE_IPHLAPI)
target_link_libraries (${INTERCEPT_NETWORK_NAME} iphlpapi)
target_link_libraries (standalone iphlpapi)
endif ()

if (RT_LIBRARY)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ${RT_LIBRARY})
target_link_libraries (standalone ${RT_LIBRARY})
endif ()

set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES PREFIX "")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES FOLDER intercept-network)
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

target_link_libraries(standalone ${CMAKE_THREAD_LIBS_INIT})
set_target_properties(standalone PROPERTIES PREFIX "")
set_target_properties(standalone PROPERTIES FOLDER intercept-network)
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")


if(CMAKE_COMPILER_IS_GNUCXX)
	set(CMAKE_CXX_FLAGS "-std=c++1z -O2 -s -fPIC -fpermissive -static-libgcc -static-libstdc++")#-march=i686 -m32
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
else()
	set(CMAKE_CXX_FLAGS_DEBUG "/D_DEBUG /Zi /Ob0 /Od /RTC1 /MP /EHsc")
	set(CMAKE_CXX_FLAGS_RELEASE "/Zi /Ox /Ob1 /EHsc /MP") #with debug info
	set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/OPT:REF /DEBUG:FULL") 
	set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "/DEBUG:FULL") # /VERBOSE:lib
    target_compile_options(libzmq-static PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    target_compile_options(standalone PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    target_compile_options(${INTERCEPT_NETWORK_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
endif()


if(CMAKE_COMPILER_IS_GNUCXX)
	set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES LINK_SEARCH_START_STATIC 1)
	set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()