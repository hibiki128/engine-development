#pragma once
#include "PostEffectParamsImpl.h"
#include <memory>
#include <cassert>

/// @brief ShaderModeに対応するIPostEffectParamsを生成するファクトリ
/// 新しいエフェクトを追加する場合はここにcaseを追加する
namespace Hagine {
class PostEffectParamsFactory {
  public:
    static std::unique_ptr<IPostEffectParams> Create(ShaderMode mode, DirectXCommon *dxCommon) {
        std::unique_ptr<IPostEffectParams> params;

        switch (mode) {
        case ShaderMode::kNone:         params = std::make_unique<NoneParams>();         break;
        case ShaderMode::kGray:         params = std::make_unique<NoneParams>();         break; // パラメータなし
        case ShaderMode::kVigneet:     params = std::make_unique<VignetteParams>();     break;
        case ShaderMode::kSmooth:       params = std::make_unique<SmoothParams>();       break;
        case ShaderMode::kGauss:        params = std::make_unique<GaussianParams>();     break;
        case ShaderMode::kOutLine:      params = std::make_unique<OutlineEdgeParams>();  break;
        case ShaderMode::kDepth:        params = std::make_unique<OutlineDepthParams>(); break;
        case ShaderMode::kBlur:         params = std::make_unique<RadialBlurParams>();   break;
        case ShaderMode::kCinematic:    params = std::make_unique<CinematicParams>();    break;
        case ShaderMode::kDissolve:     params = std::make_unique<DissolveParams>();     break;
        case ShaderMode::kRandom:       params = std::make_unique<RandomParams>();       break;
        case ShaderMode::kFocusLine:    params = std::make_unique<FocusLineParams>();    break;
        case ShaderMode::kPixelate:     params = std::make_unique<PixelateParams>();     break;
        case ShaderMode::kBloom:        params = std::make_unique<BloomParams>();        break;
        case ShaderMode::kRetro:        params = std::make_unique<RetroParams>();        break;
        case ShaderMode::kShockwave:    params = std::make_unique<ShockwaveParams>();    break;
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
