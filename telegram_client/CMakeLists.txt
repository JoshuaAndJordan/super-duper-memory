# Telegram messaging
# Copyright(C) 2024 Joshua and Jordan Ogunyinka

cmake_minimum_required(VERSION 3.6.0 FATAL_ERROR)

#Project
get_filename_component(PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
set(PROJECT_NAME telegram_messaging)

## load in pkg-config support
find_package(PkgConfig)
find_package(sdbus-c++ REQUIRED)
find_package(Td 1.8.0 REQUIRED)
find_package(Boost REQUIRED)
if(NOT Boost_FOUND)
    message(FATAL_ERROR "You need to have Boost installed")
else()
    set(Boost_USE_STATIC_LIBS OFF)
    set(Boost_USE_MULTITHREADED ON)
    set(Boost_USE_STATIC_RUNTIME OFF)
    include_directories(${Boost_INCLUDE_DIRS})
    link_directories(${Boost_LIBRARY_DIRS})
endif()

include_directories(${PROJECT_DIR}/include)
include_directories(${PROJECT_DIR}/../common)
include_directories(${PROJECT_DIR}/../common/include)
include_directories(${PROJECT_DIR}/../external)
include_directories(${PROJECT_DIR}/../external/spdlog/include)

if(WIN32)
    #set stuff for windows
else()
    #set stuff for other systems
    link_directories(/usr/lib)
    link_libraries(stdc++fs pthread Td::TdStatic)
endif()

#Outputs
set(OUTPUT_DEBUG ${PROJECT_DIR}/bin)
set(OUTPUT_RELEASE ${PROJECT_DIR}/bin)

project(${PROJECT_NAME} CXX)

#Define Release by default.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
    message(STATUS "Build type not specified: Use Release by default.")
endif(NOT CMAKE_BUILD_TYPE)

############## Artifacts Output ############################
#Defines outputs, depending BUILD TYPE #
############################################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DEBUG}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DEBUG}")
    set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${OUTPUT_DEBUG}")
else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_RELEASE}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_RELEASE}")
    set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${OUTPUT_RELEASE}")
endif()

#Messages
message("${PROJECT_NAME}: MAIN PROJECT: ${CMAKE_PROJECT_NAME}")
message("${PROJECT_NAME}: CURR PROJECT: ${CMAKE_CURRENT_SOURCE_DIR}")
message("${PROJECT_NAME}: CURR BIN DIR: ${CMAKE_CURRENT_BINARY_DIR}")

############### Files & Targets ############################
#Files of project and target to build #
############################################################

#Source Files
set(SRC_FILES main.cpp)

#Header Files
set(HEADERS_FILES include/telegram_adaptor_server_impl.hpp)

source_group("Headers" FILES ${HEADERS_FILES})
source_group("Sources" FILES ${SRC_FILES})

# Add executable to build.
add_executable(${PROJECT_NAME} ${SRC_FILES} ${HEADERS_FILES})
target_link_libraries(${PROJECT_NAME} SDBusCpp::sdbus-c++)

if(NOT MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -std=c++17 -O3")
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
    endif()
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++17 /O2")
endif()

#Preprocessor definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            -D_DEBUG
            -D_CONSOLE
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE  /W3 /MD /Od /Zi /EHsc /std:c++17)
    endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_definitions(${PROJECT_NAME} PRIVATE
            -DNDEBUG
            -D_CONSOLE
    )
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE  /W3 /GL /Oi /Gy /Zi /EHsc /std:c++17)
    endif()
endif()
