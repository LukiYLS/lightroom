cmake_minimum_required(VERSION 3.20)
project(LightroomCore VERSION 1.0.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置输出目录（与 vcxproj 一致）
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin/${CMAKE_BUILD_TYPE})

# Windows 特定设置
if(WIN32)
    # 设置目标名称
    set_target_properties(LightroomCore PROPERTIES OUTPUT_NAME "LightroomCore")
    
    # 设置 Windows SDK 版本
    set(CMAKE_SYSTEM_VERSION "10.0")
endif()

# 预处理器定义
set(COMMON_DEFINITIONS
    LIGHTROOM_CORE_EXPORTS
    _WINDOWS
    _USRDLL
    LIBRAW_AVAILABLE
    FFMPEG_AVAILABLE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Debug 配置
set(DEBUG_DEFINITIONS
    _DEBUG
    ${COMMON_DEFINITIONS}
)

# Release 配置
set(RELEASE_DEFINITIONS
    NDEBUG
    ${COMMON_DEFINITIONS}
)

# 包含目录
set(INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/d3d11rhi
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libraw
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ffmpeg/include
)

# 库目录
set(LIBRARY_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libraw/lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/ffmpeg/lib
)

# 源文件列表（与 vcxproj 完全一致）
set(CORE_SOURCES
    LightroomSDK.cpp
    D3D9Interop.cpp
    RenderTargetManager.cpp
    RenderGraph.cpp
)

set(D3D11RHI_SOURCES
    d3d11rhi/D3D11CommandContext.cpp
    d3d11rhi/D3D11IndexBuffer.cpp
    d3d11rhi/D3D11RenderTarget.cpp
    d3d11rhi/D3D11RHI.cpp
    d3d11rhi/D3D11Shader.cpp
    d3d11rhi/D3D11State.cpp
    d3d11rhi/D3D11StateCachePrivate.cpp
    d3d11rhi/D3D11Texture1D.cpp
    d3d11rhi/D3D11Texture2D.cpp
    d3d11rhi/D3D11TextureCube.cpp
    d3d11rhi/D3D11TilePool.cpp
    d3d11rhi/D3D11UniformBuffer.cpp
    d3d11rhi/D3D11UnorderedAccessView.cpp
    d3d11rhi/D3D11VertexBuffer.cpp
    d3d11rhi/D3D11ViewPort.cpp
    d3d11rhi/D3D11WindowDevice.cpp
    d3d11rhi/D3DShaderUtil.cpp
    d3d11rhi/DynamicRHI.cpp
    d3d11rhi/RHI.cpp
    d3d11rhi/RHICachedStates.cpp
    d3d11rhi/RHIShdader.cpp
    d3d11rhi/RHIViewPort.cpp
)

set(IMAGE_PROCESSING_SOURCES
    ImageProcessing/ImageProcessor.cpp
    ImageProcessing/LibRawWrapper.cpp
    ImageProcessing/RAWImageLoader.cpp
    ImageProcessing/StandardImageLoader.cpp
)

set(VIDEO_PROCESSING_SOURCES
    VideoProcessing/LightroomSDK_Video.cpp
    VideoProcessing/FFmpegVideoLoader.cpp
    VideoProcessing/VideoProcessor.cpp
    VideoProcessing/VideoExporter.cpp
)

set(RENDER_NODES_SOURCES
    RenderNodes/RenderNode.cpp
    RenderNodes/ScaleNode.cpp
    RenderNodes/ImageAdjustNode.cpp
    RenderNodes/FilterNode.cpp
)

# 合并所有源文件
set(ALL_SOURCES
    ${CORE_SOURCES}
    ${D3D11RHI_SOURCES}
    ${IMAGE_PROCESSING_SOURCES}
    ${VIDEO_PROCESSING_SOURCES}
    ${RENDER_NODES_SOURCES}
)

# 头文件列表（用于 IDE 显示）
set(CORE_HEADERS
    LightroomSDK.h
    LightroomSDKTypes.h
    LightroomSDK_Internal.h
    D3D9Interop.h
    RenderTargetManager.h
    RenderGraph.h
)

set(D3D11RHI_HEADERS
    d3d11rhi/Common.h
    d3d11rhi/D3D11CommandContext.h
    d3d11rhi/D3D11IndexBuffer.h
    d3d11rhi/D3D11RenderTarget.h
    d3d11rhi/D3D11ReourceTraits.h
    d3d11rhi/D3D11RHI.h
    d3d11rhi/D3D11RHIDeclare.h
    d3d11rhi/D3D11RHIPrivate.h
    d3d11rhi/D3D11Shader.h
    d3d11rhi/D3D11State.h
    d3d11rhi/D3D11StateCachePrivate.h
    d3d11rhi/D3D11Texture1D.h
    d3d11rhi/D3D11Texture2D.h
    d3d11rhi/D3D11TextureCube.h
    d3d11rhi/D3D11TilePool.h
    d3d11rhi/D3D11UniformBuffer.h
    d3d11rhi/D3D11UnorderedAccessView.h
    d3d11rhi/D3D11VertexBuffer.h
    d3d11rhi/D3D11ViewPort.h
    d3d11rhi/D3DShaderUtil.h
    d3d11rhi/DynamicRHI.h
    d3d11rhi/EnumAsByte.h
    d3d11rhi/RHI.h
    d3d11rhi/RHICachedStates.h
    d3d11rhi/RHICommandContext.h
    d3d11rhi/RHIDefinitions.h
    d3d11rhi/RHIIndexBuffer.h
    d3d11rhi/RHIPipeLineState.h
    d3d11rhi/RHIRenderTarget.h
    d3d11rhi/RHIShaderDefine.h
    d3d11rhi/RHIShdader.h
    d3d11rhi/RHIState.h
    d3d11rhi/RHIStaticState.h
    d3d11rhi/RHITexture1D.h
    d3d11rhi/RHITexture2D.h
    d3d11rhi/RHITextureCube.h
    d3d11rhi/RHITilePool.h
    d3d11rhi/RHIUniformBuffer.h
    d3d11rhi/RHIUnorderedAccessView.h
    d3d11rhi/RHIVertexBuffer.h
    d3d11rhi/RHIViewPort.h
    d3d11rhi/ShaderCore.h
    d3d11rhi/TypeHash.h
)

set(IMAGE_PROCESSING_HEADERS
    ImageProcessing/ImageLoader.h
    ImageProcessing/ImageProcessor.h
    ImageProcessing/LibRawWrapper.h
    ImageProcessing/RAWImageInfo.h
    ImageProcessing/RAWImageLoader.h
    ImageProcessing/StandardImageLoader.h
)

set(VIDEO_PROCESSING_HEADERS
    VideoProcessing/VideoLoader.h
    VideoProcessing/FFmpegVideoLoader.h
    VideoProcessing/VideoProcessor.h
    VideoProcessing/VideoExporter.h
)

set(RENDER_NODES_HEADERS
    RenderNodes/RenderNode.h
    RenderNodes/ScaleNode.h
    RenderNodes/ImageAdjustNode.h
    RenderNodes/FilterNode.h
)

# 创建动态库
add_library(LightroomCore SHARED
    ${ALL_SOURCES}
    ${CORE_HEADERS}
    ${D3D11RHI_HEADERS}
    ${IMAGE_PROCESSING_HEADERS}
    ${VIDEO_PROCESSING_HEADERS}
    ${RENDER_NODES_HEADERS}
    LightroomCore.def
)

# 设置包含目录
target_include_directories(LightroomCore PRIVATE ${INCLUDE_DIRS})

# 设置预处理器定义
target_compile_definitions(LightroomCore
    PRIVATE
    $<$<CONFIG:Debug>:${DEBUG_DEFINITIONS}>
    $<$<CONFIG:Release>:${RELEASE_DEFINITIONS}>
    $<$<CONFIG:RelWithDebInfo>:${RELEASE_DEFINITIONS}>
    $<$<CONFIG:MinSizeRel>:${RELEASE_DEFINITIONS}>
)

# 编译选项
target_compile_options(LightroomCore PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        /W3                    # Warning level 3
        /sdl                   # SDL check
        /permissive-           # Conformance mode
        /Zc:__cplusplus        # Enable updated __cplusplus macro
    >
)

# Debug 配置特定选项
target_compile_options(LightroomCore PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:
        /Od                    # Disable optimization
        /RTC1                  # Runtime checks
        /Zi                    # Program database debug info
        /MDd                   # Multi-threaded debug DLL runtime
    >
)

# Release 配置特定选项
target_compile_options(LightroomCore PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:
        /O2                    # Maximize speed
        /GL                    # Whole program optimization
        /MD                    # Multi-threaded DLL runtime
        /Gy                    # Function-level linking
    >
)

# 链接选项
if(WIN32)
    target_link_options(LightroomCore PRIVATE
        /SUBSYSTEM:WINDOWS
        /DEF:${CMAKE_CURRENT_SOURCE_DIR}/LightroomCore.def
    )
    
    # Debug 链接选项
    target_link_options(LightroomCore PRIVATE
        $<$<CONFIG:Debug>:
            /DEBUG              # Generate debug info
            /INCREMENTAL        # Enable incremental linking
        >
    )
    
    # Release 链接选项
    target_link_options(LightroomCore PRIVATE
        $<$<CONFIG:Release>:
            /LTCG               # Link-time code generation
            /OPT:REF            # Optimize references
            /OPT:ICF            # Enable COMDAT folding
        >
    )
endif()

# 链接库
target_link_directories(LightroomCore PRIVATE ${LIBRARY_DIRS})

# DirectX 库
if(WIN32)
    target_link_libraries(LightroomCore PRIVATE
        d3d11
        dxgi
        d3dcompiler
        dxguid
        d3d9
    )
endif()

# 第三方库
target_link_libraries(LightroomCore PRIVATE
    libraw
    avformat
    avcodec
    avutil
    swscale
)

# 设置输出目录（确保与 vcxproj 一致）
set_target_properties(LightroomCore PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../../bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../../bin/Release"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/../../bin/RelWithDebInfo"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/../../bin/MinSizeRel"
    
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../../bin/Debug"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../../bin/Release"
    LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/../../bin/RelWithDebInfo"
    LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/../../bin/MinSizeRel"
    
    ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../../bin/Debug"
    ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../../bin/Release"
    ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_SOURCE_DIR}/../../bin/RelWithDebInfo"
    ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_SOURCE_DIR}/../../bin/MinSizeRel"
)

# Post-build 事件：复制 DLL 文件
if(WIN32)
    # 复制 libraw.dll
    add_custom_command(TARGET LightroomCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/../../third_party/libraw/buildfiles/release-x86_64/libraw.dll"
        "$<TARGET_FILE_DIR:LightroomCore>/libraw.dll"
        COMMENT "Copying libraw.dll"
    )
    
    # 复制 FFmpeg DLLs
    file(GLOB FFMPEG_DLLS "${CMAKE_SOURCE_DIR}/../../third_party/ffmpeg/bin/*.dll")
    if(FFMPEG_DLLS)
        add_custom_command(TARGET LightroomCore POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FFMPEG_DLLS}
            "$<TARGET_FILE_DIR:LightroomCore>"
            COMMENT "Copying FFmpeg DLLs"
        )
    endif()
endif()

# 为 Visual Studio 组织源文件（与 vcxproj.filters 一致）
source_group("d3d11rhi" FILES ${D3D11RHI_SOURCES} ${D3D11RHI_HEADERS})
source_group("ImageProcessing" FILES ${IMAGE_PROCESSING_SOURCES} ${IMAGE_PROCESSING_HEADERS})
source_group("VideoProcessing" FILES ${VIDEO_PROCESSING_SOURCES} ${VIDEO_PROCESSING_HEADERS})
source_group("RenderNodes" FILES ${RENDER_NODES_SOURCES} ${RENDER_NODES_HEADERS})
source_group("Source Files" FILES ${CORE_SOURCES} ${CORE_HEADERS})
