#include "d3dUtil.h"
#include "ThrowIfFailed.h"
#include <filesystem>
#include <windows.h>

namespace
{
    std::filesystem::path GetExeDirectory()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        return std::filesystem::path(path).parent_path();
    }

    std::wstring ResolveShaderFile(const std::wstring& filename)
    {
        std::filesystem::path original(filename);
        if (std::filesystem::exists(original))
            return original.wstring();

        std::filesystem::path cwd = std::filesystem::current_path();
        std::filesystem::path exeDir = GetExeDirectory();
        std::filesystem::path name = original.filename();

        std::vector<std::filesystem::path> candidates =
        {
            cwd / name,
            cwd / "Project1" / name,
            cwd / ".." / "Project1" / name,
            exeDir / name,
            exeDir / ".." / name,
            exeDir / ".." / ".." / name,
            exeDir / ".." / "Project1" / name,
            exeDir / ".." / ".." / "Project1" / name
        };

        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate))
                return candidate.lexically_normal().wstring();
        }

        return filename;
    }
}

namespace d3dUtil
{
    Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
        const std::wstring& filename,
        const D3D_SHADER_MACRO* defines,
        const std::string& entrypoint,
        const std::string& target)
    {
        UINT compileFlags = 0;
#ifdef _DEBUG
        compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        Microsoft::WRL::ComPtr<ID3DBlob> byteCode = nullptr;
        Microsoft::WRL::ComPtr<ID3DBlob> errors;

        std::wstring resolvedFilename = ResolveShaderFile(filename);

        HRESULT hr = D3DCompileFromFile(
            resolvedFilename.c_str(),
            defines,
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entrypoint.c_str(),
            target.c_str(),
            compileFlags,
            0,
            &byteCode,
            &errors
        );

        if (errors != nullptr)
        {
            std::string errorStr = "Shader Compile Error:\n";
            errorStr += (char*)errors->GetBufferPointer();
            OutputDebugStringA(errorStr.c_str());
        }

        if (FAILED(hr))
        {
            if (errors)
            {
                MessageBoxA(0,
                    (char*)errors->GetBufferPointer(),
                    "Shader Compile Error",
                    MB_OK);
            }
            ThrowIfFailed(hr);
        }
        return byteCode;
    }

    UINT CalcConstantBufferByteSize(UINT byteSize)
    {
        // Constant buffers must be a multiple of 256 bytes.
        // Round up to nearest multiple of 256.
        return (byteSize + 255) & ~255;
    }
}
