cmake_minimum_required(VERSION 3.16)
project(omega_smoke_test CXX)

find_package(omega REQUIRED)

add_executable(omega_smoke_test main.cpp)
target_link_libraries(omega_smoke_test PRIVATE omega::omega)
