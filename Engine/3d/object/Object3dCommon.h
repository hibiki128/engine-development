#pragma once
#include "graphics/pipeline/ComputePipelineManager.h"
#include "graphics/pipeline/PipelineManager.h"
namespace Hagine {
class Object3dCommon
{
  public: // メンバ関数
    /// <summary>
    ///  初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 共通描画設定
    /// </summary>
    void DrawCommonSetting();

    /// <summary>
    /// 共通描画設定
    /// </summary>
    void skinningDrawCommonSetting();

    /// <summary>
    /// 共通描画設定
    /// </summary>
    void computeSkinningDrawCommonSetting();

    /// <summary>
    /// ブレンドモードの切り替え
    /// </summary>
    void SetBlendMode(BlendMode blendMode);

  private:
    PipelineManager *pPsoManager_ = nullptr;
    ComputePipelineManager *pComputePsoManager_ = nullptr;
};
} // namespace Hagine
