#pragma once
#include "DirectXCommon.h"
#include "graphics/pipeline/PipelineManager.h"
#include <graphics/pipeline/ComputePipelineManager.h>
#include <wrl.h>
namespace Hagine {
class ParticleCommon
{
  private:
    ParticleCommon() = default;
    ~ParticleCommon() = default;
    ParticleCommon(ParticleCommon &) = delete;
    ParticleCommon &operator=(ParticleCommon &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static ParticleCommon *GetInstance()
    {
        static ParticleCommon instance;
        return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon *pDxCommon);

    /// <summary>
    /// 共通描画処理
    /// </summary>
    void DrawCommonSetting(BlendMode blendMode);

    void GPUDrawCommonSetting(BlendMode blendMode);

    /// <summary>
    /// 共通描画処理
    /// </summary>
    /// <param name="blendMode"></param>
    void ComputeInitDrawCommonSetting();

    void ComputeEmitterDrawCommonSetting();

    void ComputeUpdateEmitterDrawCommonSetting();

    void ComputeCountDrawCommonSetting();

    DirectXCommon *GetDxCommon() const { return pDxCommon_; }

    /// <summary>
    /// GPU駆動描画(ExecuteIndirect)用のコマンドシグネチャを取得する。
    /// 引数は DrawIndexedInstanced 5要素のみ（ルート引数を含まないので pRootSignature=nullptr）。
    /// GPUパーティクルは「今何個描くか」を VRAM 上のカウンタから直接読ませるためにこれを使う。
    /// </summary>
    ID3D12CommandSignature *GetDrawIndexedCommandSignature() const { return drawIndexedCommandSignature_.Get(); }

  private:
    /// <summary>
    /// DrawIndexedInstanced 用コマンドシグネチャを作成する（Initialize から一度だけ呼ぶ）
    /// </summary>
    void CreateDrawIndexedCommandSignature();

  private:
    DirectXCommon *pDxCommon_ = nullptr;
    PipelineManager *pPsoManager_ = nullptr;
    ComputePipelineManager *pComputePsoManager_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> drawIndexedCommandSignature_;
};
} // namespace Hagine
