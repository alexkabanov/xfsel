cmake_minimum_required(VERSION 3.5)

project(xfsel LANGUAGES C)

find_package(PkgConfig REQUIRED)

pkg_check_modules(X11 REQUIRED x11)
include_directories(${X11_INCLUDE_DIRS})
link_directories(${X11_LIBRARY_DIRS})

add_executable(xfsel main.c)

target_link_libraries(xfsel ${X11_LIBRARIES})

install(TARGETS xfsel DESTINATION bin)
