#pragma once
#include "ShaderRootSignature.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>

namespace Hagine {
class DirectXCommon;

/// <summary>
/// コンピュートシェーダー1本ぶんのパイプライン（ルートシグネチャ + PSO）。
/// ルートシグネチャはシェーダーリフレクションから自動生成されるので、
/// C++ 側にレジスタの記述を書く必要がない。
/// </summary>
struct ComputeEffectProgram
{
    ShaderRootSignature rootSignature;                          // リフレクションから生成
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;  // コンピュートPSO
    UINT threadGroupSizeX = 8;                                  // numthreads の X
    UINT threadGroupSizeY = 8;                                  // numthreads の Y
    UINT threadGroupSizeZ = 1;                                  // numthreads の Z

    bool IsValid() const { return pipelineState != nullptr && rootSignature.Get() != nullptr; }
};

/// <summary>
/// コンピュートシェーダー版ポストエフェクトのパイプライン置き場。
///
/// 使い方は「HLSL を1本置いて、そのパスで Get() するだけ」。
/// ルートシグネチャはシェーダーのリフレクションから自動で組まれ、
/// PSO は初回の Get() で作られて以降はキャッシュから返る
/// （＝毎フレーム・毎エフェクトでパイプラインを作り直すことはない）。
///
/// エフェクトを1つ増やすときにC++側で書くのは、
/// 定数バッファの構造体と、そこへ値を詰める部分だけになる。
/// </summary>
class ComputeEffectPipeline
{
  public:
    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static ComputeEffectPipeline *GetInstance()
    {
        static ComputeEffectPipeline instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    void Initialize(DirectXCommon *pDxCommon);

    /// <summary>
    /// 終了処理（生成済みパイプラインを破棄）
    /// </summary>
    void Finalize();

    /// <summary>
    /// シェーダーファイル名からコンピュートパイプラインを取得する。
    /// 未生成なら、その場でコンパイル・リフレクション・PSO生成を行ってキャッシュする。
    /// </summary>
    /// <param name="shaderFileName">shaders ルートからの相対パス（例 "OffScreen/GaussianBlur.CS.hlsl"）</param>
    /// <param name="samplerPresets">s0 から順に割り当てるサンプラー設定</param>
    /// <returns>const ComputeEffectProgram*: 生成に失敗した場合は nullptr</returns>
    const ComputeEffectProgram *Get(const std::string &shaderFileName,
                                    const std::vector<ShaderRootSignature::SamplerPreset> &samplerPresets = {});

    /// <summary>
    /// 生成済みパイプラインをすべて破棄する（シェーダーを編集して作り直したいとき用）
    /// </summary>
    void ClearCache() { programs_.clear(); }

    /// <summary>
    /// キャッシュ済みのパイプライン数（デバッグ表示用）
    /// </summary>
    size_t GetCachedCount() const { return programs_.size(); }

  private:
    ComputeEffectPipeline() = default;
    ~ComputeEffectPipeline() = default;
    ComputeEffectPipeline(const ComputeEffectPipeline &) = delete;
    ComputeEffectPipeline &operator=(const ComputeEffectPipeline &) = delete;

    /// <summary>
    /// シェーダーをコンパイルしてパイプラインを構築する
    /// </summary>
    bool Build(const std::string &shaderFileName,
               const std::vector<ShaderRootSignature::SamplerPreset> &samplerPresets,
               ComputeEffectProgram &outProgram);

    DirectXCommon *pDxCommon_ = nullptr;
    std::unordered_map<std::string, ComputeEffectProgram> programs_;
};
} // namespace Hagine
