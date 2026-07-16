#pragma once
#include "DirectXCommon.h"
#include "graphics/pipeline/PipelineManager.h"
#include <graphics/pipeline/ComputePipelineManager.h>
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
    void Initialize(DirectXCommon *dxCommon);

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

  private:
    DirectXCommon *pDxCommon_ = nullptr;
    PipelineManager *pPsoManager_ = nullptr;
    ComputePipelineManager *pComputePsoManager_ = nullptr;
};
} // namespace Hagine
