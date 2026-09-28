#pragma once
#include "d3d12.h"
#include "wrl.h"
#include <cstdint>
#include <string>
#include <type/Matrix4x4.h>

namespace Hagine {
class DirectXCommon;
class SrvManager;

/// <summary>
/// レイトレーシングでアンビエントオクルージョンを求めるパス（インラインRT / RayQuery）。
///
/// 深度からワールド座標を戻し、法線まわりの半球へ短いレイを何本か飛ばす。
/// 当たった割合がそのまま「どれだけ周りに囲まれているか」になる。
///
/// SSAO と比べたときの利点:
///   ・**画面に写っていない物も遮蔽に数えられる**（SSAO最大の欠点がそのまま消える）
///   ・手前の物の裏側や、カメラの外にある壁が効く
///   ・半径を大きくしても破綻しない（SSAOは深度バッファの範囲に縛られる）
///
/// 代わりにレイのぶんノイズが乗るので、SSAO と同じならし（`Deferred/SsaoBlur.CS.hlsl`）を
/// そのまま通してから使う。
///
/// 実行には加速構造（RaytracingScene）が要る。
/// 非対応環境や加速構造が無いときは何もしない。
/// 有効なときは SSAO の代わりに使われる（両方掛けると二重に暗くなるため）
/// </summary>
class RtAoPass
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static RtAoPass *GetInstance()
    {
        static RtAoPass instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="pDxCommon">DirectX共通処理</param>
    /// <param name="pSrvManager">SRVマネージャー</param>
    void Initialize(DirectXCommon *pDxCommon, SrvManager *pSrvManager);

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// 遮蔽を求める。G-Buffer パスの後（深度が確定した後）に呼ぶ
    /// </summary>
    /// <param name="depthResource">シーンの深度バッファ</param>
    /// <param name="normalResource">G-Bufferの法線（無ければ何もしない）</param>
    /// <param name="inverseViewProjection">ビュー射影行列の逆行列</param>
    void Render(ID3D12Resource *depthResource, ID3D12Resource *normalResource,
                const Matrix4x4 &inverseViewProjection);

    /// <summary>
    /// このパスを使うか
    /// </summary>
    void SetEnabled(bool enabled) { enabled_ = enabled; }
    bool IsEnabled() const { return enabled_; }

    /// <summary>この環境でレイトレーシングが使えるか</summary>
    bool IsSupported() const;

    /// <summary>
    /// このフレームに遮蔽を作れたか。
    /// ライティング側はこれを見て、SSAO と使い分ける
    /// </summary>
    /// <returns>bool: 作れていれば true</returns>
    bool HasValidResult() const { return resultReady_; }

    /// <summary>
    /// ならし後の遮蔽テクスチャのSRV番号（ライティングパスが読む）
    /// </summary>
    /// <returns>uint32_t: SRV番号。未作成なら UINT32_MAX</returns>
    uint32_t GetResultSrvIndex() const { return blurredSrvIndex_; }

    /// <summary>
    /// ライティングへの効かせ具合 (0〜1)
    /// </summary>
    float GetStrength() const { return strength_; }

    /// <summary>
    /// ImGuiでの設定UI
    /// </summary>
    void DrawImGui();

    /// <summary>設定をJSONへ保存する</summary>
    /// <param name="fileName">ファイル名</param>
    void SaveData(const std::string &fileName);

    /// <summary>設定をJSONから読み込む</summary>
    /// <param name="fileName">ファイル名</param>
    void LoadData(const std::string &fileName);

  private:
    RtAoPass() = default;
    ~RtAoPass() = default;
    RtAoPass(const RtAoPass &) = delete;
    RtAoPass &operator=(const RtAoPass &) = delete;

    /// <summary>
    /// 解像度ぶんの遮蔽テクスチャを作り直す
    /// </summary>
    void CreateResources(uint32_t width, uint32_t height);

    /// <summary>
    /// 解像度が変わっていたら作り直す
    /// </summary>
    void EnsureResolution();

    /// <summary>
    /// 連続したディスクリプタ領域を押さえる
    /// </summary>
    /// <param name="count">必要な枚数</param>
    /// <returns>uint32_t: 先頭の番号。連続で取れなければ UINT32_MAX</returns>
    uint32_t AllocateContiguousTable(uint32_t count);

    /// <summary>RtAo.CS.hlsl の RtAoConstants と同じ並びにすること</summary>
    struct Constants
    {
        Matrix4x4 inverseViewProjection{}; // NDC → ワールド
        float radius = 1.2f;               // 遮蔽を探す距離
        float normalBias = 0.02f;          // 始点を法線方向へ押し出す量
        float intensity = 1.0f;            // 遮蔽の効かせ具合
        int32_t sampleCount = 8;           // 1画素あたりのレイ本数
        int32_t textureSize[2] = {0, 0};   // 出力の解像度
        float power = 1.0f;                // 陰りの立ち上がり
        float pad = 0.0f;
    };
    static_assert(sizeof(Constants) == 96, "RtAo.CS.hlsl の RtAoConstants と並びが合っていません");

    /// <summary>Deferred/SsaoBlur.CS.hlsl の SsaoBlurParameters と同じ並びにすること</summary>
    struct BlurConstants
    {
        uint32_t screenWidth = 0;
        uint32_t screenHeight = 0;
        int32_t radius = 2;            // ならす範囲（ピクセル）
        float depthThreshold = 0.0015f; // これ以上深度が違う相手は混ぜない
    };

    DirectXCommon *pDxCommon_ = nullptr; // DirectX共通処理
    SrvManager *pSrvManager_ = nullptr;  // SRVマネージャー
    bool initialized_ = false;           // 初期化済みか
    bool enabled_ = false;               // 使うかどうか（既定はOFF）
    bool resultReady_ = false;           // このフレームに遮蔽を作れたか
    float strength_ = 0.7f;              // ライティングへの効かせ具合

    uint32_t width_ = 0;  // 出力の幅
    uint32_t height_ = 0; // 出力の高さ

    // レイで求めたそのままの遮蔽と、それをならしたもの
    Microsoft::WRL::ComPtr<ID3D12Resource> rawResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> blurredResource_;
    uint32_t rawSrvIndex_ = UINT32_MAX;
    uint32_t rawUavIndex_ = UINT32_MAX;
    uint32_t blurredSrvIndex_ = UINT32_MAX;
    uint32_t blurredUavIndex_ = UINT32_MAX;

    // t0=TLAS, t1=深度, t2=法線 を並べる連続領域の先頭
    uint32_t inputTableIndex_ = UINT32_MAX;
    // t0=遮蔽, t1=深度 を並べる連続領域の先頭（ならし用）
    uint32_t blurTableIndex_ = UINT32_MAX;

    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_; // 定数バッファ
    Constants *pConstants_ = nullptr;                       // そのマップ先
    Constants settings_{};                                  // 編集中の設定

    Microsoft::WRL::ComPtr<ID3D12Resource> blurConstantBuffer_; // ならしの定数バッファ
    BlurConstants *pBlurConstants_ = nullptr;                   // そのマップ先
    BlurConstants blurSettings_{};                              // 編集中の設定
};
} // namespace Hagine
