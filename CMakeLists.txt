cmake_minimum_required(VERSION 3.5)
project(v4l2fb C)

set(CMAKE_C_STANDARD 99)

#SET(CMAKE_BUILD_TYPE "Debug")
#SET(CMAKE_C_FLAGS_DEBUG "$ENV{CFLAGS} -O0 -Wall -g2 -ggdb")
#SET(CMAKE_C_FLAGS_RELEASE "$ENV{CFLAGS} -O3 -Wall")

set(EXTRA_LIBS)
#set(EXTRA_LIBS pthread)
#find_package(SDL REQUIRED)
#if (SDL_FOUND)
#    message(STATUS ${SDL_LIBRARY})
#    include_directories(${SDL_INCLUDE_DIR})
#    set(EXTRA_LIBS ${EXTRA_LIBS} SDL)
#endif (SDL_FOUND)

aux_source_directory(. DIR_SRCS)

add_executable(v4l2fb ${DIR_SRCS})

target_link_libraries(v4l2fb ${EXTRA_LIBS})
