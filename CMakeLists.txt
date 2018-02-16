cmake_minimum_required(VERSION 3.1)
project(hello_pipline_c)

INCLUDE(FindPkgConfig)

pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST-APP REQUIRED gstreamer-app-1.0)
include_directories(${GST_INCLUDE_DIRS} ${GST-APP_INCLUDE_DIRS})
link_directories(${GST_LIBRARY_DIRS} ${GST-APP_LIBRARY_DIRS})

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} ${GST_LIBRARIES} ${GST-APP_LIBRARIES} Threads::Threads)
