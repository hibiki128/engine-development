#pragma once
#include <array>
#include <camera/projection/ViewProjection.h>
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <type/Matrix4x4.h>
#include <wrl.h>

namespace Hagine {

/// <summary>
/// カメラビュー窓。好きなカメラから見たシーンを、エディタの別の窓に描く（最大4つ）。
///
/// メインのシーン描画とは別の描画先（色＋深度）へ、前方描画だけでもう一度描く。
/// ディファード・ポストエフェクト・パーティクルはメインの1組しか持っていないので載らない
/// （スカイボックス・オブジェクト・デバッグ線が描かれる）。
/// 描く側はこの間 RenderView の番号を見て、ビュー専用の定数バッファを使う。
/// </summary>
class SceneViewRenderer
{
  public:
    static constexpr int kViewCount = 4; ///< 同時に開ける窓の数

    static SceneViewRenderer *GetInstance();

    /// <summary>
    /// 開いている窓のぶんだけシーンを描く。
    /// 影を描いた後・メインのシーン描画の前に呼ぶ（線はメインの描画で消されるため、その前に描く）
    /// </summary>
    void Render();

    /// <summary>
    /// 開いている窓を ImGui に描く（窓の大きさがそのまま次のフレームの描画サイズになる）
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// 閉じている窓を1つ開く（表示メニューから）
    /// </summary>
    void OpenNextView();

    /// <summary>
    /// 1つでも開いているか
    /// </summary>
    bool IsAnyOpen() const;

    /// <summary>
    /// 1つ目の窓の開閉（表示メニューのチェック用）
    /// </summary>
    bool *GetFirstViewOpenFlag() { return &views_[0].open; }

    /// <summary>
    /// GPU リソースを手放す（終了時）
    /// </summary>
    void Finalize();

  private:
    /// <summary>
    /// 1つの窓
    /// </summary>
    struct View
    {
        bool open = false;
        std::string cameraName;          // 見るカメラ（空なら今アクティブなカメラ）
        bool drawSky = true;             // 空を描くか
        bool drawLines = true;           // デバッグ線（コライダー・カリングの箱など）を描くか
        bool drawParticles = true;       // パーティクルを描くか
        uint32_t width = 640;            // 次に描く大きさ（窓の大きさ）
        uint32_t height = 360;
        uint32_t renderedWidth = 0;      // 直前に描いた大きさ（表示のUVに使う）
        uint32_t renderedHeight = 0;
        std::string renderedCameraName;  // 直前に描いたカメラ（表示用）

        bool created = false;
        Microsoft::WRL::ComPtr<ID3D12Resource> color;
        Microsoft::WRL::ComPtr<ID3D12Resource> depth;
        D3D12_RESOURCE_STATES colorState = D3D12_RESOURCE_STATE_GENERIC_READ;
        uint32_t srvIndex = 0;
        // 表示用（メインと同じトーンマップを掛けた後）。ImGui にはこちらを出す
        Microsoft::WRL::ComPtr<ID3D12Resource> display;
        D3D12_RESOURCE_STATES displayState = D3D12_RESOURCE_STATE_GENERIC_READ;
        uint32_t displaySrvIndex = 0;
        uint32_t displayUavIndex = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        Microsoft::WRL::ComPtr<ID3D12Resource> lineCamera; // 線を描くときのビュー射影行列
        Matrix4x4 *pLineCamera = nullptr;
        ViewProjection viewProjection; // 描くときのカメラ（行列だけ使う。定数バッファは持たない）
    };

    SceneViewRenderer() = default;
    ~SceneViewRenderer() = default;
    SceneViewRenderer(const SceneViewRenderer &) = delete;
    SceneViewRenderer &operator=(const SceneViewRenderer &) = delete;

    void CreateTargets(int index);
    void CreateFarDepth();
    void ToneMap(View &view, uint32_t width, uint32_t height);
    static void Transition(ID3D12GraphicsCommandList *pCommandList, ID3D12Resource *pResource,
                           D3D12_RESOURCE_STATES &state, D3D12_RESOURCE_STATES next);
    void RenderOne(int index);
    void DrawViewWindow(int index);

    static constexpr uint32_t kFirstRtvSlot = 11; // RTV ヒープの空き（0〜6・8〜10 は使用中）
    static constexpr uint32_t kFirstDsvSlot = 2;  // DSV ヒープの空き（0=シーン / 1=パーティクルのプレビュー）

    std::array<View, kViewCount> views_;
    // 「全面が一番奥」の深度（ソフトパーティクルに渡す代わりの深度。RTV ヒープ 7 番で一度だけクリアする）
    Microsoft::WRL::ComPtr<ID3D12Resource> farDepth_;
    bool farDepthCleared_ = false;
    static constexpr uint32_t kFarDepthRtvSlot = 7;
    uint32_t maxWidth_ = 0;  // 描画先の大きさ（仮想解像度。窓はこの左上だけを使う）
    uint32_t maxHeight_ = 0;
};

} // namespace Hagine
