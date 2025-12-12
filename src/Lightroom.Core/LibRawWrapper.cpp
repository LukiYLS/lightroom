// LibRawWrapper.h 已经处理了 Winsock 冲突
#include "LibRawWrapper.h"
#include <iostream>
#include <algorithm>
#include <cstring>  // for memcpy

#ifdef LIBRAW_AVAILABLE
// LibRaw 已集成，使用实际实现
// 使用 C++ API (LibRaw 类)
#else
// LibRaw 未集成，使用占位实现
#endif

namespace LightroomCore {

LibRawWrapper::LibRawWrapper()
    :     m_Processor(nullptr)
    , m_Data(nullptr)
    , m_IsOpen(false)
{
#ifdef LIBRAW_AVAILABLE
    // 使用 C++ API: LibRaw 类
    LibRaw* processor = new LibRaw(LIBRAW_OPIONS_NO_DATAERR_CALLBACK);
    if (processor) {
        m_Processor = processor;
        m_Data = &(processor->imgdata);
    }
#else
    // 占位实现
    m_Processor = nullptr;
    m_Data = nullptr;
#endif
}

LibRawWrapper::~LibRawWrapper() {
#ifdef LIBRAW_AVAILABLE
    if (m_Processor) {
        LibRaw* processor = reinterpret_cast<LibRaw*>(m_Processor);
        delete processor;
        m_Processor = nullptr;
    }
    m_Data = nullptr;
#else
    // 占位实现
    m_Processor = nullptr;
    m_Data = nullptr;
#endif
}

bool LibRawWrapper::OpenFile(const std::wstring& filePath) {
    m_IsOpen = false;
    m_LastError.clear();

#ifdef LIBRAW_AVAILABLE
    if (!m_Processor) {
        m_LastError = "LibRaw processor not initialized";
        return false;
    }

    // 使用 C++ API: LibRaw 类支持宽字符路径
    LibRaw* processor = reinterpret_cast<LibRaw*>(m_Processor);
    
    // LibRaw C++ API 支持直接使用宽字符路径
    int ret = processor->open_file(filePath.c_str());
    if (ret != LIBRAW_SUCCESS) {
        m_LastError = "Failed to open RAW file: ";
        m_LastError += libraw_strerror(ret);
        return false;
    }

    // 解包 RAW 数据
    ret = processor->unpack();
    if (ret != LIBRAW_SUCCESS) {
        m_LastError = "Failed to unpack RAW data: ";
        m_LastError += libraw_strerror(ret);
        processor->recycle();
        return false;
    }

    m_IsOpen = true;
    return true;
#else
    // 占位实现：返回 false，表示 LibRaw 未集成
    m_LastError = "LibRaw not integrated. Please download and integrate LibRaw library.";
    return false;
#endif
}

bool LibRawWrapper::ExtractMetadata(RAWImageInfo& outInfo) {
    if (!m_IsOpen) {
        m_LastError = "No file opened";
        return false;
    }

#ifdef LIBRAW_AVAILABLE
    if (!m_Data) {
        m_LastError = "LibRaw data not available";
        return false;
    }

    libraw_data_t* data = reinterpret_cast<libraw_data_t*>(m_Data);

    // 提取基本尺寸信息
    // 使用实际图像尺寸（可能经过裁剪）
    outInfo.width = data->sizes.iwidth > 0 ? data->sizes.iwidth : data->sizes.raw_width;
    outInfo.height = data->sizes.iheight > 0 ? data->sizes.iheight : data->sizes.raw_height;
    outInfo.bitsPerPixel = 16;  // RAW 数据通常是 16-bit

    // 提取 Bayer pattern
    // LibRaw 使用不同的模式值，需要转换
    // 0 = RGGB, 1 = BGGR, 2 = GRBG, 3 = GBRG
    outInfo.bayerPattern = data->idata.filters;

    // 提取白平衡
    outInfo.whiteBalance[0] = data->color.cam_mul[0];  // R
    outInfo.whiteBalance[1] = data->color.cam_mul[1];  // G1
    outInfo.whiteBalance[2] = data->color.cam_mul[2];  // B
    outInfo.whiteBalance[3] = data->color.cam_mul[3];  // G2

    // 提取颜色矩阵
    if (data->color.cmatrix[0][0] != 0) {
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                outInfo.colorMatrix[i * 3 + j] = data->color.cmatrix[i][j];
            }
        }
    }

    // 提取相机信息
    if (data->idata.make) {
        outInfo.make = data->idata.make;
    }
    if (data->idata.model) {
        outInfo.model = data->idata.model;
        outInfo.cameraModel = data->idata.model;
    }
    if (data->lens.Lens) {
        outInfo.lensModel = data->lens.Lens;
    }

    // 提取 EXIF 信息
    outInfo.iso = data->other.iso_speed;
    outInfo.aperture = data->other.aperture;
    outInfo.shutterSpeed = data->other.shutter;
    outInfo.focalLength = data->other.focal_len;

    // software 字段可能不存在，使用其他字段
    if (data->other.desc && strlen(data->other.desc) > 0) {
        outInfo.software = data->other.desc;
    }

    return true;
#else
    // 占位实现
    m_LastError = "LibRaw not integrated";
    return false;
#endif
}

bool LibRawWrapper::UnpackRAW(std::vector<uint16_t>& outData, uint32_t& outWidth, uint32_t& outHeight) {
    if (!m_IsOpen) {
        m_LastError = "No file opened";
        return false;
    }

#ifdef LIBRAW_AVAILABLE
    if (!m_Data) {
        m_LastError = "LibRaw data not available";
        return false;
    }

    libraw_data_t* data = reinterpret_cast<libraw_data_t*>(m_Data);

    outWidth = data->sizes.raw_width;
    outHeight = data->sizes.raw_height;
    uint32_t pixelCount = outWidth * outHeight;

    // 分配输出缓冲区
    outData.resize(pixelCount);

    // 复制 RAW 数据（16-bit）
    const uint16_t* rawImage = data->rawdata.raw_image;
    if (rawImage) {
        memcpy(outData.data(), rawImage, pixelCount * sizeof(uint16_t));
        return true;
    }

    m_LastError = "Raw image data not available";
    return false;
#else
    // 占位实现
    m_LastError = "LibRaw not integrated";
    return false;
#endif
}

bool LibRawWrapper::ProcessRAW(std::vector<uint8_t>& outData, uint32_t& outWidth, uint32_t& outHeight) {
    if (!m_IsOpen) {
        m_LastError = "No file opened";
        return false;
    }

#ifdef LIBRAW_AVAILABLE
    if (!m_Processor) {
        m_LastError = "LibRaw processor not available";
        return false;
    }

    LibRaw* processor = reinterpret_cast<LibRaw*>(m_Processor);
    libraw_data_t* data = reinterpret_cast<libraw_data_t*>(m_Data);

    // 设置处理参数
    // 使用高质量去马赛克算法
    processor->imgdata.params.output_bps = 8;  // 输出 8-bit（dcraw_make_mem_image 会使用此设置）
    processor->imgdata.params.use_camera_wb = 1;  // 使用相机白平衡
    processor->imgdata.params.use_auto_wb = 0;  // 不使用自动白平衡
    processor->imgdata.params.highlight = 0;  // 高光恢复模式：0=clip, 1=unclip, 2=blend, 3=rebuild
    processor->imgdata.params.four_color_rgb = 0;  // 标准 RGB
    processor->imgdata.params.dcb_iterations = 0;  // DCB 迭代次数
    processor->imgdata.params.dcb_enhance_fl = 0;  // DCB 增强
    processor->imgdata.params.med_passes = 0;  // 中值滤波次数
    processor->imgdata.params.no_auto_bright = 0;  // 启用自动亮度调整（重要！）
    processor->imgdata.params.no_auto_scale = 0;  // 启用自动色彩缩放（重要！）
    processor->imgdata.params.output_color = 1;  // sRGB 色彩空间
    // gamma 曲线：使用标准 sRGB gamma (2.2)
    processor->imgdata.params.gamm[0] = 1.0 / 2.222;  // gamma 值
    processor->imgdata.params.gamm[1] = 4.5;  // slope
    processor->imgdata.params.bright = 1.0;  // 亮度调整倍数（1.0 = 不调整，让自动亮度工作）
    processor->imgdata.params.user_qual = 3;  // 去马赛克质量：0=fast, 1=normal, 2=high, 3=best

    // 步骤 1: 转换为图像格式
    int ret = processor->raw2image();
    if (ret != LIBRAW_SUCCESS) {
        m_LastError = "Failed to convert raw2image: ";
        m_LastError += libraw_strerror(ret);
        return false;
    }

    // 步骤 2: 进行去马赛克和色彩处理
    ret = processor->dcraw_process();
    if (ret != LIBRAW_SUCCESS) {
        m_LastError = "Failed to process RAW data: ";
        m_LastError += libraw_strerror(ret);
        return false;
    }

    // 步骤 3: 使用 dcraw_make_mem_image 获取处理后的图像
    // 这是 LibRaw 推荐的方式，会通过 color.curve 查找表应用 gamma 校正
    // 确保正确的亮度和色彩还原
    libraw_processed_image_t* processedImage = processor->dcraw_make_mem_image(&ret);
    if (ret != LIBRAW_SUCCESS || !processedImage) {
        m_LastError = "Failed to make memory image: ";
        if (ret != LIBRAW_SUCCESS) {
            m_LastError += libraw_strerror(ret);
        } else {
            m_LastError += "processedImage is null";
        }
        return false;
    }

    // 步骤 4: 获取处理后的图像尺寸
    outWidth = processedImage->width;
    outHeight = processedImage->height;
    uint32_t pixelCount = outWidth * outHeight;

    // 步骤 5: 检查图像格式
    if (processedImage->colors != 3) {
        m_LastError = "Unsupported color format (expected RGB)";
        processor->dcraw_clear_mem(processedImage);
        return false;
    }

    // 步骤 6: 复制 RGB 数据
    // processedImage->data 已经是 8-bit RGB 数据（根据 output_bps=8 设置）
    // dcraw_make_mem_image 已经通过 color.curve 应用了 gamma 校正
    if (processedImage->bits == 8) {
        // 8-bit RGB 数据，直接复制
        outData.resize(pixelCount * 3);
        memcpy(outData.data(), processedImage->data, pixelCount * 3);
    } else if (processedImage->bits == 16) {
        // 16-bit RGB 数据，需要转换为 8-bit
        outData.resize(pixelCount * 3);
        const uint16_t* srcData = reinterpret_cast<const uint16_t*>(processedImage->data);
        for (uint32_t i = 0; i < pixelCount; ++i) {
            outData[i * 3 + 0] = static_cast<uint8_t>(srcData[i * 3 + 0] >> 8);  // R
            outData[i * 3 + 1] = static_cast<uint8_t>(srcData[i * 3 + 1] >> 8);  // G
            outData[i * 3 + 2] = static_cast<uint8_t>(srcData[i * 3 + 2] >> 8);  // B
        }
    } else {
        m_LastError = "Unsupported bit depth";
        processor->dcraw_clear_mem(processedImage);
        return false;
    }

    // 清理内存
    processor->dcraw_clear_mem(processedImage);
    return true;
#else
    // 占位实现
    m_LastError = "LibRaw not integrated";
    return false;
#endif
}

const char* LibRawWrapper::GetError() const {
    return m_LastError.c_str();
}

bool LibRawWrapper::IsOpen() const {
    return m_IsOpen;
}

} // namespace LightroomCore

