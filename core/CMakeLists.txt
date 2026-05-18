cmake_minimum_required(VERSION 4.2)
project(core C)

set(CMAKE_C_STANDARD 11)

add_library(core SHARED library.c)
