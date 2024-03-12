# CryptoLog
# Copyright (C) 2023-2024 Joshua & Jordan Ogunyinka

cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # We use -std=c++17 instead of -std=gnu++17 in macOS
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(SDBUS_CPP_XML_GEN "/usr/local/bin/sdbus-c++-xml2cpp")
set(SDBUS_CXXXML_GEN_PATH "")

if (NOT EXISTS "${SDBUS_CPP_XML_GEN}")
  if (EXISTS "/usr/bin/sdbus-c++-xml2cpp")
    set(SDBUS_CXXXML_GEN_PATH "/usr/bin/sdbus-c++-xml2cpp")
  endif ()
else ()
  set(SDBUS_CXXXML_GEN_PATH "${SDBUS_CPP_XML_GEN}")
endif ()

if (NOT "${SDBUS_CXXXML_GEN_PATH}" STREQUAL "")
  set(COMMON_DBUS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/common/dbus")
  set(COMMON_DBUS_INCLUDE "${COMMON_DBUS_PATH}/base")

  add_custom_command(
    OUTPUT ${COMMON_DBUS_INCLUDE}/telegram_adaptor_server.hpp
          ${COMMON_DBUS_INCLUDE}/telegram_proxy_client.hpp
    COMMAND ${SDBUS_CXXXML_GEN_PATH} --verbose
      --adaptor=${COMMON_DBUS_INCLUDE}/telegram_adaptor_server.hpp
      --proxy=${COMMON_DBUS_INCLUDE}/telegram_proxy_client.hpp
    ${COMMON_DBUS_PATH}/xml/telegram_dbus_interface.xml
    DEPENDS ${COMMON_DBUS_PATH}/xml/telegram_dbus_interface.xml
    VERBATIM)
  add_custom_command(
    OUTPUT ${COMMON_DBUS_INCLUDE}/time_proxy_client.hpp
          ${COMMON_DBUS_INCLUDE}/time_adaptor_server.hpp
    COMMAND ${SDBUS_CXXXML_GEN_PATH} --verbose
          --proxy=${COMMON_DBUS_INCLUDE}/time_proxy_client.hpp
          --adaptor=${COMMON_DBUS_INCLUDE}/time_adaptor_server.hpp
          ${COMMON_DBUS_PATH}/xml/time_dbus_interfaces.xml
    DEPENDS ${COMMON_DBUS_PATH}/xml/time_dbus_interfaces.xml
    VERBATIM)

  add_custom_command(
    OUTPUT ${COMMON_DBUS_INCLUDE}/progress_proxy_client.hpp
    ${COMMON_DBUS_INCLUDE}/progress_adaptor_server.hpp
    COMMAND ${SDBUS_CXXXML_GEN_PATH} --verbose
    --proxy=${COMMON_DBUS_INCLUDE}/progress_proxy_client.hpp
    --adaptor=${COMMON_DBUS_INCLUDE}/progress_adaptor_server.hpp
    ${COMMON_DBUS_PATH}/xml/progress_dbus_interfaces.xml
    DEPENDS ${COMMON_DBUS_PATH}/xml/progress_dbus_interfaces.xml
    VERBATIM)

  add_custom_command(
    OUTPUT ${COMMON_DBUS_INCLUDE}/price_task_result_client.hpp
    ${COMMON_DBUS_INCLUDE}/price_task_result_server.hpp
    COMMAND ${SDBUS_CXXXML_GEN_PATH} --verbose
    --proxy=${COMMON_DBUS_INCLUDE}/price_task_result_client.hpp
    --adaptor=${COMMON_DBUS_INCLUDE}/price_task_result_server.hpp
    ${COMMON_DBUS_PATH}/xml/price_result_stream_interface.xml
    DEPENDS ${COMMON_DBUS_PATH}/xml/price_result_stream_interface.xml
    VERBATIM)

  add_custom_target(DbusGeneration ALL
    DEPENDS ${COMMON_DBUS_INCLUDE}/time_proxy_client.hpp
    ${COMMON_DBUS_INCLUDE}/time_adaptor_server.hpp
    ${COMMON_DBUS_INCLUDE}/progress_adaptor_server.hpp
    ${COMMON_DBUS_INCLUDE}/progress_proxy_client.hpp
    ${COMMON_DBUS_INCLUDE}/price_task_result_client.hpp
    ${COMMON_DBUS_INCLUDE}/price_task_result_server.hpp
    ${COMMON_DBUS_INCLUDE}/telegram_adaptor_server.hpp
    ${COMMON_DBUS_INCLUDE}/telegram_proxy_client.hpp
  )
endif ()

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

find_package(sdbus-c++ REQUIRED)
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
option(ENABLE_PRICE_RESULT_STREAM    "Compile price result streamer" on)
option(ENABLE_ACCOUNT_DISSEMINATOR   "Compile account disseminator" on)
option(ENABLE_HTTP_STREAM            "Compile http interface for interaction" on)
option(ENABLE_BACKEND_DATABASE       "Enable non-sqlite database support" on)
option(ENABLE_MSGPACK_USAGE          "Compile the library with MSGPACK" on)
option(ENABLE_TIME_TASKS             "Compile the library with the time tasks" on)
option(ENABLE_PROGRESS_TASKS         "Compile the library with the progress tasks" on)
option(ENABLE_TELEGRAM_CLIENT        "Compile with telegram client" on)

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

if (ENABLE_HTTP_STREAM)
  add_subdirectory(http_stream)
endif()

if (ENABLE_PRICE_RESULT_STREAM)
  add_subdirectory(price_result_stream)
endif()

if (ENABLE_ACCOUNT_DISSEMINATOR)
  add_subdirectory(account_tasks)
endif()

if (ENABLE_TIME_TASKS)
  add_subdirectory(time_tasks)
endif ()

if (ENABLE_PROGRESS_TASKS)
  add_subdirectory(progress_tasks)
endif ()

if(ENABLE_TELEGRAM_CLIENT)
  add_subdirectory(telegram_client)
endif ()
