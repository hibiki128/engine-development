#pragma once
#include "DirectXCommon.h"
#include "PipeLineManager.h"
#include "d3d12.h"
#include "string"
#include "unordered_map"
#include "wrl.h"

namespace Hagine {
enum class ComputePipelineType {
    kSkinning,
    kInitParticle,
    kEmitter,
    kUpdateEmitter,
    kResetArgs,
    kCount,
};

class ComputePipeLineManager {
  private:
    /// ====================================
    /// public method
    /// ====================================

    ComputePipeLineManager() = default;
    ~ComputePipeLineManager() = default;
    ComputePipeLineManager(ComputePipeLineManager &) = delete;
    ComputePipeLineManager &operator=(ComputePipeLineManager &) = delete;

  public:
    static ComputePipeLineManager *GetInstance() {
        static ComputePipeLineManager instance;
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
    Microsoft::WRL::ComPtr<ID3D12PipelineState> GetPipeline(ComputePipelineType type, BlendMode blendMode = BlendMode::kNormal, ShaderMode shaderMode = ShaderMode::kNone);

    /// <summary>
    /// ルートシグネチャの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12RootSignature> GetRootSignature(ComputePipelineType type, ShaderMode shaderMode = ShaderMode::kNone);

    /// <summary>
    /// 描画に必要な共通設定を行う
    /// cmdList が nullptr の場合は Direct Queue のコマンドリストを使用する
    /// </summary>
    void DrawCommonSetting(ComputePipelineType type, BlendMode blendMode = BlendMode::kNormal,
                           ShaderMode shaderMode = ShaderMode::kNone,
                           ID3D12GraphicsCommandList *cmdList = nullptr);

  private:
    // 内部パイプライン作成メソッド
    void CreateAllPipelines();

    // スキニング関連
    void CreateSkinningPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkinningRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkinningGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // パーティクル関連
    void CreateInitParticlePipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateInitParticleRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateInitParticleGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // エミッター関連
    void CreateEmitterPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateEmitterRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateEmitterGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // エミッター関連
    void CreateUpdateEmitterPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateUpdateEmitterRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateUpdateEmitterGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // パーティクルカウント関連
    void CreateCountPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateCountRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateCountGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // 生存コンパクション用カウンタリセット関連
    void CreateResetArgsPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateResetArgsRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateResetArgsGraphicsPipeLine(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

  private:
    DirectXCommon *dxCommon_;

    // パイプラインとルートシグネチャの格納用マップ
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> rootSignatures_;

    // キー文字列を生成するヘルパー関数
    std::string MakePipelineKey(ComputePipelineType type, BlendMode blendMode, ShaderMode shaderMode);
    std::string MakeRootSignatureKey(ComputePipelineType type, ShaderMode shaderMode);
};
} // namespace Hagine
