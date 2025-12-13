# Winsock 冲突解决方案

## 问题分析

### 根本原因

1. **Windows SDK 的头文件包含机制**：
   - `windows.h` 默认会包含 `winsock.h` (Winsock 1.1)
   - 如果先包含 `windows.h`，再包含 `winsock2.h`，会产生冲突

2. **LibRaw 的头文件行为**：
   - `libraw_datastream.h` 在 Windows 下会包含 `winsock2.h`（第 38 行）
   - `internal/defines.h` 在 Windows 下也会包含 `winsock2.h`（第 51 行）
   - LibRaw 会检查 `LIBRAW_NO_WINSOCK2`，如果未定义则包含 `winsock2.h`

3. **冲突场景**：
   ```
   我们的代码：
   1. 包含 Common.h → 包含 Windows.h → 包含 winsock.h
   2. 包含 LibRawWrapper.h → 包含 libraw.h → 包含 winsock2.h
   3. 冲突！winsock.h 和 winsock2.h 同时存在
   ```

## 解决方案

### 方案：项目级别 + 头文件级别双重保护

#### 1. 项目级别预处理器定义

在 `.vcxproj` 中添加：
```xml
<PreprocessorDefinitions>...;WIN32_LEAN_AND_MEAN;NOMINMAX;...</PreprocessorDefinitions>
```

**作用**：
- `WIN32_LEAN_AND_MEAN`：阻止 `windows.h` 自动包含 `winsock.h`
- `NOMINMAX`：阻止 Windows 定义 `min`/`max` 宏

#### 2. 头文件级别保护

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

**作用**：
- 确保在包含 LibRaw 之前，`winsock2.h` 已经被正确包含
- 即使项目级别的定义失效，头文件级别也能保护

## 为什么这样有效？

1. **WIN32_LEAN_AND_MEAN** 阻止 `windows.h` 包含 `winsock.h`
   - 当定义了这个宏后，`windows.h` 不会自动包含 `winsock.h`
   - 这样就不会与 `winsock2.h` 冲突

2. **先包含 winsock2.h** 确保正确的顺序
   - 即使 `windows.h` 被包含，由于 `WIN32_LEAN_AND_MEAN`，它不会包含 `winsock.h`
   - LibRaw 的 `winsock2.h` 包含会被正确处理（避免重复包含）

3. **双重保护** 确保可靠性
   - 项目级别：影响所有文件
   - 头文件级别：专门保护 LibRaw 的包含

## 验证

编译时应该不再出现：
- `error C2011: "sockaddr":"struct"类型重定义`
- `error C2375: "accept": 重定义；不同的链接`
- 其他 Winsock 相关的重定义错误

## 注意事项

1. **不要在其他地方直接包含 `windows.h`**
   - 应该通过 `Common.h` 或其他统一入口
   - 确保 `WIN32_LEAN_AND_MEAN` 已定义

2. **LibRaw DLL 的 SDK 版本**
   - DLL 是用预编译的，可能使用不同版本的 Windows SDK
   - 如果运行时出现问题，可能需要重新编译 DLL

3. **如果仍有冲突**
   - 检查是否有其他头文件在 LibRaw 之前包含了 `windows.h`
   - 确保所有包含 LibRaw 的文件都遵循正确的顺序


