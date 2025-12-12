# LibRaw DLL 部署指南

## DLL 文件位置

LibRaw DLL 文件位于：
```
third_party\libraw\buildfiles\release-x86_64\libraw.dll
```

## 部署方式

### 方式 1: 使用部署脚本（推荐）

```powershell
.\scripts\copy_libraw_dll.ps1
```

脚本会自动：
- 查找 `libraw.dll`
- 复制到所有找到的输出目录（`bin\Release`, `bin\Debug` 等）

### 方式 2: 手动复制

#### Release 版本
```powershell
Copy-Item "third_party\libraw\buildfiles\release-x86_64\libraw.dll" "bin\Release\" -Force
```

#### Debug 版本
```powershell
Copy-Item "third_party\libraw\buildfiles\release-x86_64\libraw.dll" "bin\Debug\" -Force
```

### 方式 3: 在 Visual Studio 中配置后生成事件

在项目属性中添加后生成事件：

**Lightroom.Core 项目 → 属性 → 生成事件 → 后期生成事件**

```powershell
copy /Y "$(ProjectDir)..\..\third_party\libraw\buildfiles\release-x86_64\libraw.dll" "$(OutDir)libraw.dll"
```

这样每次编译后会自动复制 DLL。

### 方式 4: 添加到项目输出（推荐用于生产环境）

在 `Lightroom.Core.vcxproj` 中添加：

```xml
<ItemGroup>
  <Content Include="..\..\third_party\libraw\buildfiles\release-x86_64\libraw.dll">
    <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
  </Content>
</ItemGroup>
```

## 验证部署

部署后，检查输出目录：

```powershell
Test-Path "bin\Release\libraw.dll"
Test-Path "bin\Debug\libraw.dll"
```

## 运行时要求

应用程序运行时，`libraw.dll` 必须位于以下位置之一：

1. **应用程序目录**（推荐）
   - `bin\Release\libraw.dll`
   - `bin\Debug\libraw.dll`

2. **系统 PATH**
   - 不推荐，可能影响其他应用程序

3. **与可执行文件同目录**
   - 如果应用程序在子目录运行，DLL 也应在该目录

## 常见问题

### 问题 1: "找不到 libraw.dll"

**原因**: DLL 未复制到输出目录

**解决**: 运行部署脚本或手动复制 DLL

### 问题 2: "应用程序无法启动"

**原因**: DLL 架构不匹配（x86 vs x64）

**解决**: 确保使用 x64 版本的 DLL

### 问题 3: DLL 版本不匹配

**原因**: 使用了不同配置（Debug/Release）的 DLL

**解决**: 确保 DLL 与应用程序配置匹配

## 自动化部署（推荐）

在项目文件中添加后生成事件，实现自动部署：

```xml
<PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
  <PostBuildEvent>
    <Command>copy /Y "$(ProjectDir)..\..\third_party\libraw\buildfiles\release-x86_64\libraw.dll" "$(OutDir)libraw.dll"</Command>
  </PostBuildEvent>
</PropertyGroup>
```

这样每次编译 Release 版本后会自动复制 DLL。

