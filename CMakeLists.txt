cmake_minimum_required(VERSION 3.10)
project(fmSEIGet)

set(CMAKE_CXX_STANDARD 14)

# 包含 pkg_check_modules 所需模块
find_package(PkgConfig REQUIRED)

# FFmpeg 相关
pkg_check_modules(AV REQUIRED libavformat libavcodec libavutil libswscale)

# 其他依赖
find_package(OpenCV REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system)
find_package(Threads REQUIRED)
find_path(EXIV2_INCLUDE_DIR exiv2/exiv2.hpp)
find_library(EXIV2_LIBRARY exiv2)

include_directories(
    ${AV_INCLUDE_DIRS}
    ${OpenCV_INCLUDE_DIRS}
    ${Boost_INCLUDE_DIRS}
    ${EXIV2_INCLUDE_DIR}
)
link_directories(${AV_LIBRARY_DIRS})

# 可执行文件1：SEI提取到CSV
add_executable(parse_sei_from_file parse_sei_from_file.cpp sei_parser.cpp)
target_link_libraries(parse_sei_from_file
    ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY}
    ${CMAKE_THREAD_LIBS_INIT}
)


# 可执行文件2：影像帧保存+EXIF写入
add_executable(get_image_from_stream get_image_from_stream.cpp sei_parser.cpp)
target_link_libraries(get_image_from_stream
    ${OpenCV_LIBS}
    ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY}
    ${EXIV2_LIBRARY}
    ${AV_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)

# 可执行文件3：SEI提取+影像帧保存+EXIF写入一体化
add_executable(parse_and_tag parse_and_tag.cpp sei_parser.cpp)
target_link_libraries(parse_and_tag
    ${OpenCV_LIBS}
    ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY}
    ${EXIV2_LIBRARY}
    ${AV_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
)
