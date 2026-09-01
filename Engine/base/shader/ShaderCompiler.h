#pragma once
#include "Windows.h"
#include "d3d12.h"
// dxcapi.h は d3d12shader.h より先に include する必要がある
#include "dxcapi.h"
#include "d3d12shader.h"
#include <string>

namespace Hagine {

/// <summary>
/// シェーダーコンパイラクラス
/// DXC（DirectX Shader Compiler）の初期化と HLSL のコンパイルを担当する
/// </summary>
class ShaderCompiler
{
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

    /// <summary>
    /// シェーダーをコンパイルし、リフレクション情報も取得する
    /// リフレクションを使うと、シェーダーが宣言している定数バッファ・テクスチャ・UAV・サンプラーを
    /// バイナリから読み出せるため、ルートシグネチャを手書きしなくて済む
    /// </summary>
    /// <param name="filePath">コンパイルするShaderファイルへのパス</param>
    /// <param name="profile">コンパイルに使用するProfile</param>
    /// <param name="ppReflection">リフレクションの受け取り先（呼び出し側が Release すること）</param>
    /// <returns>コンパイル済みバイナリ（呼び出し側が Release すること）</returns>
    IDxcBlob *CompileWithReflection(const std::wstring &filePath, const wchar_t *profile,
                                    ID3D12ShaderReflection **ppReflection);

    IDxcUtils *GetDxcUtils() const { return pDxcUtils_; }
    IDxcCompiler3 *GetDxcCompiler() const { return pDxcCompiler_; }

  private:
    // DXCコンパイラ関連
    IDxcUtils *pDxcUtils_ = nullptr;
    IDxcCompiler3 *pDxcCompiler_ = nullptr;
    // 現時点ではincludeはしないが、includeに対応するための設定を行っておく
    IDxcIncludeHandler *pIncludeHandler_ = nullptr;
};
} // namespace Hagine
