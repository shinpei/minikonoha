cmake_minimum_required(VERSION 2.6)
set(PACKAGE_SOURCE_CODE math_glue.c)
set(PACKAGE_SCRIPT_CODE math_glue.k)
find_library(MATH_LIBRARY NAMES m)
set(PACKAGE_EXTRA_LIBRARY ${MATH_LIBRARY})
add_konoha_package(konoha.math)
