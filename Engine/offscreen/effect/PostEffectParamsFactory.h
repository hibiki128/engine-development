#pragma once
#include "PostEffectParamsImpl.h"
#include "PostEffectParamsFx.h"
#include <memory>
#include <cassert>

/// @brief ShaderModeに対応するIPostEffectParamsを生成するファクトリ
/// 新しいエフェクトを追加する場合はここにcaseを追加する
namespace Hagine {
class PostEffectParamsFactory
{
  public:
    static std::unique_ptr<IPostEffectParams> Create(ShaderMode mode, DirectXCommon *pDxCommon)
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
        case ShaderMode::Fxaa:
            params = std::make_unique<FxaaParams>();
            break;
        case ShaderMode::ColorGrading:
            params = std::make_unique<ColorGradingParams>();
            break;
        case ShaderMode::ChromaticAberration:
            params = std::make_unique<ChromaticAberrationParams>();
            break;
        case ShaderMode::FilmGrain:
            params = std::make_unique<FilmGrainParams>();
            break;
        case ShaderMode::LensDistortion:
            params = std::make_unique<LensDistortionParams>();
            break;
        case ShaderMode::Monochrome:
            params = std::make_unique<MonochromeParams>();
            break;
        case ShaderMode::DepthOfField:
            params = std::make_unique<DepthOfFieldParams>();
            break;
        case ShaderMode::HeightFog:
            params = std::make_unique<HeightFogParams>();
            break;
        case ShaderMode::LightShaft:
            params = std::make_unique<LightShaftParams>();
            break;
        case ShaderMode::RtReflection:
            params = std::make_unique<RtReflectionParams>();
            break;
        case ShaderMode::Ssr:
            params = std::make_unique<SsrParams>();
            break;
        case ShaderMode::Impact:
            params = std::make_unique<ImpactParams>();
            break;
        case ShaderMode::Sepia:
            params = std::make_unique<SepiaParams>();
            break;
        case ShaderMode::Posterize:
            params = std::make_unique<PosterizeParams>();
            break;
        case ShaderMode::GradientMap:
            params = std::make_unique<GradientMapParams>();
            break;
        case ShaderMode::Invert:
            params = std::make_unique<InvertParams>();
            break;
        case ShaderMode::HueSaturation:
            params = std::make_unique<HueSaturationParams>();
            break;
        case ShaderMode::ColorIsolation:
            params = std::make_unique<ColorIsolationParams>();
            break;
        case ShaderMode::Thermal:
            params = std::make_unique<ThermalParams>();
            break;
        case ShaderMode::NightVision:
            params = std::make_unique<NightVisionParams>();
            break;
        case ShaderMode::Sharpen:
            params = std::make_unique<SharpenParams>();
            break;
        case ShaderMode::ColorOverlay:
            params = std::make_unique<ColorOverlayParams>();
            break;
        case ShaderMode::Kuwahara:
            params = std::make_unique<KuwaharaParams>();
            break;
        case ShaderMode::Watercolor:
            params = std::make_unique<WatercolorParams>();
            break;
        case ShaderMode::Toon:
            params = std::make_unique<ToonParams>();
            break;
        case ShaderMode::Sketch:
            params = std::make_unique<SketchParams>();
            break;
        case ShaderMode::Halftone:
            params = std::make_unique<HalftoneParams>();
            break;
        case ShaderMode::Ascii:
            params = std::make_unique<AsciiParams>();
            break;
        case ShaderMode::Dither:
            params = std::make_unique<DitherParams>();
            break;
        case ShaderMode::Emboss:
            params = std::make_unique<EmbossParams>();
            break;
        case ShaderMode::ShapeMosaic:
            params = std::make_unique<ShapeMosaicParams>();
            break;
        case ShaderMode::Swirl:
            params = std::make_unique<SwirlParams>();
            break;
        case ShaderMode::Bulge:
            params = std::make_unique<BulgeParams>();
            break;
        case ShaderMode::Wave:
            params = std::make_unique<WaveParams>();
            break;
        case ShaderMode::Kaleidoscope:
            params = std::make_unique<KaleidoscopeParams>();
            break;
        case ShaderMode::Mirror:
            params = std::make_unique<MirrorParams>();
            break;
        case ShaderMode::HeatHaze:
            params = std::make_unique<HeatHazeParams>();
            break;
        case ShaderMode::Underwater:
            params = std::make_unique<UnderwaterParams>();
            break;
        case ShaderMode::RainLens:
            params = std::make_unique<RainLensParams>();
            break;
        case ShaderMode::FrostedGlass:
            params = std::make_unique<FrostedGlassParams>();
            break;
        case ShaderMode::Dizzy:
            params = std::make_unique<DizzyParams>();
            break;
        case ShaderMode::TiltShift:
            params = std::make_unique<TiltShiftParams>();
            break;
        case ShaderMode::MotionBlur:
            params = std::make_unique<MotionBlurParams>();
            break;
        case ShaderMode::DirectionalBlur:
            params = std::make_unique<DirectionalBlurParams>();
            break;
        case ShaderMode::LensFlare:
            params = std::make_unique<LensFlareParams>();
            break;
        case ShaderMode::AnamorphicStreak:
            params = std::make_unique<AnamorphicStreakParams>();
            break;
        case ShaderMode::ScreenGodRays:
            params = std::make_unique<ScreenGodRaysParams>();
            break;
        case ShaderMode::NeonEdge:
            params = std::make_unique<NeonEdgeParams>();
            break;
        case ShaderMode::Crt:
            params = std::make_unique<CrtParams>();
            break;
        case ShaderMode::Glitch:
            params = std::make_unique<GlitchParams>();
            break;
        case ShaderMode::Vhs:
            params = std::make_unique<VhsParams>();
            break;
        case ShaderMode::OldFilm:
            params = std::make_unique<OldFilmParams>();
            break;
        case ShaderMode::Afterimage:
            params = std::make_unique<AfterimageParams>();
            break;
        case ShaderMode::Letterbox:
            params = std::make_unique<LetterboxParams>();
            break;
        case ShaderMode::Spotlight:
            params = std::make_unique<SpotlightParams>();
            break;
        case ShaderMode::DangerVignette:
            params = std::make_unique<DangerVignetteParams>();
            break;
        case ShaderMode::WorldScan:
            params = std::make_unique<WorldScanParams>();
            break;
        default:
            assert(false && "未対応のShaderModeです。PostEffectParamsFactory::Createにcaseを追加してください。");
            params = std::make_unique<NoneParams>();
            break;
        }

        params->Initialize(pDxCommon);
        return params;
    }
};
} // namespace Hagine
