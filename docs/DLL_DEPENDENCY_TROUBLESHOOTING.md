# DLL 依赖问题排查指南

## 错误信息

```
System.DllNotFoundException: Unable to load DLL 'LightroomCore.dll' or one of its dependencies: 找不到指定的模块。 (0x8007007E)
```

## 可能的原因

### 1. 缺少 Visual C++ 运行时库

`LightroomCore.dll` 是使用 Visual Studio 编译的 C++ DLL，需要 Visual C++ Redistributable。

**解决方案**：
- 安装 [Visual C++ Redistributable for Visual Studio 2022](https://aka.ms/vs/17/release/vc_redist.x64.exe)
- 或者确保开发机器上已安装 Visual Studio 2022（包含运行时）

### 2. 缺少 libraw.dll

`LightroomCore.dll` 依赖 `libraw.dll`。

**检查**：
```powershell
Test-Path "src\Lightroom.App\bin\x64\Debug\net8.0-windows\libraw.dll"
```

**解决方案**：
- 确保 `libraw.dll` 已复制到应用程序输出目录
- 检查 `Lightroom.App.csproj` 中的 `CopyNativeDll` Target 是否正确配置

### 3. DLL 架构不匹配

确保所有 DLL 都是 x64 架构。

**检查**：
- `LightroomCore.dll` 应该是 x64
- `libraw.dll` 应该是 x64
- 应用程序应该配置为 x64

### 4. DLL 路径问题

确保 DLL 在以下位置之一：
1. 应用程序目录（推荐）
2. 系统 PATH
3. 与可执行文件同目录

## 排查步骤

### 步骤 1: 检查 DLL 文件是否存在

```powershell
# 检查应用程序输出目录
Get-ChildItem "src\Lightroom.App\bin\x64\Debug\net8.0-windows\*.dll"
```

应该看到：
- `LightroomCore.dll`
- `libraw.dll`

### 步骤 2: 检查 DLL 依赖项

使用 Dependency Walker 或 dumpbin 检查依赖项：

```powershell
# 使用 dumpbin（需要 Visual Studio）
& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" /dependents "src\Lightroom.App\bin\x64\Debug\net8.0-windows\LightroomCore.dll"
```

### 步骤 3: 检查 Visual C++ 运行时

```powershell
# 检查已安装的运行时版本
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64" -ErrorAction SilentlyContinue
```

### 步骤 4: 使用 Process Monitor 跟踪

使用 [Process Monitor](https://docs.microsoft.com/en-us/sysinternals/downloads/procmon) 跟踪 DLL 加载失败的原因：
1. 启动 Process Monitor
2. 过滤：`Process Name is Lightroom.App.exe` 和 `Operation is Load Image`
3. 运行应用程序
4. 查看哪些 DLL 加载失败

## 解决方案

### 方案 1: 静态链接运行时（推荐用于开发）

在 `Lightroom.Core.vcxproj` 中修改运行时库：

```xml
<RuntimeLibrary>MultiThreaded</RuntimeLibrary>  <!-- 静态链接 -->
```

而不是：

```xml
<RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>  <!-- 动态链接 -->
```

### 方案 2: 确保所有依赖项都在输出目录

在 `Lightroom.App.csproj` 中添加：

```xml
<Target Name="CopyNativeDll" AfterTargets="Build">
  <!-- 复制 LightroomCore.dll -->
  <!-- 复制 libraw.dll -->
  <!-- 复制 Visual C++ 运行时 DLL（如果使用动态链接） -->
</Target>
```

### 方案 3: 使用 LoadLibrary 显式加载

在 C# 代码中显式加载 DLL：

```csharp
[DllImport("kernel32.dll", SetLastError = true)]
private static extern IntPtr LoadLibrary(string lpFileName);

private static void LoadDependencies()
{
    string appDir = AppDomain.CurrentDomain.BaseDirectory;
    string librawPath = Path.Combine(appDir, "libraw.dll");
    if (File.Exists(librawPath))
    {
        LoadLibrary(librawPath);
    }
}
```

## 验证

运行应用程序后，检查：
1. 是否出现 DLL 加载错误
2. 应用程序是否能正常初始化 SDK
3. 控制台输出是否有相关错误信息

## 常见问题

### Q: 为什么在 Visual Studio 中运行正常，但直接运行 exe 失败？

A: Visual Studio 会自动将运行时 DLL 添加到 PATH。直接运行 exe 时，需要确保运行时已安装或 DLL 在正确位置。

### Q: 如何确定缺少哪个 DLL？

A: 使用 Dependency Walker 或 Process Monitor 可以精确定位缺失的 DLL。

### Q: 可以静态链接所有依赖项吗？

A: 可以，但会增加 DLL 大小。对于 `libraw.dll`，建议保持动态链接，因为它可能很大。

