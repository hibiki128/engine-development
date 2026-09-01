#include "ShaderCompiler.h"
#include "cassert"
#include "format"
#include <debug/log/Logger.h>
#include <string/StringUtility.h>

namespace Hagine {
using namespace Logger;
using namespace StringUtility;

void ShaderCompiler::Initialize()
{
    HRESULT hr;

    // dxcCompilerを初期化
    hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&pDxcUtils_));
    assert(SUCCEEDED(hr));
    hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&pDxcCompiler_));
    assert(SUCCEEDED(hr));

    // 現時点でincludeはしないが、includeに対応するための設定を行っておく
    hr = pDxcUtils_->CreateDefaultIncludeHandler(&pIncludeHandler_);
    assert(SUCCEEDED(hr));
}

void ShaderCompiler::Finalize()
{
    // ComPtrではなく生ポインタで保持しているため手動でReleaseが必要
    if (pIncludeHandler_)
    {
        pIncludeHandler_->Release();
        pIncludeHandler_ = nullptr;
    }
    if (pDxcCompiler_)
    {
        pDxcCompiler_->Release();
        pDxcCompiler_ = nullptr;
    }
    if (pDxcUtils_)
    {
        pDxcUtils_->Release();
        pDxcUtils_ = nullptr;
    }
}

IDxcBlob *ShaderCompiler::Compile(const std::wstring &filePath, const wchar_t *profile)
{
    return CompileWithReflection(filePath, profile, nullptr);
}

IDxcBlob *ShaderCompiler::CompileWithReflection(const std::wstring &filePath, const wchar_t *profile,
                                                ID3D12ShaderReflection **ppReflection)
{
    if (ppReflection)
    {
        *ppReflection = nullptr;
    }

    // これからシェーダーをコンパイルする旨をログに出す
    Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));
    // hlslファイルを読む
    IDxcBlobEncoding *shaderSource = nullptr;
    HRESULT hr = pDxcUtils_->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    // 読めなかったら止める
    assert(SUCCEEDED(hr));
    // 読み込んだファイルの内容を設定する
    DxcBuffer shaderSourceBuffer;
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

    // 最適化の指定。
    // Debug ビルドではシェーダーデバッガで追えるように -Od（最適化なし）のままにし、
    // それ以外では -O3 を掛ける。ポストエフェクトのような重いループでは差が大きい。
#ifdef _DEBUG
    const wchar_t *optimizeOption = L"-Od";
#else
    const wchar_t *optimizeOption = L"-O3";
#endif

    LPCWSTR arguments[] = {
        filePath.c_str(),         // コンパイル対象のhlslファイル名
        L"-E", L"main",           // エントリーポイントの指定。基本的にmain以外にはしない
        L"-T", profile,           // ShaderProfileの設定
        L"-Zi", L"-Qembed_debug", // デバッグ用の情報を埋め込む
        optimizeOption,           // 最適化レベル
        L"-Zpr",                  // メモリレイアウトは行優先
    };
    // 実際にShaderをコンパイルする
    IDxcResult *shaderResult = nullptr;
    hr = pDxcCompiler_->Compile(
        &shaderSourceBuffer,        // 読み込んだファイル
        arguments,                  // コンパイルオプション
        _countof(arguments),        // コンパイルオプションの数
        pIncludeHandler_,            // includeが含まれた諸々
        IID_PPV_ARGS(&shaderResult) // コンパイル結果
    );
    // コンパイルエラーではなくdxcが起動できないなど致命的な状況
    assert(SUCCEEDED(hr));

    // 警告・エラーが出てたらログに出して止める
    IDxcBlobUtf8 *shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0)
    {
        Log(shaderError->GetStringPointer());
        // 警告・エラーダメゼッタイ
        assert(false);
    }
    // コンパイル結果から実行用のバイナリ部分を取得
    IDxcBlob *shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    assert(SUCCEEDED(hr));

    // リフレクション（シェーダーが宣言しているリソース一覧）を取り出す。
    // これを使ってルートシグネチャを自動生成する。
    if (ppReflection)
    {
        IDxcBlob *reflectionBlob = nullptr;
        hr = shaderResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr);
        if (SUCCEEDED(hr) && reflectionBlob)
        {
            DxcBuffer reflectionBuffer{};
            reflectionBuffer.Ptr = reflectionBlob->GetBufferPointer();
            reflectionBuffer.Size = reflectionBlob->GetBufferSize();
            reflectionBuffer.Encoding = DXC_CP_ACP;
            hr = pDxcUtils_->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(ppReflection));
            assert(SUCCEEDED(hr) && "シェーダーリフレクションの生成に失敗");
            reflectionBlob->Release();
        }
    }

    // 成功したログを出す
    Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));
    // もう使わないリソースを解放
    if (shaderError)
    {
        shaderError->Release();
    }
    shaderSource->Release();
    shaderResult->Release();
    // 実行用のバイナリを返却
    return shaderBlob;
}
} // namespace Hagine
