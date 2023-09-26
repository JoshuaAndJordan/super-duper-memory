# CryptoLog
# Copyright (C) 2023 Joshua & Jordan Ogunyinka

cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # We use -std=c++17 instead of -std=gnu++17 in macOS
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(COMMAND cmake_policy)
  # CMP0003: Libraries linked via full path no longer produce linker search paths.
  #cmake_policy(SET CMP0003 NEW)
  if(CMAKE_MAJOR_VERSION GREATER 2)
    # CMP0046: Old behavior to silently ignore non-existent dependencies.
    cmake_policy(SET CMP0046 OLD)
  endif()
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
      "Choose the type of build, options are:
        None Debug Release RelWithDebInfo Profile."
      FORCE)
endif()

# CryptoLog
project(cryptolog C CXX)

# Check repository status
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/spdlog/CMakeLists.txt)
  message(FATAL_ERROR "Your cryptolog repository is incomplete, initialize submodules using:\n  git submodule update --init --recursive")
endif()

find_package(Boost REQUIRED)
if(NOT Boost_FOUND)
  message(FATAL_ERROR "You need to have Boost installed")
else()
  set(Boost_USE_STATIC_LIBS OFF) 
  set(Boost_USE_MULTITHREADED ON)  
  set(Boost_USE_STATIC_RUNTIME OFF)
  include_directories(${Boost_INCLUDE_DIRS})
endif()

# use openSSL
find_package(OpenSSL REQUIRED)
if(NOT OPENSSL_FOUND)
  message(FATAL_ERROR "You need to have OpenSSL installed (e.g. libssl-dev)")
else()
  include_directories(${OPENSSL_INCLUDE_DIR})
endif()

# Select openssl
if(REQUIRE_SSL)
  if(APPLE)
    option(CMAKE_USE_SECTRANSP "enable Apple OS native SSL/TLS" ON)
  endif()
  if(WIN32)
    set(CMAKE_USE_SCHANNEL "enable Windows native SSL/TLS" ON)
  endif()
endif()

######################################################################
# Options (these can be specified in cmake command line or modifying
# CMakeCache.txt)

option(ENABLE_USER_ACCT_MONITOR      "Compile AccountMonitor" on)
option(ENABLE_COMMON_LIB             "Compile CommonLib" on)
option(ENABLE_PRICE_MONITOR          "Compile Crypto price monitor" on)
option(ENABLE_USER_PROCESS_DELEGATOR "Compile user account process delegator" on)
option(ENABLE_BACKEND_DATABASE       "Enable non-sqlite database support" on)
option(ENABLE_MSGPACK_USAGE          "Compile the library with MSGPACK" on)

######################################################################
# Profile build type

list(APPEND CMAKE_BUILD_TYPES Profile)
mark_as_advanced(
    CMAKE_C_FLAGS_PROFILE
    CMAKE_CXX_FLAGS_PROFILE
    CMAKE_EXE_LINKER_FLAGS_PROFILE)

if(CMAKE_COMPILER_IS_GNUCC)
    set(CMAKE_C_FLAGS_PROFILE "-pg"
        CACHE STRING "Profiling C flags")
    set(CMAKE_CXX_FLAGS_PROFILE "${CMAKE_C_FLAGS_PROFILE}"
        CACHE STRING "Profiling C++ flags")
    set(CMAKE_EXE_LINKER_FLAGS_PROFILE "-pg"
        CACHE STRING "Profiling linker flags")
endif()

if(MSVC)
    set(CMAKE_C_FLAGS_PROFILE "/MT /Zi /Ox /Gd"
        CACHE STRING "Profiling C flags")
    set(CMAKE_CXX_FLAGS_PROFILE "${CMAKE_C_FLAGS_PROFILE}"
        CACHE STRING "Profiling C++ flags")
    set(CMAKE_EXE_LINKER_FLAGS_PROFILE "/PROFILE /DEBUG"
        CACHE STRING "Profiling linker flags")
endif()

######################################################################
# Directories

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# We need to specify the output for each configuration to make it work
# on Visual Studio solutions.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_PROFILE "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_PROFILE "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_PROFILE "${CMAKE_BINARY_DIR}/bin")

set(CLI11_DIR           ${CMAKE_CURRENT_SOURCE_DIR}/external/CLI)
set(CPP_JWT             ${CMAKE_CURRENT_SOURCE_DIR}/external/cpp-jwt)
set(NLOHMANN_JSON       ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann)
set(OTL_DIR             ${CMAKE_CURRENT_SOURCE_DIR}/external/otl_v4)
set(SPDLOG_DIR          ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog)

# Search in the "cmake" directory for additional CMake modules.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if(ENABLE_CCACHE)
  find_package(CCache)
  if(CCache_FOUND)
    # Use e.g. "ccache clang++" instead of "clang++"
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE "${CCache_EXECUTABLE}")
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK    "${CCache_EXECUTABLE}")
  endif()
endif()
# Put libraries into "lib".
set(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/lib)

######################################################################
# Common definitions to compile all sources (app code and third party)

# Debug C/C++ flags
if(CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-DDEBUGMODE -D_DEBUG)
else()
  add_definitions(-DNDEBUG)
endif()

if(ENABLE_BACKEND_DATABASE)
  add_definitions(-DCL_USE_WITH_DATABASE)
endif()

if(ENABLE_MSGPACK_USAGE)
  add_definitions(-DCRYPTOLOG_USING_MSGPACK)
endif ()

if(ENABLE_DBUS_USAGE)
  add_definitions(-DCL_USE_WITH_DBUS)
endif()

if (ENABLE_USER_ACCT_MONITOR)
  add_subdirectory(account_monitor)
endif()

if (ENABLE_COMMON_LIB)
  add_subdirectory(common)
endif()

if (ENABLE_PRICE_MONITOR)
  add_subdirectory(price_monitor)
endif()

if (ENABLE_USER_PROCESS_DELEGATOR)
  add_subdirectory(account_process_delegator)
endif()


