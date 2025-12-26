#include "ImageAdjustNode.h"
#include "../d3d11rhi/D3D11RHI.h"
#include "../d3d11rhi/D3D11VertexBuffer.h"
#include "../d3d11rhi/D3D11UniformBuffer.h"
#include "../d3d11rhi/RHI.h"
#include "../d3d11rhi/Common.h"
#include <d3dcompiler.h>
#include <iostream>
#include <cstring>
#include <string>
#include <algorithm>

#pragma comment(lib, "d3dcompiler.lib")

namespace LightroomCore {

// Constant buffer 结构体（必须 16 字节对齐）
// 注意：由于参数很多，可能需要使用多个 constant buffer 或纹理
struct __declspec(align(16)) ImageAdjustConstantBuffer {
    // 基本调整
    float Exposure;
    float Contrast;
    float Highlights;
    float Shadows;
    float Whites;
    float Blacks;
    
    // 白平衡（仅对 RAW 有效）
    float Temperature;
    float Tint;
    
    // 颜色调整
    float Vibrance;
    float Saturation;
    
    // HSL 调整 - 色相
    float HueAdjustments[4];  // Red, Orange, Yellow, Green
    float HueAdjustments2[4]; // Aqua, Blue, Purple, Magenta
    
    // HSL 调整 - 饱和度
    float SatAdjustments[4];  // Red, Orange, Yellow, Green
    float SatAdjustments2[4]; // Aqua, Blue, Purple, Magenta
    
    // HSL 调整 - 明亮度
    float LumAdjustments[4];  // Red, Orange, Yellow, Green
    float LumAdjustments2[4]; // Aqua, Blue, Purple, Magenta
    
    // 细节调整
    float Sharpness;
    float NoiseReduction;
    
    // 镜头校正
    float LensDistortion;
    float ChromaticAberration;
    
    // 效果
    float Vignette;
    float Grain;
    
    // 校准
    float ShadowTint;
    float RedHue;
    float RedSaturation;
    float GreenHue;
    float GreenSaturation;
    float BlueHue;
    float BlueSaturation;
    
    float ImageWidth;
    float ImageHeight;
    float Padding[2];
};

ImageAdjustNode::ImageAdjustNode(std::shared_ptr<RenderCore::DynamicRHI> rhi)
    : RenderNode(rhi)
    , m_ShaderResourcesInitialized(false)
{
    // 初始化默认参数（全为 0 或不调整）
    memset(&m_Params, 0, sizeof(ImageAdjustParams));
    m_Params.saturation = 0.0f;  // 0 = 不调整
    m_Params.vibrance = 0.0f;
    m_Params.temperature = 5500.0f;  // 默认日光色温
    
    InitializeShaderResources();
}

ImageAdjustNode::~ImageAdjustNode() {
    CleanupShaderResources();
}

bool ImageAdjustNode::InitializeShaderResources() {
    if (m_ShaderResourcesInitialized || !m_RHI) {
        return m_ShaderResourcesInitialized;
    }

    // 简单的全屏四边形顶点 shader
    const char* vsCode = R"(
        struct VSInput {
            float2 Position : POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        struct VSOutput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        VSOutput main(VSInput input) {
            VSOutput output;
            output.Position = float4(input.Position, 0.0, 1.0);
            output.TexCoord = input.TexCoord;
            return output;
        }
    )";

    // 图像调整像素 shader（基础框架 - 等待逐个实现算法）
    const char* psCodePart1 = R"(
        cbuffer AdjustParams : register(b0) {
            // 基本调整
            float Exposure;
            float Contrast;
            float Highlights;
            float Shadows;
            float Whites;
            float Blacks;
            
            // 白平衡
            float Temperature;
            float Tint;
            
            // 颜色调整
            float Vibrance;
            float Saturation;
            
            // HSL 调整 - 色相
            float4 HueAdjustments;      // Red, Orange, Yellow, Green
            float4 HueAdjustments2;     // Aqua, Blue, Purple, Magenta
            
            // HSL 调整 - 饱和度
            float4 SatAdjustments;      // Red, Orange, Yellow, Green
            float4 SatAdjustments2;     // Aqua, Blue, Purple, Magenta
            
            // HSL 调整 - 明亮度
            float4 LumAdjustments;      // Red, Orange, Yellow, Green
            float4 LumAdjustments2;     // Aqua, Blue, Purple, Magenta
            
            // 细节调整
            float Sharpness;
            float NoiseReduction;
            
            // 镜头校正
            float LensDistortion;
            float ChromaticAberration;
            
            // 效果
            float Vignette;
            float Grain;
            
            // 校准
            float ShadowTint;
            float RedHue;
            float RedSaturation;
            float GreenHue;
            float GreenSaturation;
            float BlueHue;
            float BlueSaturation;
            
            // 图像尺寸
            float ImageWidth;
            float ImageHeight;
            float2 Padding;
        };
        
        Texture2D InputTexture : register(t0);
        SamplerState InputSampler : register(s0);
        
        struct PSInput {
            float4 Position : SV_POSITION;
            float2 TexCoord : TEXCOORD0;
        };
        
    )";
    
    const char* psCodePart2 = R"(
        // 常量定义：1/3，用于色相计算中的120度角度转换
        #define ONE_THIRD 0.3333333
        
        // 曝光调整
        float3 AdjustExposure(float3 color, float ev) {
            // 简单的曝光补偿
            return color * pow(2.0, ev);
        }
        
        // Photoshop 标准对比度调整算法
        // contrast: 对比度值，范围 -1 到 1（已在 C++ 端归一化）
        // 使用 Photoshop 的标准公式：factor = (259 * (contrast + 255)) / (255 * (259 - contrast))
        float3 AdjustContrast(float3 color, float contrast) {
            // contrast 已经是归一化的值（-1 到 1），需要映射到 [-255, 255]
            float c = contrast * 255.0;
            
            // 避免除零错误
            if (abs(c - 259.0) < 0.1) {
                c = 258.9;
            }
            
            // Photoshop 对比度公式
            float factor = (259.0 * (c + 255.0)) / (255.0 * (259.0 - c));
            
            // 应用对比度调整（以 0.5 为中点）
            float3 result;
            result.r = saturate(factor * (color.r - 0.5) + 0.5);
            result.g = saturate(factor * (color.g - 0.5) + 0.5);
            result.b = saturate(factor * (color.b - 0.5) + 0.5);
            
            return result;
        }
        
        // Photoshop 高光调整算法
        // highlights: 高光调整值，范围 -100 到 100（负值恢复高光，正值增强高光）
        // 基于亮度的非线性调整，使用 S 曲线
        float3 AdjustHighlights(float3 color, float highlights) {
            if (abs(highlights) < 0.1) {
                return color;
            }
            
            // 计算亮度（使用标准权重）
            float luminance = dot(color, float3(0.299, 0.587, 0.114));
            
            // 高光区域：亮度 > 0.4（降低阈值，让更多区域受影响）
            if (luminance <= 0.4) {
                return color; // 非高光区域不调整
            }
            
            // 计算高光区域的权重（0.4-1.0 映射到 0.0-1.0）
            float highlightWeight = (luminance - 0.4) / 0.6;
            
            // 使用平滑的 S 曲线函数，增强效果
            float curve = smoothstep(0.0, 1.0, highlightWeight);
            
            // 高光调整：负值恢复（降低），正值增强（提高）
            float adjustAmount = highlights / 100.0;
            
            // 应用调整：增强调整强度
            float3 adjustedColor;
            if (adjustAmount < 0.0) {
                // 恢复高光：降低亮度，恢复细节（增强效果）
                float reduceFactor = 1.0 + adjustAmount * curve * 1.5;
                adjustedColor = color * saturate(reduceFactor);
            } else {
                // 增强高光：提高亮度（增强效果）
                float boostFactor = 1.0 + adjustAmount * curve * 0.8;
                adjustedColor = lerp(color, color * boostFactor, curve);
            }
            
            return saturate(adjustedColor);
        }
        
        // Photoshop 阴影调整算法
        // shadows: 阴影调整值，范围 -100 到 100（正值提升阴影，负值降低阴影）
        // 基于亮度的非线性调整，使用反向 S 曲线
        float3 AdjustShadows(float3 color, float shadows) {
            if (abs(shadows) < 0.1) {
                return color;
            }
            
            // 计算亮度（使用标准权重）
            float luminance = dot(color, float3(0.299, 0.587, 0.114));
            
            // 阴影区域：亮度 < 0.6（提高阈值，让更多区域受影响）
            if (luminance >= 0.6) {
                return color; // 非阴影区域不调整
            }
            
            // 计算阴影区域的权重（0.0-0.6 映射到 1.0-0.0）
            float shadowWeight = (0.6 - luminance) / 0.6;
            
            // 使用平滑的 S 曲线函数，增强效果
            float curve = smoothstep(0.0, 1.0, shadowWeight);
            
            // 阴影调整：正值提升（提亮），负值降低（变暗）
            float adjustAmount = shadows / 100.0;
            
            // 应用调整：增强调整强度
            float3 adjustedColor;
            if (adjustAmount > 0.0) {
                // 提升阴影：增加亮度，恢复细节（增强效果）
                float liftFactor = 1.0 + adjustAmount * curve * 1.5;
                adjustedColor = lerp(color, color * liftFactor, curve);
            } else {
                // 降低阴影：减少亮度（增强效果）
                float reduceFactor = 1.0 + adjustAmount * curve * 1.2;
                adjustedColor = color * saturate(reduceFactor);
            }
            
            return saturate(adjustedColor);
        }
        
        // Photoshop 白色色阶调整算法
        // whites: 白色色阶调整值，范围 -100 到 100（正值提高白色点，负值降低白色点）
        float3 AdjustWhites(float3 color, float whites) {
            if (abs(whites) < 0.1) {
                return color;
            }
            
            // 计算亮度
            float luminance = dot(color, float3(0.299, 0.587, 0.114));
            
            // 只影响高光区域（亮度 > 0.7）
            if (luminance <= 0.7) {
                return color;
            }
            
            // 计算高光区域的权重（0.7-1.0 映射到 0.0-1.0）
            float whiteWeight = (luminance - 0.7) / 0.3;
            
            // 使用平滑曲线
            float curve = whiteWeight * whiteWeight;
            
            // 白色色阶调整
            float adjustAmount = whites / 100.0;
            
            // 应用调整
            float3 adjustedColor;
            if (adjustAmount > 0.0) {
                // 提高白色点：增强高光
                float boostFactor = 1.0 + adjustAmount * curve * 0.3; // 限制增强幅度
                adjustedColor = lerp(color, color * boostFactor, curve);
            } else {
                // 降低白色点：压缩高光
                float reduceFactor = 1.0 + adjustAmount * curve * 0.5;
                adjustedColor = color * reduceFactor;
            }
            
            return saturate(adjustedColor);
        }
        
        // Photoshop 黑色色阶调整算法
        // blacks: 黑色色阶调整值，范围 -100 到 100（正值提高黑色点，负值降低黑色点）
        float3 AdjustBlacks(float3 color, float blacks) {
            if (abs(blacks) < 0.1) {
                return color;
            }
            
            // 计算亮度
            float luminance = dot(color, float3(0.299, 0.587, 0.114));
            
            // 只影响阴影区域（亮度 < 0.3）
            if (luminance >= 0.3) {
                return color;
            }
            
            // 计算阴影区域的权重（0.0-0.3 映射到 1.0-0.0）
            float blackWeight = (0.3 - luminance) / 0.3;
            
            // 使用平滑曲线
            float curve = blackWeight * blackWeight;
            
            // 黑色色阶调整
            float adjustAmount = blacks / 100.0;
            
            // 应用调整
            float3 adjustedColor;
            if (adjustAmount > 0.0) {
                // 提高黑色点：提升阴影
                float liftFactor = 1.0 + adjustAmount * curve * 0.5;
                adjustedColor = lerp(color, color * liftFactor, curve);
            } else {
                // 降低黑色点：加深阴影
                float reduceFactor = 1.0 + adjustAmount * curve;
                adjustedColor = color * reduceFactor;
            }
            
            return saturate(adjustedColor);
        }
        
        // RGB到HSL色彩空间转换
        float3 RGBToHSL(float3 rgb) {
            float fmin = min(min(rgb.r, rgb.g), rgb.b);
            float fmax = max(max(rgb.r, rgb.g), rgb.b);
            float delta = fmax - fmin;
            
            // 明度(Lightness)定义：RGB最大值和最小值的平均值
            float3 hsl = float3(0, 0, (fmax + fmin) * 0.5);
            
            // 只有当存在色彩差异时才计算饱和度和色相
            if (delta > 0.0001) {
                // 饱和度计算：基于明度的不同公式
                hsl.y = (hsl.z < 0.5) 
                      ? delta / (fmax + fmin) 
                      : delta / (2.0 - fmax - fmin);
                
                // 色相计算：基于哪个RGB分量是最大值
                float deltaR = ((fmax - rgb.r) / 6.0 + delta / 2.0) / delta;
                float deltaG = ((fmax - rgb.g) / 6.0 + delta / 2.0) / delta;
                float deltaB = ((fmax - rgb.b) / 6.0 + delta / 2.0) / delta;

                // 色相环分段：R(0°-120°), G(120°-240°), B(240°-360°)
                hsl.x = (rgb.r == fmax) ? deltaB - deltaG :
                       (rgb.g == fmax) ? ONE_THIRD + deltaR - deltaB :
                                         2.0 * ONE_THIRD + deltaG - deltaR;
                
                // 处理色相溢出，确保在[0,1]范围内
                hsl.x = frac(hsl.x + 1.0);
            }
            return hsl;
        }
        
        // HSL到RGB转换的辅助函数
        float HueToRGB(float f1, float f2, float hue) {
            hue = frac(hue);
            
            return (6.0 * hue < 1.0) ? f1 + (f2 - f1) * 6.0 * hue :
                  (2.0 * hue < 1.0) ? f2 :
                  (3.0 * hue < 2.0) ? f1 + (f2 - f1) * (2.0 * ONE_THIRD - hue) * 6.0 :
                                      f1;
        }
        
        // HSL到RGB色彩空间转换
        float3 HSLToRGB(float3 hsl) {
            float3 rgb;
            
            // 如果饱和度为0，则为灰色（无色彩）
            if (hsl.y < 0.0001) {
                rgb = hsl.zzz;
            } else {
                // 计算色彩的上下边界值
                float f2 = (hsl.z < 0.5) 
                         ? hsl.z * (1.0 + hsl.y)
                         : hsl.z + hsl.y - hsl.y * hsl.z;
                float f1 = 2.0 * hsl.z - f2;
                
                // 为每个RGB分量计算对应的色相偏移
                rgb.r = HueToRGB(f1, f2, hsl.x + ONE_THIRD);
                rgb.g = HueToRGB(f1, f2, hsl.x);
                rgb.b = HueToRGB(f1, f2, hsl.x - ONE_THIRD);
            }
            return rgb;
        }
        
        // 专业饱和度调整（基于 HSL 色彩空间）
        float3 AdjustSaturation(float3 color, float s) {
            // 将 UI 的饱和度值（-50 到 50）转换为强度系数（0.0 到 2.0）
            // -50 -> 0.0 (完全去饱和)
            // 0 -> 1.0 (原始饱和度)
            // +50 -> 2.0 (双倍饱和度)
            float saturationFactor = 1.0 + s / 50.0;
            
            // 转换到HSL色彩空间
            float3 hsl = RGBToHSL(color);
            
            // 在HSL空间直接调整饱和度分量
            hsl.y = saturate(hsl.y * saturationFactor);
            
            // 转换回RGB色彩空间
            return HSLToRGB(hsl);
        }
        
        // sRGB到线性RGB转换
        float srgb_to_linear(float srgb_val) {
            if (srgb_val <= 0.04045)
                return srgb_val / 12.92;
            else
                return pow((srgb_val + 0.055) / 1.055, 2.4);
        }
        
        // 线性RGB到sRGB转换
        float linear_to_srgb(float linear_val) {
            if (linear_val <= 0.0031308)
                return linear_val * 12.92;
            else
                return 1.055 * pow(linear_val, 1.0 / 2.4) - 0.055;
        }
        
        // GEGL 色温转换算法
        void convert_k_to_rgb(float temperature, out float3 rgb) {
            // GEGL系数数组（HLSL中需要展开为常量）
            const float rgb_r55_r[12] = {
                6.9389923563552169e-01,  2.7719388100974670e+03,
                2.0999316761104289e+07, -4.8889434162208414e+09,
                -1.1899785506796783e+07, -4.7418427686099203e+04,
                1.0000000000000000e+00,  3.5434394338546258e+03,
                -5.6159353379127791e+05,  2.7369467137870544e+08,
                1.6295814912940913e+08,  4.3975072422421846e+05
            };
            const float rgb_r55_g[12] = {
                9.5417426141210926e-01,  2.2041043287098860e+03,
                -3.0142332673634286e+06, -3.5111986367681120e+03,
                -5.7030969525354260e+00,  6.1810926909962016e-01,
                1.0000000000000000e+00,  1.3728609973644000e+03,
                1.3099184987576159e+06, -2.1757404458816318e+03,
                -2.3892456292510311e+00,  8.1079012401293249e-01
            };
            const float rgb_r55_b[12] = {
                -7.1151622540856201e+10,  3.3728185802339764e+16,
                -7.9396187338868539e+19,  2.9699115135330123e+22,
                -9.7520399221734228e+22, -2.9250107732225114e+20,
                1.0000000000000000e+00,  1.3888666482167408e+16,
                2.3899765140914549e+19,  1.4583606312383295e+23,
                1.9766018324502894e+22,  2.9395068478016189e+18
            };

            // 温度范围限定（1500K-11500K）
            temperature = clamp(temperature, 1500.0, 11500.0);
            float temp = temperature;

            // 计算红色通道
            float nomin_r = rgb_r55_r[0];
            for (int d1 = 1; d1 < 6; d1++) {
                nomin_r = nomin_r * temp + rgb_r55_r[d1];
            }
            float denom_r = rgb_r55_r[6];
            for (int d2 = 1; d2 < 6; d2++) {
                denom_r = denom_r * temp + rgb_r55_r[6 + d2];
            }
            float value_r = nomin_r / denom_r;
            if (value_r < 0) value_r = 0;
            if (value_r < 1e-5) value_r = 1e-5;
            if (value_r > 100.0) value_r = 100.0;
            rgb.r = value_r;

            // 计算绿色通道
            float nomin_g = rgb_r55_g[0];
            for (int d1 = 1; d1 < 6; d1++) {
                nomin_g = nomin_g * temp + rgb_r55_g[d1];
            }
            float denom_g = rgb_r55_g[6];
            for (int d2 = 1; d2 < 6; d2++) {
                denom_g = denom_g * temp + rgb_r55_g[6 + d2];
            }
            float value_g = nomin_g / denom_g;
            if (value_g < 0) value_g = 0;
            if (value_g < 1e-5) value_g = 1e-5;
            if (value_g > 100.0) value_g = 100.0;
            rgb.g = value_g;

            // 计算蓝色通道
            float nomin_b = rgb_r55_b[0];
            for (int d1 = 1; d1 < 6; d1++) {
                nomin_b = nomin_b * temp + rgb_r55_b[d1];
            }
            float denom_b = rgb_r55_b[6];
            for (int d2 = 1; d2 < 6; d2++) {
                denom_b = denom_b * temp + rgb_r55_b[6 + d2];
            }
            float value_b = nomin_b / denom_b;
            if (value_b < 0) value_b = 0;
            if (value_b < 1e-5) value_b = 1e-5;
            if (value_b > 100.0) value_b = 100.0;
            rgb.b = value_b;
        }
        
        // 计算色温调整系数
        void calculate_temp_adjust_coeffs(float intended_temp, out float3 coeffs) {
            const float original_temp = 6500.0; // 标准日光白平衡

            if (abs(original_temp - intended_temp) < 0.1) {
                coeffs = float3(1.0, 1.0, 1.0);
                return;
            }

            float3 original_rgb, intended_rgb;
            convert_k_to_rgb(original_temp, original_rgb);
            convert_k_to_rgb(intended_temp, intended_rgb);

            coeffs = float3(
                original_rgb.r / intended_rgb.r,
                original_rgb.g / intended_rgb.g,
                original_rgb.b / intended_rgb.b
            );
        }
        
        // 专业色温调整（基于 GEGL 算法）
        float3 ApplyTemperature(float3 color, float temp_kelvin) {
            // 如果温度超出有效范围（1500K-11500K），不调整
            if (temp_kelvin < 1500.0 || temp_kelvin > 11500.0) {
                return color;
            }

            // 计算色温调整系数
            float3 tempCoeffs;
            calculate_temp_adjust_coeffs(temp_kelvin, tempCoeffs);

            // 转换到线性RGB空间
            float r_linear = srgb_to_linear(color.r);
            float g_linear = srgb_to_linear(color.g);
            float b_linear = srgb_to_linear(color.b);

            // 应用色温调整
            r_linear *= tempCoeffs.r;
            g_linear *= tempCoeffs.g;
            b_linear *= tempCoeffs.b;

            // 确保值在有效范围内
            r_linear = saturate(r_linear);
            g_linear = saturate(g_linear);
            b_linear = saturate(b_linear);

            // 转换回sRGB空间
            float3 result;
            result.r = linear_to_srgb(r_linear);
            result.g = linear_to_srgb(g_linear);
            result.b = linear_to_srgb(b_linear);

            return result;
        }
    )";
    
    const char* psCodePart3 = R"(
        // 获取缩放因子（用于根据图像尺寸调整采样范围）
        float GetScaleFactor(float2 uv) {
            float standardWidth = 1920.0;
            float standardHeight = 1080.0;
            float scaleX = ImageWidth / standardWidth;
            float scaleY = ImageHeight / standardHeight;
            return min(scaleX, scaleY);
        }
        
        // 高斯模糊采样（基于标准高斯核）
        // 注意：这个函数从输入纹理采样，用于柔化模式
        float3 GaussianBlur(float2 uv, float sigma) {
            float2 texelSize = float2(ddx(uv.x), ddy(uv.y));
            if (length(texelSize) < 0.0001) {
                texelSize = float2(1.0 / ImageWidth, 1.0 / ImageHeight);
            }
            sigma = clamp(sigma, 0.5, 5.0);
            float2 offset1 = float2(1.0, 0.0) * texelSize * sigma;
            float2 offset2 = float2(0.0, 1.0) * texelSize * sigma;
            float2 offset3 = float2(1.0, 1.0) * texelSize * sigma * 0.707;
            float w0 = 0.2270270270;
            float w1 = 0.1945945946;
            float w2 = 0.1216216216;
            float weightSum = w0 + w1 * 4.0 + w2 * 4.0;
            w0 /= weightSum;
            w1 /= weightSum;
            w2 /= weightSum;
            float3 result = InputTexture.Sample(InputSampler, uv).rgb * w0;
            result += InputTexture.Sample(InputSampler, uv + offset1).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv - offset1).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv + offset2).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv - offset2).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv + float2(offset3.x, offset3.y)).rgb * w2;
            result += InputTexture.Sample(InputSampler, uv + float2(-offset3.x, offset3.y)).rgb * w2;
            result += InputTexture.Sample(InputSampler, uv + float2(offset3.x, -offset3.y)).rgb * w2;
            result += InputTexture.Sample(InputSampler, uv + float2(-offset3.x, -offset3.y)).rgb * w2;
            return result;
        }
        
        // 对当前处理后的颜色进行高斯模糊（用于锐化）
        // 需要采样周围像素并应用相同的调整
        float3 GaussianBlurCurrentColor(float2 uv, float sigma, float3 centerColor) {
            float2 texelSize = float2(ddx(uv.x), ddy(uv.y));
            if (length(texelSize) < 0.0001) {
                texelSize = float2(1.0 / ImageWidth, 1.0 / ImageHeight);
            }
            sigma = clamp(sigma, 0.5, 5.0);
            float2 offset1 = float2(1.0, 0.0) * texelSize * sigma;
            float2 offset2 = float2(0.0, 1.0) * texelSize * sigma;
            float2 offset3 = float2(1.0, 1.0) * texelSize * sigma * 0.707;
            float w0 = 0.2270270270;
            float w1 = 0.1945945946;
            float w2 = 0.1216216216;
            float weightSum = w0 + w1 * 4.0 + w2 * 4.0;
            w0 /= weightSum;
            w1 /= weightSum;
            w2 /= weightSum;
            // 采样周围像素（这些像素也需要经过相同的调整，但为了简化，我们直接从纹理采样）
            // 注意：这是近似，理想情况下应该对每个采样点应用相同的调整
            float3 result = centerColor * w0;
            result += InputTexture.Sample(InputSampler, uv + offset1).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv - offset1).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv + offset2).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv - offset2).rgb * w1;
            result += InputTexture.Sample(InputSampler, uv + float2(offset3.x, offset3.y)).rgb * w2;
            result += InputTexture.Sample(InputSampler, uv + float2(-offset3.x, offset3.y)).rgb * w2;
            result += InputTexture.Sample(InputSampler, uv + float2(offset3.x, -offset3.y)).rgb * w2;
            result += InputTexture.Sample(InputSampler, uv + float2(-offset3.x, -offset3.y)).rgb * w2;
            return result;
        }
        
        float ConvertDetailToGrayscale(float3 detail) {
            return dot(detail, float3(0.299, 0.587, 0.114));
        }
        
        float ApplyThresholdMask(float detail, float threshold) {
            float absDetail = abs(detail);
            absDetail *= 2.0;
            float mask = smoothstep(threshold * 0.5, threshold * 1.5, absDetail);
            return detail * mask;
        }
        
        float3 ApplyClarity(float3 color, float2 uv, float clarityValue) {
            // 移除早期返回，确保总是执行（即使 clarityValue = 0）
            // if (abs(clarityValue) < 0.01) return color;
            float scaleFactor = GetScaleFactor(uv);
            if (clarityValue < 0.0) {
                // 柔化处理（负值，-100% ~ 0%）
                float refSigma = -clarityValue * 0.05;
                refSigma = clamp(refSigma, 0.5, 10.0);
                return GaussianBlur(uv, refSigma);
            }
            else {
                // 锐化处理（正值，非锐化掩码）
                // 严格按照用户提供的算法实现，但增强效果以便调试
                float amount = clarityValue * 0.3;  // 临时增加到 0.3 以便看到效果
                float radius;
                
                if (clarityValue <= 10.0) {
                    radius = 1.5;
                }
                else {
                    radius = (1.0 + clarityValue / 25.0) * scaleFactor;
                }
                radius = clamp(radius, 0.5, 10.0);
                
                // 步骤1: 高斯模糊（对输入纹理进行模糊）
                float3 blurred = GaussianBlur(uv, radius);
                
                // 步骤2: 计算原始图像与模糊图像的差值
                float3 originalColor = InputTexture.Sample(InputSampler, uv).rgb;
                float3 detail = originalColor - blurred;
                
                // 步骤3: 将细节图转换为灰度图
                float grayDetail = ConvertDetailToGrayscale(detail);
                
                // 步骤4: 应用阈值处理（临时降低阈值以便看到效果）
                grayDetail = ApplyThresholdMask(grayDetail, 0.01);  // 临时降低到 0.01
                
                // 步骤5: 将灰度细节图应用到当前处理后的颜色
                float3 sharpened = color + grayDetail * amount;
                
                return saturate(sharpened);
            }
        }
    )";
    
    const char* psCodePart4 = R"(
        // ============================================
        // 主函数 - 逐个添加算法调用
        // ============================================
        float4 main(PSInput input) : SV_TARGET {
            // 采样输入纹理
            float4 color = InputTexture.Sample(InputSampler, input.TexCoord);
            float3 rgb = color.rgb;
            
            // ============================================
            // 算法应用区域 - 按顺序逐个添加
            // ============================================
            
            // 1. 白平衡调整 (Temperature, Tint)
            // 注意：Tint 调整暂时未实现，只应用色温
            if (abs(Temperature - 5500.0) > 0.1) {
                rgb = ApplyTemperature(rgb, Temperature);
            }
            
            // 2. 曝光调整 (Exposure)
            if (abs(Exposure) > 0.001) {
                rgb = AdjustExposure(rgb, Exposure);
            }
            
            // 3. 高光/阴影/白色/黑色调整 (Highlights, Shadows, Whites, Blacks)
            // 注意：调整顺序很重要，先调整高光/阴影，再调整白色/黑色色阶
            if (abs(Highlights) > 0.1) {
                rgb = AdjustHighlights(rgb, Highlights);
            }
            if (abs(Shadows) > 0.1) {
                rgb = AdjustShadows(rgb, Shadows);
            }
            if (abs(Whites) > 0.1) {
                rgb = AdjustWhites(rgb, Whites);
            }
            if (abs(Blacks) > 0.1) {
                rgb = AdjustBlacks(rgb, Blacks);
            }
            
            // 4. 对比度调整 (Contrast)
            // 对比度调整应该在色调调整之后进行
            // 注意：Contrast 已经在 C++ 端归一化到 -1 到 1
            if (abs(Contrast) > 0.001) {
                rgb = AdjustContrast(rgb, Contrast);
            }
            
            // 5. HSL 调整 (HueAdjustments, SatAdjustments, LumAdjustments)
            // TODO: 等待算法实现...
            
            // 6. 自然饱和度和饱和度调整 (Vibrance, Saturation)
            // 应用饱和度调整
            if (abs(Saturation) > 0.001) {
                rgb = AdjustSaturation(rgb, Saturation);
            }
            // TODO: Vibrance 调整等待算法实现...
            
            // TODO: 7. 校准调整 (ShadowTint, RedHue, RedSaturation, GreenHue, GreenSaturation, BlueHue, BlueSaturation)
            // 等待算法实现...
            
            // 8. 锐化 (Sharpness) - 使用专业清晰度调整算法
            // Sharpness 范围：0-150，映射到清晰度值 0-100
            // 0 = 不调整，150 = 最大锐化
            // 移除条件判断，确保总是执行（ApplyClarity 内部会检查）
            float clarityValue = (Sharpness / 150.0) * 100.0;
            // 应用锐化（传入当前处理后的颜色）
            rgb = ApplyClarity(rgb, input.TexCoord, clarityValue);
            
            // TODO: 9. 降噪 (NoiseReduction)
            // 等待算法实现...
            
            // TODO: 10. 镜头校正 (LensDistortion, ChromaticAberration)
            // 等待算法实现...
            
            // TODO: 11. 晕影效果 (Vignette)
            // 等待算法实现...
            
            // TODO: 12. 颗粒效果 (Grain)
            // 等待算法实现...
            
            // 裁剪到有效范围
            rgb = saturate(rgb);
            
            return float4(rgb, color.a);
        }
    )";
    
    // 连接所有 shader 代码部分
    std::string psCodeStr = std::string(psCodePart1) + std::string(psCodePart2) + std::string(psCodePart3) + std::string(psCodePart4);
    const char* psCode = psCodeStr.c_str();

    // 使用基类的 CompileShaders 方法
    if (!CompileShaders(vsCode, psCode, m_Shader)) {
        std::cerr << "[ImageAdjustNode] Failed to compile shaders" << std::endl;
        return false;
    }

    // 创建 constant buffer
    const uint32_t cbSize = sizeof(ImageAdjustConstantBuffer);
    m_ParamsBuffer = m_RHI->RHICreateUniformBuffer(cbSize);
    if (!m_ParamsBuffer) {
        std::cerr << "[ImageAdjustNode] Failed to create constant buffer" << std::endl;
        return false;
    }

    m_ShaderResourcesInitialized = (m_ParamsBuffer != nullptr &&
                                    m_Shader.VS != nullptr && 
                                    m_Shader.PS != nullptr && 
                                    m_Shader.InputLayout != nullptr);

    return m_ShaderResourcesInitialized;
}

void ImageAdjustNode::CleanupShaderResources() {
    m_ParamsBuffer.reset();
    m_Shader.VS.Reset();
    m_Shader.PS.Reset();
    m_Shader.InputLayout.Reset();
    m_Shader.Blob.Reset();
    m_ShaderResourcesInitialized = false;
}

void ImageAdjustNode::SetAdjustParams(const ImageAdjustParams& params) {
    m_Params = params;
}

void ImageAdjustNode::UpdateConstantBuffers(uint32_t width, uint32_t height) {
    if (!m_ParamsBuffer || !m_CommandContext) {
        return;
    }

    ImageAdjustConstantBuffer cbData;
    cbData.Exposure = m_Params.exposure;
    cbData.Contrast = m_Params.contrast / 100.0f;  // 归一化到 -1 到 1
    cbData.Highlights = m_Params.highlights;
    cbData.Shadows = m_Params.shadows;
    cbData.Whites = m_Params.whites;
    cbData.Blacks = m_Params.blacks;
    cbData.Temperature = m_Params.temperature;
    cbData.Tint = m_Params.tint;
    cbData.Vibrance = m_Params.vibrance;
    cbData.Saturation = m_Params.saturation;
    
    // HSL 调整
    cbData.HueAdjustments[0] = m_Params.hueRed / 100.0f;
    cbData.HueAdjustments[1] = m_Params.hueOrange / 100.0f;
    cbData.HueAdjustments[2] = m_Params.hueYellow / 100.0f;
    cbData.HueAdjustments[3] = m_Params.hueGreen / 100.0f;
    cbData.HueAdjustments2[0] = m_Params.hueAqua / 100.0f;
    cbData.HueAdjustments2[1] = m_Params.hueBlue / 100.0f;
    cbData.HueAdjustments2[2] = m_Params.huePurple / 100.0f;
    cbData.HueAdjustments2[3] = m_Params.hueMagenta / 100.0f;
    cbData.SatAdjustments[0] = m_Params.satRed;
    cbData.SatAdjustments[1] = m_Params.satOrange;
    cbData.SatAdjustments[2] = m_Params.satYellow;
    cbData.SatAdjustments[3] = m_Params.satGreen;
    cbData.SatAdjustments2[0] = m_Params.satAqua;
    cbData.SatAdjustments2[1] = m_Params.satBlue;
    cbData.SatAdjustments2[2] = m_Params.satPurple;
    cbData.SatAdjustments2[3] = m_Params.satMagenta;
    cbData.LumAdjustments[0] = m_Params.lumRed;
    cbData.LumAdjustments[1] = m_Params.lumOrange;
    cbData.LumAdjustments[2] = m_Params.lumYellow;
    cbData.LumAdjustments[3] = m_Params.lumGreen;
    cbData.LumAdjustments2[0] = m_Params.lumAqua;
    cbData.LumAdjustments2[1] = m_Params.lumBlue;
    cbData.LumAdjustments2[2] = m_Params.lumPurple;
    cbData.LumAdjustments2[3] = m_Params.lumMagenta;
    
    cbData.Sharpness = m_Params.sharpness;
    // 调试输出
    if (abs(m_Params.sharpness) > 0.1f) {
        std::cout << "[ImageAdjustNode] Sharpness = " << m_Params.sharpness << std::endl;
    }
    cbData.NoiseReduction = m_Params.noiseReduction;
    cbData.LensDistortion = m_Params.lensDistortion;
    cbData.ChromaticAberration = m_Params.chromaticAberration;
    cbData.Vignette = m_Params.vignette;
    cbData.Grain = m_Params.grain;
    cbData.ShadowTint = m_Params.shadowTint;
    cbData.RedHue = m_Params.redHue;
    cbData.RedSaturation = m_Params.redSaturation;
    cbData.GreenHue = m_Params.greenHue;
    cbData.GreenSaturation = m_Params.greenSaturation;
    cbData.BlueHue = m_Params.blueHue;
    cbData.BlueSaturation = m_Params.blueSaturation;
    cbData.ImageWidth = static_cast<float>(width);
    cbData.ImageHeight = static_cast<float>(height);
    cbData.Padding[0] = 0.0f;
    cbData.Padding[1] = 0.0f;

    // 使用 RHI 接口更新 constant buffer
    m_CommandContext->RHIUpdateUniformBuffer(m_ParamsBuffer, &cbData);
}

void ImageAdjustNode::SetConstantBuffers() {
    if (m_ParamsBuffer) {
        m_CommandContext->RHISetShaderUniformBuffer(RenderCore::EShaderFrequency::SF_Pixel, 0, m_ParamsBuffer);
    }
}

void ImageAdjustNode::SetShaderResources(std::shared_ptr<RenderCore::RHITexture2D> inputTexture) {
    // 设置输入纹理
    m_CommandContext->RHISetShaderTexture(RenderCore::EShaderFrequency::SF_Pixel, 0, inputTexture);
    // 设置采样器（使用基类的公共采样器）
    m_CommandContext->RHISetShaderSampler(RenderCore::EShaderFrequency::SF_Pixel, 0, m_CommonSamplerState);
}

bool ImageAdjustNode::Execute(std::shared_ptr<RenderCore::RHITexture2D> inputTexture,
                            std::shared_ptr<RenderCore::RHITexture2D> outputTarget,
                            uint32_t width, uint32_t height) {
    if (!m_ShaderResourcesInitialized) {
        return false;
    }

    if (!m_Shader.VS || !m_Shader.PS || !m_Shader.InputLayout || !m_ParamsBuffer) {
        return false;
    }

    // 设置当前 shader（基类 Execute 会使用）
    m_CurrentShader = &m_Shader;

    // 使用基类的 Execute 方法（它会调用我们的钩子方法）
    return RenderNode::Execute(inputTexture, outputTarget, width, height);
}

} // namespace LightroomCore

