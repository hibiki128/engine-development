#pragma once
#include "d3d12.h"
#include "string/stringUtility.h"
#include "wrl.h"
#include <Asset/AssetPath.h>
#include <DirectXCommon.h>
#include <string>
#include <unordered_map>

namespace Hagine {
enum class BlendMode {
    // ブレンドなし
    None,
    // 通常ブレンド
    Normal,
    // 加算
    Add,
    // 減算
    Subtract,
    // 乗算
    Multiply,
    // スクリーン
    Screen,
};

enum class ShaderMode {
    None,
    Gray,
    Vignette,
    Smooth,
    Gauss,
    Outline,
    Depth,
    Blur,
    Cinematic,
    Dissolve,
    Random,
    FocusLine,
    Pixelate,
    Bloom,
    Retro,
    Shockwave,
    Count,
};

enum class PipelineType {
    Standard,
    Particle,
    Sprite,
    Render,
    Skinning,
    Line3d,
    Skybox,
    GPUParticle,
    ShadowMap,
};

class PipelineManager {
  private:
    /// ====================================
    /// public method
    /// ====================================

    PipelineManager() = default;
    ~PipelineManager() = default;
    PipelineManager(PipelineManager &) = delete;
    PipelineManager &operator=(PipelineManager &) = delete;

  public:
    static PipelineManager *GetInstance() {
        static PipelineManager instance;
        return &instance;
    }

    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon *dxCommon);

    /// <summary>
    /// パイプラインの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipeline(PipelineType type, BlendMode blendMode = BlendMode::Normal, ShaderMode shaderMode = ShaderMode::None);

    /// <summary>
    /// ルートシグネチャの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature(PipelineType type, ShaderMode shaderMode = ShaderMode::None);

    /// <summary>
    /// 描画に必要な共通設定を行う
    /// </summary>
    void DrawCommonSetting(PipelineType type, BlendMode blendMode = BlendMode::Normal, ShaderMode shaderMode = ShaderMode::None);

  private:
    // 内部パイプライン作成メソッド
    void CreateAllPipelines();

    // 標準パイプライン関連
    void CreateStandardPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // パーティクル関連
    void CreateParticlePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateParticleRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateParticleGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // GPUパーティクル関連
    void CreateGPUParticlePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateGPUParticleRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGPUParticleGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // スプライト関連
    void CreateSpritePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSpriteRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSpriteGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // レンダー関連
    void CreateRenderPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRenderRootSignature(ShaderMode shaderMode);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateRenderGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, ShaderMode shaderMode);

    // スキニング関連
    void CreateSkinningPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkinningRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkinningGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // 3Dライン関連
    void CreateLine3dPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateLine3dRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateLine3dGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // スカイボックス関連
    void CreateSkyboxPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkyboxRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkyboxGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // シャドウマップ関連
    void CreateShadowMapPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateShadowMapRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateShadowMapGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // シェーダーモード別のルートシグネチャ作成
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateBaseRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateGrayRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateVignetteRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSmoothRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateGaussRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateOutlineRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateDepthRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateBlurRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCinematicRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateDissolveRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRandomRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateFocusLineRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreatePixelateRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateBloomRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRetroRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateShockwaveRootSignature();

    // シェーダーモード別のパイプライン作成
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateNoneGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGrayGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateVignetteGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSmoothGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGaussGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateOutlineGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateDepthGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateBlurGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCinematicGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateDissolveGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateRandomGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFocusLineGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePixelateGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateBloomGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateRetroGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateShockwaveGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

  private:
    DirectXCommon *pDxCommon_;

    std::wstring shaderPath = Hagine::StringUtility::ConvertString(AssetPath::EngineRoot());

    // パイプラインとルートシグネチャの格納用マップ
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> rootSignatures_;

    // キー文字列を生成するヘルパー関数
    std::string MakePipelineKey(PipelineType type, BlendMode blendMode, ShaderMode shaderMode);
    std::string MakeRootSignatureKey(PipelineType type, ShaderMode shaderMode);

    D3D12_STATIC_SAMPLER_DESC CreateCommonSamplerDesc();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCommonRootSignature(bool hasCBV);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFullScreenPostEffectPipeline(const std::wstring &psPath, Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    D3D12_DEPTH_STENCIL_DESC SettingDepthStencilDesc(bool depth);
};
} // namespace Hagine
