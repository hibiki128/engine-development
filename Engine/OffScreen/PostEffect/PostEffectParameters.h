#pragma once
#include "Data/DataHandler.h"
#include <memory>
#include"Graphics/Srv/SrvManager.h"

namespace Hagine {

/// <summary>
/// 各ポストエフェクトのパラメータと定数バッファを管理するクラス
/// シェーダーへのパラメータ設定、ImGuiでの調整、Json保存/読み込みを行う
/// </summary>
class PostEffectParameters {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化（各エフェクトの定数バッファを生成）
    /// </summary>
    /// <param name="dxCommon">DirectX共通処理</param>
    void Initialize(DirectXCommon *dxCommon);

    /// <summary>
    /// 指定エフェクトのパラメータをシェーダーへ設定
    /// </summary>
    /// <param name="mode">シェーダーモード</param>
    /// <param name="commandList">コマンドリスト</param>
    /// <param name="srvManager">SRVマネージャー</param>
    /// <param name="dxCommon">DirectX共通処理</param>
    void SetShaderParameters(ShaderMode mode, ID3D12GraphicsCommandList *commandList, SrvManager *srvManager, DirectXCommon *dxCommon);

    /// <summary>
    /// 時間依存パラメータを更新
    /// </summary>
    /// <param name="deltaTime">経過時間</param>
    void UpdateTimeParameters(float deltaTime);

    /// <summary>
    /// パラメータをJsonへ保存
    /// </summary>
    /// <param name="dataHandler">データハンドラ</param>
    void SaveParameters(DataHandler *dataHandler) const;

    /// <summary>
    /// パラメータをJsonから読み込み
    /// </summary>
    /// <param name="dataHandler">データハンドラ</param>
    void LoadParameters(DataHandler *dataHandler);

    /// <summary>
    /// ImGuiでのパラメータ設定UIを表示
    /// </summary>
    /// <param name="mode">対象のシェーダーモード</param>
    void DrawParameterUI(ShaderMode mode);

    /// <summary>
    /// 投影行列を設定（深度系エフェクト用の逆行列として保持）
    /// </summary>
    /// <param name="projectionMatrix">投影行列</param>
    void SetProjection(Matrix4x4 projectionMatrix) { projectionInverse_ = projectionMatrix; }

  private:
    /// ===================================================
    /// private method（各エフェクトの定数バッファ生成）
    /// ===================================================

    /// <summary>
    /// 全エフェクトの定数バッファを生成
    /// </summary>
    void CreateAllBuffers();

    void CreateSmooth();    // 平滑化
    void CreateGauss();     // ガウスぼかし
    void CreateVignette();  // ビネット
    void CreateDepth();     // 深度ぼかし
    void CreateRadial();    // ラジアルブラー
    void CreateCinematic(); // シネマティック
    void CreateDissolve();  // ディゾルブ
    void CreateRandom();    // ランダムノイズ
    void CreateFocusLine(); // 集中線
    void CreatePixelate();  // モザイク
    void CreateBloom();     // ブルーム
    void CreateRetro();     // レトロ

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    DirectXCommon *dxCommon_ = nullptr; // DirectX共通処理

    /// <summary>平滑化のパラメータ</summary>
    struct KernelSettings {
        int kernelSize;
    };

    /// <summary>ガウスぼかしのパラメータ</summary>
    struct GaussianParams {
        int kernelSize;
        float sigma;
    };

    /// <summary>ビネットのパラメータ</summary>
    struct VignetteParameter {
        float vignetteStrength;
        float vignetteRadius;
        float vignetteExponent;
        float padding;
        Vector2 vignetteCenter;
    };

    /// <summary>深度ぼかしのパラメータ</summary>
    struct Depth {
        Matrix4x4 projectionInverse;
        int kernelSize;
    };

    /// <summary>ラジアルブラーのパラメータ</summary>
    struct RadialBlur {
        Vector2 kCenter;
        float kBlurWidth;
    };

    /// <summary>シネマティックのパラメータ</summary>
    struct Cinematic {
        Vector2 iResolution;
        float contrast;
        float saturation;
        float brightness;
    };

    /// <summary>ディゾルブのパラメータ</summary>
    struct Dissolve {
        float threshold;
        float edgeWidth;
        float _pad[2];
        Vector3 edgeColor;
        float _pad1;
        bool invert;
        float _pad2[3];
    };

    /// <summary>ランダムノイズのパラメータ</summary>
    struct Random {
        float time;
    };

    /// <summary>集中線のパラメータ</summary>
    struct FocusLine {
        float time;
        float lines;
        float width;
        float speed;
        float intensity;
        float centerRadius;
        float maxDistance;
        float padding1;
        Vector4 lineColor;
    };

    /// <summary>モザイクのパラメータ</summary>
    struct Pixelate {
        float blockSize;
        float centerX;
        float centerY;
    };

    /// <summary>ブルームのパラメータ</summary>
    struct Bloom {
        float bloomThreshold;
        float bloomIntensity;
        Vector2 texelSize;
    };

    /// <summary>レトロ調のパラメータ</summary>
    struct Retro {
        float pixelSize;
        float colorLevels;
        float scanlineIntensity;
        float scanlineCount;
        float vignetteStrength;
        float chromaticOffset;
        float time;
        float resolutionX;
    };

    // 各エフェクトの定数バッファリソースとマップ先ポインタ
    Microsoft::WRL::ComPtr<ID3D12Resource> vignetteResource_;
    VignetteParameter *vignetteData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> smoothResource_;
    KernelSettings *smoothData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> gaussianResouce_;
    GaussianParams *gaussianData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> depthResouce_;
    Depth *depthData_ = nullptr;

    Matrix4x4 projectionInverse_; // 投影行列の逆行列（深度系エフェクト用）

    Microsoft::WRL::ComPtr<ID3D12Resource> radialResource_;
    RadialBlur *radialData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> cinematicResource_;
    Cinematic *cinematicData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> dissolveResource_;
    Dissolve *dissolveData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> randomResource_;
    Random *randomData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> focusLineResource_;
    FocusLine *focusLineData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> pixelateResource_;
    Pixelate *pixelateData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> bloomResource_;
    Bloom *bloomData_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12Resource> retroResource_;
    Retro *retroData_ = nullptr;

    std::string texPath_ = "debug/noise0.png"; // ノイズテクスチャのパス
};
} // namespace Hagine
