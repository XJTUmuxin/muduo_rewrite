# Install script for directory: /home/muxin/hdd/muduo_rewrite/muduo/net

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/home/muxin/hdd/muduo_rewrite/release-install-cpp11")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "release")
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
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/muxin/hdd/muduo_rewrite/build/lib/libmuduo_net.a")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/muduo/net" TYPE FILE FILES
    "/home/muxin/hdd/muduo_rewrite/muduo/net/Buffer.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/Callbacks.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/Channel.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/Endian.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/EventLoop.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/EventLoopThread.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/EventLoopThreadPool.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/InetAddress.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/TcpClient.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/TcpConnection.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/TcpServer.h"
    "/home/muxin/hdd/muduo_rewrite/muduo/net/TimerId.h"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/muxin/hdd/muduo_rewrite/build/muduo/net/http/cmake_install.cmake")
  include("/home/muxin/hdd/muduo_rewrite/build/muduo/net/inspect/cmake_install.cmake")

endif()

