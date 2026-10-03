#pragma once
#include "Windows.h"
#include "d3d12.h"
// dxcapi.h は d3d12shader.h より先に include する必要がある
#include "dxcapi.h"
#include "d3d12shader.h"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <wrl.h>

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

    /// <summary>
    /// コンパイルが通るかだけを確かめる。失敗しても止めずに false を返す
    ///
    /// Compile / CompileWithReflection はエラーがあると assert で止まるため、
    /// ホットリロードのように「直している最中のシェーダー」を相手にすると使えない。
    /// パイプラインを作り直す前にここで確かめておけば、書きかけのHLSLを保存しても落ちない
    /// </summary>
    /// <param name="filePath">コンパイルするShaderファイルへのパス</param>
    /// <param name="profile">コンパイルに使用するProfile</param>
    /// <param name="outError">失敗したときのDXCのメッセージ（省略可）</param>
    /// <returns>bool: コンパイルが通れば true</returns>
    bool TryCompile(const std::wstring &filePath, const wchar_t *profile,
                    std::string *outError = nullptr);

    IDxcUtils *GetDxcUtils() const { return pDxcUtils_; }
    IDxcCompiler3 *GetDxcCompiler() const { return pDxcCompiler_; }

    /// <summary>
    /// コンパイル結果の使い回しを全部捨てる。
    /// .hlsli（インクルード）を書き換えたときに呼ぶ（.hlsl 自身の書き換えは更新時刻で自動的に捨てる）
    /// </summary>
    void ClearCache();

    /// <summary>コンパイル結果の使い回しの様子（使い回した回数・コンパイルした回数・控えている本数）</summary>
    void GetCacheStats(size_t &outHits, size_t &outMisses, size_t &outEntries) const;

  private:
    /// <summary>
    /// 1本ぶんのコンパイル結果。同じファイル・同じプロファイルは、ファイルの更新時刻が
    /// 変わっていない限り使い回す（起動時に FullScreen.VS を60回以上コンパイルしていたため）
    /// </summary>
    struct CachedShader
    {
        std::filesystem::file_time_type writeTime{};      // コンパイルしたときのファイルの更新時刻
        Microsoft::WRL::ComPtr<IDxcBlob> object;          // 実行用のバイナリ
        Microsoft::WRL::ComPtr<IDxcBlob> reflectionData;  // リフレクションの元データ（必要になったら作る）
    };
    std::unordered_map<std::wstring, CachedShader> cache_; // キー: パス + "|" + プロファイル
    size_t cacheHits_ = 0;
    size_t cacheMisses_ = 0;

    /// <summary>キャッシュの控えからリフレクションを作る</summary>
    void CreateReflectionFromCache(const CachedShader &cached, ID3D12ShaderReflection **ppReflection);

  private:
    // DXCコンパイラ関連
    IDxcUtils *pDxcUtils_ = nullptr;
    IDxcCompiler3 *pDxcCompiler_ = nullptr;
    // 現時点ではincludeはしないが、includeに対応するための設定を行っておく
    IDxcIncludeHandler *pIncludeHandler_ = nullptr;
};
} // namespace Hagine
