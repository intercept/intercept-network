cmake_minimum_required (VERSION 3.0)

if(NOT INTERCEPT_INCLUDE_PATH)
	set(INTERCEPT_INCLUDE_PATH "${CMAKE_SOURCE_DIR}/intercept/include")
endif()
set(INTERCEPT_OUTPUT_DIR ${PROJECT_SOURCE_DIR})

if(NOT INTERCEPT_LIB_PATH)
    set(INTERCEPT_LIB_PATH "${CMAKE_SOURCE_DIR}/intercept/lib/intercept_client.lib")
endif()

set(INTERCEPT_NETWORK_NAME "intercept-network")

add_definitions(/DINTERCEPT_NO_THREAD_SAFETY)
add_definitions(/DZMQ_STATIC)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")

file(GLOB INTERCEPT_NETWORK_SOURCES *.h *.hpp *.c *.cpp)

SOURCE_GROUP("src" FILES ${INTERCEPT_NETWORK_SOURCES})

set(library_sources ${INTERCEPT_NETWORK_SOURCES})
list(REMOVE_ITEM library_sources "${CMAKE_SOURCE_DIR}/src/standalone.cpp")

add_library( ${INTERCEPT_NETWORK_NAME} SHARED ${library_sources})

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${INTERCEPT_INCLUDE_PATH} "${CMAKE_SOURCE_DIR}/3rdParty" "${CMAKE_SOURCE_DIR}/3rdParty/cppzmq" "${CMAKE_SOURCE_DIR}/3rdParty/libzmq/include")

target_link_libraries(${INTERCEPT_NETWORK_NAME} libzmq-static)


target_link_libraries (${INTERCEPT_NETWORK_NAME} ${CMAKE_THREAD_LIBS_INIT})

if (SODIUM_FOUND)
target_link_libraries (${INTERCEPT_NETWORK_NAME}${SODIUM_LIBRARIES})
endif ()
if (HAVE_WS2_32)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ws2_32)
elseif (HAVE_WS2)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ws2)
endif ()

if (HAVE_RPCRT4)
target_link_libraries (${INTERCEPT_NETWORK_NAME} rpcrt4)
endif ()

if (HAVE_IPHLAPI)
target_link_libraries (${INTERCEPT_NETWORK_NAME} iphlpapi)
endif ()

if (RT_LIBRARY)
target_link_libraries (${INTERCEPT_NETWORK_NAME} ${RT_LIBRARY})
endif ()


target_link_libraries(${INTERCEPT_NETWORK_NAME} ${INTERCEPT_LIB_PATH} ${LINK_LIBS})

target_link_libraries(${INTERCEPT_NETWORK_NAME} ${LINK_LIBS})
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES PREFIX "")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES FOLDER intercept-network)
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")

#set_target_properties(libzmq-static PROPERTIES CXX_FLAGS "${CMAKE_CXX_FLAGS} /MTd")
#set_target_properties(libzmq-static PROPERTIES CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} /MTd")
#set_target_properties(libzmq-static PROPERTIES COMPILE_FLAGS "${CMAKE_CXX_FLAGS} /MTd")
#set_target_properties(libzmq-static PROPERTIES COMPILE_FLAGS_DEBUG "${CMAKE_CXX_FLAGS} /MT")

if(CMAKE_COMPILER_IS_GNUCXX)
	SET(CMAKE_CXX_FLAGS "-std=c++1z -O2 -s -fPIC -fpermissive -static-libgcc -static-libstdc++")#-march=i686 -m32
	set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	set(CMAKE_SHARED_LINKER_FLAGS "-static -static-libgcc -static-libstdc++")
else()
	set(CMAKE_CXX_FLAGS_DEBUG "/D _DEBUG /MTd /Zi /Ob0 /Od /RTC1 /EHsc")
	set(CMAKE_CXX_FLAGS_MINSIZEREL     "/MT /O1 /Ob1 /D /EHsc NDEBUG")
	set(CMAKE_CXX_FLAGS_RELEASE       "/MT /O2 /Ob2 /D /EHsc NDEBUG")
	set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/MT /Zi /O2 /Ob1 /D /EHsc NDEBUG")
	target_compile_options(libzmq-static PRIVATE "/MT$<$<CONFIG:Debug>:d>")
endif()


if(CMAKE_COMPILER_IS_GNUCXX)
	set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES LINK_SEARCH_START_STATIC 1)
	set_target_properties(${INTERCEPT_NETWORK_NAME} PROPERTIES LINK_SEARCH_END_STATIC 1)
endif()

set(standalone_sources ${INTERCEPT_NETWORK_SOURCES})
list(REMOVE_ITEM standalone_sources "${CMAKE_SOURCE_DIR}/src/client_dll.cpp")
add_executable(standalone ${standalone_sources})



target_link_libraries(standalone libzmq-static)


target_link_libraries (standalone ${CMAKE_THREAD_LIBS_INIT})

if (SODIUM_FOUND)
target_link_libraries (standalone ${SODIUM_LIBRARIES})
endif ()
if (HAVE_WS2_32)
target_link_libraries (standalone ws2_32)
elseif (HAVE_WS2)
target_link_libraries (standalone ws2)
endif ()

if (HAVE_RPCRT4)
target_link_libraries (standalone rpcrt4)
endif ()

if (HAVE_IPHLAPI)
target_link_libraries (standalone iphlpapi)
endif ()

if (RT_LIBRARY)
target_link_libraries (standalone ${RT_LIBRARY})
endif ()


target_link_libraries(standalone ${INTERCEPT_LIB_PATH} ${LINK_LIBS} ${CMAKE_THREAD_LIBS_INIT})

target_link_libraries(standalone ${LINK_LIBS})
set_target_properties(standalone PROPERTIES PREFIX "")
set_target_properties(standalone PROPERTIES FOLDER intercept-network)
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")
set_target_properties(standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${INTERCEPT_OUTPUT_DIR}/rv/${INTERCEPT_NETWORK_NAME}/intercept")