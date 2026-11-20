cmake_minimum_required(VERSION 3.22)
project(olivision CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ncnn android-vulkan package (downloaded by the build)
set(ncnn_DIR ${NCNN_ROOT}/${ANDROID_ABI}/lib/cmake/ncnn)
find_package(ncnn REQUIRED)

add_library(olivision SHARED
    olivision.cpp
    realesrgan/realesrgan.cpp
)
target_include_directories(olivision PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/realesrgan)
target_link_libraries(olivision ncnn jnigraphics android log)
target_link_options(olivision PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
