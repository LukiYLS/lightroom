cmake_minimum_required(VERSION 3.10)
project(LightroomCore)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)

# 导出 DLL 宏
add_definitions(-DLIGHTROOM_CORE_EXPORTS)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR}/d3d11rhi)

# D3D11RHI 文件
file(GLOB D3D11RHI_SOURCES "d3d11rhi/*.cpp")
file(GLOB D3D11RHI_HEADERS "d3d11rhi/*.h")

# SDK 入口文件
set(SDK_SOURCES LightroomSDK.cpp)
set(SDK_HEADERS LightroomSDK.h)

# 创建过滤器组（在 VS 中显示为文件夹）
source_group("d3d11rhi" FILES ${D3D11RHI_SOURCES} ${D3D11RHI_HEADERS})
source_group("Source Files" FILES ${SDK_SOURCES} ${SDK_HEADERS})

# 合并文件列表
set(ALL_SOURCES ${SDK_SOURCES} ${D3D11RHI_SOURCES})
set(ALL_HEADERS ${SDK_HEADERS} ${D3D11RHI_HEADERS})

# 创建动态库
add_library(LightroomCore SHARED ${ALL_SOURCES} ${ALL_HEADERS})

# 链接 D3D11 和 D3D9 库 (Windows Only)
if(WIN32)
    target_link_libraries(LightroomCore d3d11 dxgi d3dcompiler dxguid d3d9)
endif()

# 输出目录设置
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE})

# 如果是 Windows，需要处理 debug/release 子目录
if(MSVC)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/../../bin/Debug)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/../../bin/Debug)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/../../bin/Debug)
    
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/../../bin/Release)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/../../bin/Release)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/../../bin/Release)
endif()
