# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/conrado/esp-idf5/esp-idf/components/bootloader/subproject"
  "/home/conrado/TapBeerFlowControl/build/bootloader"
  "/home/conrado/TapBeerFlowControl/build/bootloader-prefix"
  "/home/conrado/TapBeerFlowControl/build/bootloader-prefix/tmp"
  "/home/conrado/TapBeerFlowControl/build/bootloader-prefix/src/bootloader-stamp"
  "/home/conrado/TapBeerFlowControl/build/bootloader-prefix/src"
  "/home/conrado/TapBeerFlowControl/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/conrado/TapBeerFlowControl/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/conrado/TapBeerFlowControl/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
