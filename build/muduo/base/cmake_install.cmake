# Install script for directory: /home/muxin/hdd/muduo_rewrite/muduo/base

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/muxin/hdd/muduo_rewrite/debug-install-cpp11")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/muxin/hdd/muduo_rewrite/build/lib/libmuduo_base.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/muduo/base" TYPE FILE FILES
    "/home/muxin/hdd/muduo_rewrite/muduo/base/AsyncLogging.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Atomic.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/BlockingQueue.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/BoundedBlockingQueue.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/CircularBuffer.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Condition.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/CountDownLatch.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/CurrentThread.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Date.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Exception.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/FileUtil.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/GzipFile.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/LogFile.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/LogStream.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Logging.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Mutex.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/ProcessInfo.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Singleton.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/StringPiece.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Thread.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/ThreadLocal.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/ThreadLocalSingleton.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/ThreadPool.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/TimeZone.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Timestamp.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/Types.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/WeakCallback.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/copyable.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/base/noncopyable.h"
    )
endif()

