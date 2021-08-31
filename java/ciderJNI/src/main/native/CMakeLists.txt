# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

cmake_minimum_required(VERSION 3.12)

project(ciderjni)

set(CMAKE_CXX_STANDARD 17)


find_package(JNI REQUIRED)
include_directories(${JAVA_INCLUDE_PATH})
include_directories(${JAVA_INCLUDE_PATH2})
if(JNI_FOUND)
    message(STATUS "JNI_INCLUDE_DIRS = ${JNI_INCLUDE_DIRS}")
    message(STATUS "JNI_LIBRARIES = ${JNI_LIBRARIES}")
else()
    message(WARNING "Could not find JNI")
endif()

add_compile_options("-I$ENV{JAVA_HOME}/include/")
if(WIN32)
    add_compile_options("-I$ENV{JAVA_HOME}/include/win32")
elseif(APPLE)
    add_compile_options("-I$ENV{JAVA_HOME}/include/darwin")
else() # linux
    add_compile_options("-I$ENV{JAVA_HOME}/include/linux")
endif()

file(GLOB JNI_LIB_SRC
    com_mapd_CiderJNI.cpp)

add_library(ciderjni SHARED ${JNI_LIB_SRC})
target_link_libraries(ciderjni DBEngine mapd_thrift thrift_handler Catalog QueryState Parser QueryEngine QueryRunner DataMgr Logger ${Arrow_LIBRARIES} ${Boost_LIBRARIES} ${CMAKE_DL_LIBS} ${TBB_LIBS} Geospatial)

