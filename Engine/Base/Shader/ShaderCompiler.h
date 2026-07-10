#pragma once
#include "Windows.h"
#include "d3d12.h"
#include "dxcapi.h"
#include <string>

namespace Hagine {

/// <summary>
/// シェーダーコンパイラクラス
/// DXC（DirectX Shader Compiler）の初期化と HLSL のコンパイルを担当する
/// </summary>
class ShaderCompiler {
  public:
    ShaderCompiler() = default;
    ~ShaderCompiler() = default;
    ShaderCompiler(const ShaderCompiler &) = delete;
    ShaderCompiler &operator=(const ShaderCompiler &) = delete;

    /// <summary>
    /// 初期化（DXCコンパイラの生成）
    /// </summary>
    void Initialize();

    /// <summary>
    /// 終了処理（生ポインタで保持しているDXC関連の解放）
    /// </summary>
    void Finalize();

    /// <summary>
    /// シェーダーをコンパイルする
    /// </summary>
    /// <param name="filePath">コンパイルするShaderファイルへのパス</param>
    /// <param name="profile">コンパイルに使用するProfile</param>
    /// <returns>コンパイル済みバイナリ</returns>
    IDxcBlob *Compile(const std::wstring &filePath, const wchar_t *profile);

    IDxcUtils *GetDxcUtils() const { return dxcUtils_; }
    IDxcCompiler3 *GetDxcCompiler() const { return dxcCompiler_; }

  private:
    // DXCコンパイラ関連
    IDxcUtils *dxcUtils_ = nullptr;
    IDxcCompiler3 *dxcCompiler_ = nullptr;
    // 現時点ではincludeはしないが、includeに対応するための設定を行っておく
    IDxcIncludeHandler *includeHandler_ = nullptr;
};
} // namespace Hagine
