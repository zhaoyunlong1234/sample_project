# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/zhaoyunlong/esp/v5.2.2/esp-idf/components/bootloader/subproject"
  "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader"
  "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix"
  "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix/tmp"
  "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix/src/bootloader-stamp"
  "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix/src"
  "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/zhaoyunlong/esp/esp-workspace/sample_project/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
