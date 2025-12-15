# LUT 滤镜资源目录

此目录包含所有可用的 LUT (Look-Up Table) 滤镜文件，格式为标准的 `.cube` 文件。

## 滤镜分类

### 人像滤镜 (Portrait)
- Portrait1.cube ~ Portrait10.cube (10个)

### 自然与野生动物 (Nature & Wildlife)
- NW-1.cube ~ NW-10.cube (10个)

### 风景 (Landscape)
- Landscape1.cube ~ Landscape10.cube (10个)

### 黑白 (Black & White)
- BW1.cube ~ BW10.cube (10个)

### 电影风格 (Cinematic)
- Cinematic-1.cube ~ Cinematic-10.cube (10个)

### 色彩增强 (Color Boost)
- Aqua.cube
- Aqua and Orange Dark.cube
- Blues.cube
- Earth Tone Boost.cube
- Green_Blues.cube
- Green_Yellow.cube
- Oranges.cube
- Purple.cube
- Reds.cube
- Reds_Oranges_Yellows.cube (10个)

### 生活方式与商业 (Lifestyle & Commercial)
- LC1.cube ~ LC10.cube (10个)

### 情绪 (Moody)
- Moody1.cube ~ Moody10.cube (10个)

### Lutify.me 系列
- 2-Strip-Process.cube
- Berlin Sky.cube
- Chrome 01.cube
- Classic Teal and Orange.cube
- Fade to Green.cube
- Film Print 01.cube
- Film Print 02.cube
- French Comedy.cube
- Studio Skin Tone Shaper.cube
- Vintage Chrome.cube (10个)

## 总计
共 90 个 LUT 滤镜文件

## 使用说明

这些 `.cube` 文件可以通过 SDK 的 `LoadFilterLUTFromFile()` API 加载并应用到图像上。

## 文件格式

所有文件均为标准的 3D LUT 格式：
- LUT_3D_SIZE: 32 (32x32x32)
- DOMAIN_MIN: 0.0 0.0 0.0
- DOMAIN_MAX: 1.0 1.0 1.0
- 数据格式: RGB 浮点数，每行 3 个值，共 32768 行






