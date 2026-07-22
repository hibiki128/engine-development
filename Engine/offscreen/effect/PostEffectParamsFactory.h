#pragma once
#include "PostEffectParamsImpl.h"
#include <memory>
#include <cassert>

/// @brief ShaderModeに対応するIPostEffectParamsを生成するファクトリ
/// 新しいエフェクトを追加する場合はここにcaseを追加する
namespace Hagine {
class PostEffectParamsFactory
{
  public:
    static std::unique_ptr<IPostEffectParams> Create(ShaderMode mode, DirectXCommon *dxCommon)
    {
        std::unique_ptr<IPostEffectParams> params;

        switch (mode)
        {
        case ShaderMode::None:
            params = std::make_unique<NoneParams>();
            break;
        case ShaderMode::Gray:
            params = std::make_unique<GrayParams>();
            break; // パラメータなし（グレイスケール化のみ・GetMode()でGrayを返す）
        case ShaderMode::Vignette:
            params = std::make_unique<VignetteParams>();
            break;
        case ShaderMode::Smooth:
            params = std::make_unique<SmoothParams>();
            break;
        case ShaderMode::Gauss:
            params = std::make_unique<GaussianParams>();
            break;
        case ShaderMode::Outline:
            params = std::make_unique<OutlineEdgeParams>();
            break;
        case ShaderMode::Depth:
            params = std::make_unique<OutlineDepthParams>();
            break;
        case ShaderMode::Blur:
            params = std::make_unique<RadialBlurParams>();
            break;
        case ShaderMode::Cinematic:
            params = std::make_unique<CinematicParams>();
            break;
        case ShaderMode::Dissolve:
            params = std::make_unique<DissolveParams>();
            break;
        case ShaderMode::Random:
            params = std::make_unique<RandomParams>();
            break;
        case ShaderMode::FocusLine:
            params = std::make_unique<FocusLineParams>();
            break;
        case ShaderMode::Pixelate:
            params = std::make_unique<PixelateParams>();
            break;
        case ShaderMode::Bloom:
            params = std::make_unique<BloomParams>();
            break;
        case ShaderMode::Retro:
            params = std::make_unique<RetroParams>();
            break;
        case ShaderMode::Shockwave:
            params = std::make_unique<ShockwaveParams>();
            break;
        case ShaderMode::Monochrome:
            params = std::make_unique<MonochromeParams>();
            break;
        default:
            assert(false && "未対応のShaderModeです。PostEffectParamsFactory::Createにcaseを追加してください。");
            params = std::make_unique<NoneParams>();
            break;
        }

        params->Initialize(dxCommon);
        return params;
    }
};
} // namespace Hagine
