cmake_minimum_required (VERSION 3.8)

project ("D3DProject")

set (CMAKE_WIN32_EXECUTABLE "True")
set (CMAKE_CXX_FLAGS "/Wall /std:c++20 /DUNICODE /TP /Zc:__cplusplus /EHs")
set (CMAKE_CXX_FLAGS_DEBUG "/Zi /MTd")
set (CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG /MT")

# Search for Direct3D library (d3d12.lib)
find_library (D3D_12 d3d12)
if (NOT D3D_12)
	message(FATAL_ERROR "Could not find Direct3D 12")
endif ()

# Search for DirectX Graphics Infrastructure library (dxgi.lib)
find_library (DX_GI dxgi)
if (NOT DX_GI)
	message(FATAL_ERROR "Could not find DXGI")
endif ()

# Search for DX_GUID library (dxguid.lib)
find_library(DX_GUID dxguid)
if (NOT DX_GUID)
    message(FATAL_ERROR "Could not find DX_GUID")
endif ()

# Search for .hlsl files compiler
find_program (FXC fxc.exe)
if (NOT FXC)
	message(FATAL_ERROR "Could not find fxc.exe")
endif ()

set(SOURCE_FILES 
	"src/MainWindow.cpp"
	"src/d3dx12.h" "src/DXSampleHelper.h"
	"src/Win32Application.h" "src/Win32Application.cpp"
	"src/DXSample.h" "src/DXSample.cpp"
	"src/D3DProject.h" "src/D3DProject.cpp"
)

# Compile .hlsl shaders
add_custom_target(
	HLSL_Shaders ALL 
	COMMAND ${FXC} /T vs_5_1 /Vn vs_main /Fh vertex_shader.h VertexShader.hlsl
	COMMAND ${FXC} /T ps_5_1 /Vn ps_main /Fh pixel_shader.h PixelShader.hlsl
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src
	VERBATIM
)

# Plik wykonywalny projektu
add_executable (D3DProject ${SOURCE_FILES})

# Dołączenie bibliotek
target_link_libraries (D3DProject ${D3D_12} ${DX_GI} ${DX_GUID})