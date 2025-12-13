# Winsock 冲突问题分析

## 问题根源

### 1. Windows SDK 的头文件包含顺序

Windows SDK 中有两个 Winsock 版本：
- `winsock.h` (Winsock 1.1) - 旧版本
- `winsock2.h` (Winsock 2.0) - 新版本

**关键问题**：
- `windows.h` 默认会包含 `winsock.h`
- 如果先包含 `windows.h`，再包含 `winsock2.h`，会产生冲突
- 正确的顺序是：**先包含 `winsock2.h`，再包含 `windows.h`**

### 2. LibRaw 的头文件行为

LibRaw 的头文件在 Windows 下会包含 `winsock2.h`：

**`libraw_datastream.h`** (第 38 行):
```cpp
#if defined _WIN32
#ifndef LIBRAW_NO_WINSOCK2
// ... 处理 NOMINMAX ...
#include <winsock2.h>
#endif
#endif
```

**`internal/defines.h`** (第 51 行):
```cpp
#if defined LIBRAW_WIN32_CALLS
#ifndef LIBRAW_NO_WINSOCK2
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#endif
```

### 3. 冲突场景

当我们的代码执行以下顺序时会产生冲突：

```
1. 包含 Common.h (包含 Windows.h)
   → Windows.h 包含 winsock.h
   
2. 包含 LibRawWrapper.h (包含 libraw.h)
   → libraw.h 包含 libraw_datastream.h
   → libraw_datastream.h 包含 winsock2.h
   → 冲突！winsock.h 和 winsock2.h 同时存在
```

## 解决方案

### 方案 1: 项目级别预处理器定义（推荐）

在项目文件中定义 `WIN32_LEAN_AND_MEAN` 和 `NOMINMAX`：

```xml
<PreprocessorDefinitions>...;WIN32_LEAN_AND_MEAN;NOMINMAX;...</PreprocessorDefinitions>
```

**作用**：
- `WIN32_LEAN_AND_MEAN`：阻止 `windows.h` 包含 `winsock.h`
- `NOMINMAX`：阻止 Windows 定义 `min`/`max` 宏

### 方案 2: 在包含 LibRaw 之前先包含 winsock2.h

在 `LibRawWrapper.h` 中：

```cpp
// 先定义宏
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

// 先包含 winsock2.h
#include <winsock2.h>
#include <ws2tcpip.h>

// 再包含 windows.h（如果需要）
#include <windows.h>

// 最后包含 LibRaw
#include <libraw/libraw.h>
```

### 方案 3: 定义 LIBRAW_NO_WINSOCK2（不推荐）

如果定义 `LIBRAW_NO_WINSOCK2`，LibRaw 不会包含 `winsock2.h`，但可能影响某些功能。

## 当前实现

我们采用了**方案 1 + 方案 2** 的组合：

1. **项目级别**：在 `.vcxproj` 中定义 `WIN32_LEAN_AND_MEAN` 和 `NOMINMAX`
2. **头文件级别**：在 `LibRawWrapper.h` 中确保正确的包含顺序

这样可以：
- 确保所有文件在包含 `windows.h` 时不会自动包含 `winsock.h`
- 确保在包含 LibRaw 之前，`winsock2.h` 已经被正确包含
- 避免 LibRaw 内部的 Winsock 包含与我们的代码冲突

## 验证

编译时应该不再出现以下错误：
- `error C2011: "sockaddr":"struct"类型重定义`
- `error C2375: "accept": 重定义；不同的链接`
- 其他 Winsock 相关的重定义错误

## 注意事项

1. **不要在其他地方直接包含 `windows.h`**
   - 应该通过 `Common.h` 或其他统一入口
   - 确保 `WIN32_LEAN_AND_MEAN` 已定义

2. **LibRaw DLL 编译时的 SDK 版本**
   - 如果 DLL 是用不同版本的 Windows SDK 编译的，可能会有运行时问题
   - 建议使用相同版本的 SDK 编译 DLL 和主项目

3. **如果仍有冲突**
   - 检查是否有其他头文件在 LibRaw 之前包含了 `windows.h`
   - 确保所有包含 LibRaw 的文件都遵循正确的顺序


