import os
import uuid

base_dir = r"src\Lightroom.Core"
rhi_dir = os.path.join(base_dir, "d3d11rhi")

filters_content = f"""<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <Filter Include="Source Files">
      <UniqueIdentifier>{{{str(uuid.uuid4())}}}</UniqueIdentifier>
      <Extensions>cpp;c;cc;cxx;c++;def;odl;idl;hpj;bat;asm;asmx</Extensions>
    </Filter>
    <Filter Include="Header Files">
      <UniqueIdentifier>{{{str(uuid.uuid4())}}}</UniqueIdentifier>
      <Extensions>h;hh;hpp;hxx;h++;hm;inl;inc;ipp;xsd</Extensions>
    </Filter>
    <Filter Include="d3d11rhi">
      <UniqueIdentifier>{{{str(uuid.uuid4())}}}</UniqueIdentifier>
    </Filter>
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="LightroomSDK.cpp">
      <Filter>Source Files</Filter>
    </ClCompile>
"""

# Add d3d11rhi cpp files
for f in os.listdir(rhi_dir):
    if f.endswith(".cpp"):
        filters_content += f"""    <ClCompile Include="d3d11rhi\\{f}">
      <Filter>d3d11rhi</Filter>
    </ClCompile>
"""

filters_content += """  </ItemGroup>
  <ItemGroup>
    <ClInclude Include="LightroomSDK.h">
      <Filter>Header Files</Filter>
    </ClInclude>
"""

# Add d3d11rhi h files
for f in os.listdir(rhi_dir):
    if f.endswith(".h"):
        filters_content += f"""    <ClInclude Include="d3d11rhi\\{f}">
      <Filter>d3d11rhi</Filter>
    </ClInclude>
"""

filters_content += """  </ItemGroup>
</Project>
"""

with open(os.path.join(base_dir, "Lightroom.Core.vcxproj.filters"), "w", encoding="utf-8") as f:
    f.write(filters_content)

print("Generated Lightroom.Core.vcxproj.filters")



