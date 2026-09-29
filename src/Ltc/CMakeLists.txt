cmake_minimum_required(VERSION 3.16)
project(ltc VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
include(GNUInstallDirs)

set(LIBLTC_DIR ${CMAKE_SOURCE_DIR}/../lib/libltc/src)

qt_add_library(ltc
    STATIC
    LtcDevice.cpp LtcDevice.h
    Shared.h
    ${LIBLTC_DIR}/ltc.c
    ${LIBLTC_DIR}/decoder.c
    ${LIBLTC_DIR}/encoder.c
    ${LIBLTC_DIR}/timecode.c
)

target_include_directories(ltc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBLTC_DIR}
)

target_compile_definitions(ltc PUBLIC
    LTC_LIBRARY
)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia)

target_link_libraries(ltc PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Multimedia
)
