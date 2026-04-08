cmake_minimum_required(VERSION 3.10)  # 指定 CMake 最低版本
project(LearnCplusplus)                   # 项目名称

set(CMAKE_CXX_STANDARD 14)            # 使用 C++14 标准

find_package(OpenCV REQUIRED) # 查找OpenCV库

include_directories(${OpenCV_INCLUDE_DIRS}) # 包含OpenCV头文件路径

add_executable(ai_camera ai_camera.cpp)

target_link_libraries(ai_camera
    avformat
    avcodec
    avutil
    swscale  
    ${OpenCV_LIBS} # opencv全家桶
)
