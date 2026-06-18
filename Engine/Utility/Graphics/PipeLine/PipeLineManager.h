#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <DirectXCommon.h>
#include <string>
#include <unordered_map>

namespace Hagine {
enum class BlendMode {
    // ブレンドなし
    kNone,
    // 通常ブレンド
    kNormal,
    // 加算
    kAdd,
    // 減算
    kSubtract,
    // 乗算
    kMultiply,
    // スクリーン
    kScreen,
};

enum class ShaderMode {
    kNone,
    kGray,
    kVigneet,
    kSmooth,
    kGauss,
    kOutLine,
    kDepth,
    kBlur,
    kCinematic,
    kDissolve,
    kRandom,
    kFocusLine,
    kPixelate,
    kBloom,
    kRetro,
    kShockwave,
    kCount,
};

enum class PipelineType {
    kStandard,
    kParticle,
    kSprite,
    kRender,
    kSkinning,
    kLine3d,
    kSkybox,
    kGPUParticle,
    kShadowMap,
};

class PipeLineManager {
  private:
    /// ====================================
    /// public method
    /// ====================================

    PipeLineManager() = default;
    ~PipeLineManager() = default;
    PipeLineManager(PipeLineManager &) = delete;
    PipeLineManager &operator=(PipeLineManager &) = delete;

  public:
      static PipeLineManager* GetInstance() {
        static PipeLineManager instance;
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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipeline(PipelineType type, BlendMode blendMode = BlendMode::kNormal, ShaderMode shaderMode = ShaderMode::kNone);

    /// <summary>
    /// ルートシグネチャの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature(PipelineType type, ShaderMode shaderMode = ShaderMode::kNone);

    /// <summary>
    /// 描画に必要な共通設定を行う
    /// </summary>
    void DrawCommonSetting(PipelineType type, BlendMode blendMode = BlendMode::kNormal, ShaderMode shaderMode = ShaderMode::kNone);

  private:
    // 内部パイプライン作成メソッド
    void CreateAllPipelines();

    // 標準パイプライン関連
    void CreateStandardPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // パーティクル関連
    void CreateParticlePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateParticleRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateParticleGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // GPUパーティクル関連
    void CreateGPUParticlePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateGPUParticleRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGPUParticleGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // スプライト関連
    void CreateSpritePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSpriteRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSpriteGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode);

    // レンダー関連
    void CreateRenderPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRenderRootSignature(ShaderMode shaderMode);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateRenderGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, ShaderMode shaderMode);

    // スキニング関連
    void CreateSkinningPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkinningRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkinningGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // 3Dライン関連
    void CreateLine3dPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateLine3dRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateLine3dGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // スカイボックス関連
    void CreateSkyboxPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkyboxRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkyboxGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // シャドウマップ関連
    void CreateShadowMapPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateShadowMapRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateShadowMapGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);


    // シェーダーモード別のルートシグネチャ作成
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateBaseRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateGrayRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateVignetteRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSmoothRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateGaussRootSignature();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateOutLineRootSignature();
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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateNoneGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGrayGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateVigneetGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSmoothGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGaussGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateOutLineGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateDepthGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateBlurGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCinematicGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateDissolveGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateRandomGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFocusLineGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePixelateGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateBloomGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateRetroGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateShockwaveGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

  private:
    DirectXCommon *dxCommon_;

    // パイプラインとルートシグネチャの格納用マップ
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> rootSignatures_;

    // キー文字列を生成するヘルパー関数
    std::string MakePipelineKey(PipelineType type, BlendMode blendMode, ShaderMode shaderMode);
    std::string MakeRootSignatureKey(PipelineType type, ShaderMode shaderMode);

    D3D12_STATIC_SAMPLER_DESC CreateCommonSamplerDesc();

    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCommonRootSignature(bool hasCBV);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFullScreenPostEffectPipeline(const std::wstring& psPath,Microsoft::WRL::ComPtr<ID3D12RootSignature>rootSignature);

    D3D12_DEPTH_STENCIL_DESC SettingDepthStencilDesc(bool depth);
};
} // namespace Hagine
