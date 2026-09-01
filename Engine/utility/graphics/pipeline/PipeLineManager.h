#pragma once
#include "d3d12.h"
#include "ShaderRootSignature.h"
#include "string/stringUtility.h"
#include "wrl.h"
#include <Asset/AssetPath.h>
#include <DirectXCommon.h>
#include <string>
#include <unordered_map>
#include <vector>

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

// ポストエフェクトのチェーン内で使うレンダーターゲットのフォーマット。
// リニア空間のFP16にしている理由:
//   ・sRGB フォーマットには UAV を作れず、コンピュートシェーダーで書けない
//   ・8bit だとエフェクトを重ねるたびに階調が落ちる（ブラー・ブルーム・DoFで顕著）
// sRGB へのエンコードはバックバッファへの最終合成時にハードウェアが行う。
inline constexpr DXGI_FORMAT kPostEffectChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// バックバッファのフォーマット（最終合成先）
inline constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

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
    Monochrome,    // 完全な白黒（明度で白or黒に二値化）
    DepthOfField,  // 被写界深度（コンピュートシェーダー専用）
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
    // ディファード: G-Buffer 書き込み。ルートシグネチャは Standard / Skinning と共通で、
    // ピクセルシェーダーと RTV フォーマットだけを差し替えたもの。
    GBuffer,
    GBufferSkinning,
    // ディファード: 全画面ライティングパス
    DeferredLighting,
    // 同じモデルを参照するオブジェクトをまとめて描くインスタンシング版。
    // ルートシグネチャは対応する非インスタンシング版と共有し（末尾のルートSRVだけ追加で使う）、
    // 頂点シェーダーだけを差し替えたもの。
    StandardInstanced,
    GBufferInstanced,
    ShadowMapInstanced,
    // ポストエフェクトの最終結果をバックバッファへ写すためだけのパイプライン。
    // チェーン内はリニアFP16、バックバッファは sRGB とフォーマットが違うため、
    // 同じ CopyImage シェーダーでも PSO を分ける必要がある。
    PresentCopy,
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
    void Initialize(DirectXCommon *pDxCommon);

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

    /// <summary>
    /// リフレクションから生成したルートシグネチャの情報を取得する。
    ///
    /// 描画側は `SetGraphicsRootConstantBufferView(0, ...)` のように番号を直書きせず、
    /// ここで得た ShaderRootSignature の GetCbvIndex("b番号") 等を経由して番号を引くこと。
    /// DXC は使われていないリソースをリフレクションから落とすため、
    /// シェーダーを編集するとルートパラメータの並びが変わりうる。
    /// </summary>
    /// <param name="type">パイプライン種別</param>
    /// <param name="shaderMode">シェーダーモード</param>
    /// <returns>const ShaderRootSignature*: リフレクションから生成していない種別では nullptr</returns>
    const ShaderRootSignature *GetReflectedRootSignature(PipelineType type,
                                                         ShaderMode shaderMode = ShaderMode::None) const;

    /// <summary>
    /// 直近の DrawCommonSetting でバインドしたルートシグネチャの情報を取得する。
    ///
    /// Material や LightGroup のように「複数のパイプラインから共有される描画コード」は、
    /// どのパイプラインで描かれているかを知らなくてよいようにこちらを使う。
    /// </summary>
    /// <returns>const ShaderRootSignature*: リフレクションから生成していないパイプラインでは nullptr</returns>
    const ShaderRootSignature *GetCurrentRootSignature() const { return pCurrentRootSignature_; }

  private:
    /// <summary>
    /// リフレクション用にコンパイルするシェーダー1本ぶんの指定
    /// </summary>
    struct ShaderStageFile
    {
        std::wstring path;                                            // shaders ルートからの相対パスを含む完全パス
        const wchar_t *profile = L"vs_6_0";                           // コンパイルプロファイル
        D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL; // このステージの可視性
    };

    /// <summary>
    /// 複数のシェーダーをコンパイルし、そのリフレクションからルートシグネチャを生成して登録する。
    /// 同じルートシグネチャを共有する派生シェーダー（インスタンシング版など）も一緒に渡すこと。
    /// 渡し漏れると、そのシェーダーだけが使うレジスタがルートシグネチャから欠ける。
    /// </summary>
    /// <param name="type">登録先のパイプライン種別</param>
    /// <param name="shaderMode">登録先のシェーダーモード</param>
    /// <param name="shaders">対象シェーダー</param>
    /// <param name="options">生成時の指定</param>
    /// <returns>生成したルートシグネチャ（失敗時は nullptr）</returns>
    Microsoft::WRL::ComPtr<ID3D12RootSignature> BuildReflectedRootSignature(
        PipelineType type, ShaderMode shaderMode,
        const std::vector<ShaderStageFile> &shaders,
        const ShaderRootSignature::BuildOptions &options);

    /// <summary>
    /// 生成済みのリフレクション情報を別のパイプライン種別へも紐づける。
    /// ルートシグネチャを共有する派生（G-Buffer版・インスタンシング版など）に使う。
    /// </summary>
    /// <param name="from">コピー元の種別</param>
    /// <param name="to">コピー先の種別</param>
    void AliasReflectedRootSignature(PipelineType from, PipelineType to);

    /// <summary>
    /// 頂点シェーダーをコンパイルし、入力レイアウトをリフレクションから組み立てる
    /// </summary>
    /// <param name="path">頂点シェーダーの完全パス</param>
    /// <param name="outLayout">組み立てた入力レイアウトの受け取り先（PSO生成まで生かしておくこと）</param>
    /// <returns>IDxcBlob*: コンパイル済みバイナリ（呼び出し側が Release すること）</returns>
    IDxcBlob *CompileVertexShaderWithLayout(const std::wstring &path, ShaderInputLayout &outLayout);

    // 内部パイプライン作成メソッド
    void CreateAllPipelines();

    // 標準パイプライン関連
    void CreateStandardPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateRootSignature();
    /// <param name="instanced">true でインスタンシング版の頂点シェーダーを使う（他は同一）</param>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, BlendMode blendMode, bool instanced = false);

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

    // ディファード関連
    void CreateDeferredPipelines();
    /// <summary>G-Buffer書き込みPSOを作る（ルートシグネチャは Standard / Skinning を流用）</summary>
    /// <param name="rootSignature">流用するルートシグネチャ</param>
    /// <param name="skinned">スキニング版の頂点シェーダーを使うか</param>
    /// <param name="instanced">インスタンシング版の頂点シェーダーを使うか</param>
    /// <param name="rootSignature">流用するルートシグネチャ</param>
    /// <param name="skinned">true ならスキニング用の頂点シェーダーを使う</param>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGBufferGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, bool skinned, bool instanced = false);
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateDeferredLightingRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateDeferredLightingGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // スカイボックス関連
    void CreateSkyboxPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateSkyboxRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateSkyboxGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature);

    // シャドウマップ関連
    void CreateShadowMapPipelines();
    Microsoft::WRL::ComPtr<ID3D12RootSignature> CreateShadowMapRootSignature();
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateShadowMapGraphicsPipeline(Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, bool instanced = false);

    /// <summary>
    /// シェーダーモードに対応するポストエフェクトのピクセルシェーダーのパスを返す
    /// </summary>
    /// <param name="shaderMode">シェーダーモード</param>
    /// <returns>std::wstring: ピクセルシェーダーの完全パス</returns>
    std::wstring GetPostEffectPixelShaderPath(ShaderMode shaderMode) const;

  private:
    DirectXCommon *pDxCommon_;

    std::wstring shaderPath = Hagine::StringUtility::ConvertString(AssetPath::EngineRoot());

    // パイプラインとルートシグネチャの格納用マップ
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pipelines_;
    std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> rootSignatures_;
    // リフレクションから生成したものは、ルートパラメータ番号を引けるようにこちらへも入れる
    std::unordered_map<std::string, ShaderRootSignature> reflectedRootSignatures_;
    // 直近に DrawCommonSetting でバインドしたもの（reflectedRootSignatures_ の要素を指す）
    const ShaderRootSignature *pCurrentRootSignature_ = nullptr;

    // キー文字列を生成するヘルパー関数
    std::string MakePipelineKey(PipelineType type, BlendMode blendMode, ShaderMode shaderMode) const;
    std::string MakeRootSignatureKey(PipelineType type, ShaderMode shaderMode) const;


    /// <summary>
    /// 全画面ポストエフェクト用のパイプラインを作る
    /// </summary>
    /// <param name="psPath">ピクセルシェーダーのパス</param>
    /// <param name="rootSignature">ルートシグネチャ</param>
    /// <param name="rtvFormat">書き込み先のフォーマット。チェーン内はリニアFP16、
    /// バックバッファへの最終合成のみ sRGB を指定する</param>
    Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateFullScreenPostEffectPipeline(const std::wstring &psPath, Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature, DXGI_FORMAT rtvFormat = kPostEffectChainFormat);

};
} // namespace Hagine
