cmake_minimum_required(VERSION 3.23)
project(patch6n)

set(CMAKE_CXX_STANDARD 14)

add_library(patch6n src/libpatch6n/platform/amd64_sysvabi_posix.cc)
target_include_directories(patch6n PUBLIC src/)

function(add_demo SRC)
  add_executable(demo_${SRC} demo/${SRC}.cc)
  target_include_directories(demo_${SRC} PRIVATE src/)
  target_link_libraries(demo_${SRC} PRIVATE patch6n)
endfunction()

add_demo(1_mixin_head)