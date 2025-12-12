# LibRaw 编译指南

## 方式 1: 使用 LibRaw 自带的 Visual Studio 项目（推荐）

### 步骤

1. **打开 LibRaw 解决方案**
   - 打开 `third_party/libraw/LibRaw.sln`
   - 或者在 Visual Studio 中打开 `third_party/libraw/buildfiles/libraw.vcxproj`

2. **配置项目**
   - 选择 `x64` 平台
   - 选择 `Release` 或 `Debug` 配置
   - 右键项目 → 属性 → 配置属性 → 常规
     - 配置类型：静态库 (.lib)
     - 平台工具集：Visual Studio 2022 (v143)

3. **编译**
   - 生成 → 生成解决方案
   - 编译后的库文件会在输出目录（通常是 `debug-x86_64/` 或 `release-x86_64/`）

4. **复制库文件**
   - 将编译后的 `libraw.lib` 复制到 `third_party/libraw/lib/` 目录
   - 如果输出目录不同，请相应调整

## 方式 2: 使用 CMake（跨平台）

### 步骤

1. **打开 CMake**
   - 设置源代码目录：`third_party/libraw`
   - 设置构建目录：`third_party/libraw/build`

2. **配置**
   - 点击 Configure
   - 选择 Visual Studio 2022 和 x64
   - 设置 `CMAKE_BUILD_TYPE` 为 `Release` 或 `Debug`

3. **生成**
   - 点击 Generate
   - 点击 Open Project 打开 Visual Studio

4. **编译**
   - 在 Visual Studio 中生成解决方案
   - 库文件会在 `build/Release/` 或 `build/Debug/` 目录

5. **复制库文件**
   - 将 `libraw.lib` 复制到 `third_party/libraw/lib/` 目录

## 方式 3: 直接包含源文件到项目（简单但不推荐）

如果不想单独编译库，可以将 LibRaw 的源文件直接添加到项目中：

1. 在 `Lightroom.Core.vcxproj` 中添加所有 `third_party/libraw/src/*.cpp` 文件
2. 添加 `third_party/libraw/internal` 到包含目录
3. 不需要链接 `libraw.lib`

**注意：** 这种方式会增加编译时间，但可以避免单独编译库的步骤。

## 验证编译

编译成功后，检查：
- `third_party/libraw/lib/libraw.lib` 文件存在
- 项目可以成功链接
- 代码可以 `#include <libraw/libraw.h>`

## 当前项目配置

项目已配置：
- ✅ 包含目录：`$(ProjectDir)..\..\third_party\libraw`
- ✅ 预处理器定义：`LIBRAW_AVAILABLE`
- ✅ 库目录：`$(ProjectDir)..\..\third_party\libraw\lib`
- ✅ 库链接：`libraw.lib`

编译 LibRaw 后，将 `.lib` 文件放到 `third_party/libraw/lib/` 目录即可。


