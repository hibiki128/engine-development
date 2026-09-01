#pragma once
#include "DirectXCommon.h"
#include "PipelineManager.h"
#include "d3d12.h"
#include "string"
#include "unordered_map"
#include "wrl.h"

namespace Hagine {
enum class ComputePipelineType {
    Skinning,
    InitParticle,
    Emitter,
    UpdateEmitter,
    UpdateEmitterLite, // 演出なし専用の軽量 Update（root sig は UpdateEmitter と共有）
    ResetArgs,
    LightCulling,      // ディファードのタイルベースライトカリング
    ParticleLightGen,  // GPUパーティクルの粒子から動的ポイントライトを生成する
    Count,
};

class ComputePipelineManager {
  private:
    /// ====================================
    /// public method
    /// ====================================

    ComputePipelineManager() = default;
    ~ComputePipelineManager() = default;
    ComputePipelineManager(ComputePipelineManager &) = delete;
    ComputePipelineManager &operator=(ComputePipelineManager &) = delete;

  public:
    static ComputePipelineManager *GetInstance() {
        static ComputePipelineManager instance;
        return &instance;
    }

    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(DirectXCommon *pDxCommon);

    /// <summary>
    /// パイプラインの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipeline(ComputePipelineType type, BlendMode blendMode = BlendMode::Normal, ShaderMode shaderMode = ShaderMode::None);

    /// <summary>
    /// ルートシグネチャの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature(ComputePipelineType type, ShaderMode shaderMode = ShaderMode::None);

    /// <summary>
    /// 描画に必要な共通設定を行う
    /// pCommandList が nullptr の場合は Direct Queue のコマンドリストを使用する
    /// </summary>
    void DrawCommonSetting(ComputePipelineType type, BlendMode blendMode = BlendMode::Normal,
                           ShaderMode shaderMode = ShaderMode::None,
                           ID3D12GraphicsCommandList *pCommandList = nullptr);

  private:
    // 内部パイプライン作成メソッド
    void CreateAllPipelines();

    // スキニング関連
    void CreateSkinningPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkinningRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkinningGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // パーティクル関連
    void CreateInitParticlePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateInitParticleRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateInitParticleGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // エミッター関連
    void CreateEmitterPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateEmitterRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateEmitterGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // エミッター関連
    void CreateUpdateEmitterPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateUpdateEmitterRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateUpdateEmitterGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);
    // 演出なし専用の軽量 Update PSO（root sig は CreateUpdateEmitterRootSignature を共有）
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateUpdateEmitterLiteGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // パーティクルカウント関連
    void CreateCountPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCountRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCountGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // 生存コンパクション用カウンタリセット関連
    void CreateResetArgsPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateResetArgsRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateResetArgsGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // ディファードのライトカリング
    void CreateLightCullingPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateLightCullingRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateLightCullingGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // 粒子から動的ポイントライトを生成する
    void CreateParticleLightGenPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateParticleLightGenRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateParticleLightGenGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

  private:
    DirectXCommon *pDxCommon_;

    std::wstring shaderPath = Hagine::StringUtility::ConvertString(AssetPath::EngineRoot());

    // パイプラインとルートシグネチャの格納用マップ
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> rootSignatures_;

    // キー文字列を生成するヘルパー関数
    std::string MakePipelineKey(ComputePipelineType type, BlendMode blendMode, ShaderMode shaderMode);
    std::string MakeRootSignatureKey(ComputePipelineType type, ShaderMode shaderMode);
};
} // namespace Hagine
