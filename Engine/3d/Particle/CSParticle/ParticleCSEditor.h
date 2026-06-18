#pragma once
#include "Particle/ParticleStruct.h"
#include "ParticleCSEmitter.h"
#include "ParticleCSGroupManager.h"
#include "map"
#include <unordered_map>
namespace Hagine {

/// <summary>
/// GPUパーティクルのエミッター・グループをImGuiで編集するエディタ（シングルトン）
/// エミッターの追加・描画（Compute/Graphicsの2フェーズ）・統計表示・プレビュー窓を提供する
/// </summary>
class ParticleCSEditor {
  private:
    /// ===================================
    /// private methods
    /// ===================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ParticleCSEditor() = default;
    ParticleCSEditor(const ParticleCSEditor &) = delete;
    ParticleCSEditor &operator=(const ParticleCSEditor &) = delete;

  private:
    /// ===================================
    /// private variaus
    /// ===================================

    std::unordered_map<std::string, std::unique_ptr<ParticleCSEmitter>> emitters_;
    int selectedEmitterIndex_ = 0;
    std::string selectedEmitterName_;

    ParticleCSGroupManager *particleGroupManager_ = nullptr;

    std::string localName_;
    std::string localFileObj_;
    std::string localTexturePath_;
    std::string localEmitterName_;
    int localMaxParticleCount_ = 1000;
    PrimitiveType localType_ = PrimitiveType::None;

#ifdef USE_IMGUI
    // CollapsingHeaderの色を定義
    ImVec4 headerColors_[6];
#endif // USE_IMGUI

    bool isLoad_ = false;
    std::string name_;
    std::string fileName_;
    std::string texturePath_;
    int maxParticleCount_ = 1000;

  private:
    void SetupColors();

    bool ColoredCollapsingHeader(const char *label, int colorIndex);

    void ShowFileSelector();

    // エミッター・グループの一覧表示と削除UIを表示
    void ShowDeleteSection();

    std::vector<std::string> GetJsonFiles();
    std::string localEmitterModelPath_;
    PrimitiveType localEmitterType_ = PrimitiveType::None;

  public:
    // インスタンスの取得
    static ParticleCSEditor *GetInstance() {
        static ParticleCSEditor instance;
        return &instance;
    }
    // 終了処理
    void Finalize();
    // 初期化
    void Initialize();
    // パーティクルエミッター追加（名前指定）
    void AddParticleEmitter(const std::string &name);
    void AddParticleEmitter(const std::string &name, const std::string &modelPath);
    void AddParticleEmitter(const std::string &name, PrimitiveType primitiveType);
    // パーティクルグループ追加（OBJモデル使用）
    void AddParticleGroup(const std::string &name, const std::string &fileName, uint32_t maxParticleCount, const std::string &texturePath);
    // パーティクルグループ追加（プリミティブ使用）
    void AddPrimitiveParticleGroup(const std::string &name, PrimitiveType type, uint32_t maxParticleCount, const std::string &texturePath);
    std::unique_ptr<ParticleCSEmitter> CreateEmitterFromTemplate(const std::string &name);
    void ShowGPUParticleStatistics();
    // ImGuiエディターの表示
    void EditorWindow();
    // すべてのエミッターを描画（後方互換：Compute→Execute→Wait→Graphics を内部完結）
    void DrawAll(const ViewProjection &vp_);
    // Compute フェーズのみ（DrawSystem::kGPUParticleCompute ステージから呼ぶ）
    void DrawAllCompute(const ViewProjection &vp_);
    // Graphics フェーズのみ（Compute 実行済み後に呼ぶ）
    void DrawAllGraphics(const ViewProjection &vp_);
    // すべてのエミッターのデバッグ情報を表示
    void DebugAll();
    // ImGuiエディターの表示処理
    void ShowImGuiEditor();
    // データのロード
    void Load();
    // エミッターを名前指定で削除
    void RemoveParticleEmitter(const std::string &name);

    // 登録済みエミッター名の一覧を取得（描画グループ管理UIで使用）
    std::vector<std::string> GetEmitterNames() const;
    // 名前からエミッターを取得（なければ nullptr）
    ParticleCSEmitter *GetEmitterByName(const std::string &name);

    // ===== プレビュー窓 (Phase 8) =====
    // 専用オフスクリーンRTを生成する（初回のみ）。Initialize から呼ぶ。
    void InitializePreview();
    // プレビューRTへ描画する。DrawSystem::Draw 冒頭(direct リスト記録中・ステージ束ね前)で呼ぶこと。
    void RenderPreview();
    // ImGui にプレビュー画像を表示する（ImGui構築フェーズで呼ぶ）。pOpen はウィンドウのXボタンと連動する表示フラグ。
    void ShowPreviewWindow(bool *pOpen = nullptr);

  private:
    /// ===== プレビュー窓 内部状態 =====
    // RT/深度はクライアント解像度ぶんを最大確保し、実描画は ImGui ウィンドウのサイズに合わせて
    // ビューポート＋UV部分表示で可変にする（リソース再確保による frame-latency ハザードを回避）。
    static constexpr uint32_t kPreviewMaxWidth_ = 1760;  // = WinApp::kClientWidth
    static constexpr uint32_t kPreviewMaxHeight_ = 990;  // = WinApp::kClientHeight
    uint32_t previewRenderWidth_ = 512;                  // 今フレームの実描画幅（ImGuiウィンドウ依存）
    uint32_t previewRenderHeight_ = 512;                 // 今フレームの実描画高
    Microsoft::WRL::ComPtr<ID3D12Resource> previewColorResource_;
    uint32_t previewColorSrvIndex_ = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE previewRtvHandle_{};
    D3D12_RESOURCE_STATES previewColorState_ = D3D12_RESOURCE_STATE_GENERIC_READ;
    bool previewInitialized_ = false;

    // 専用深度バッファ（DSV スロット1）。サンプリングしないので常時 DEPTH_WRITE。
    Microsoft::WRL::ComPtr<ID3D12Resource> previewDepthResource_;
    D3D12_CPU_DESCRIPTOR_HANDLE previewDsvHandle_{};

    // 白グリッド線（共有 DrawLine3D とは独立した専用VB）。kLine3d PSO を流用して描画する。
    struct PreviewLineVertex {
        Vector3 pos;
        Vector4 color;
    };
    static constexpr int kPreviewGridMaxDivision_ = 600; // VB容量の上限（(div+1)*4 頂点）。ほぼ無限グリッド用に拡大。
    Microsoft::WRL::ComPtr<ID3D12Resource> previewGridVB_;
    D3D12_VERTEX_BUFFER_VIEW previewGridVBView_{};
    uint32_t previewGridVertexCount_ = 0;
    PreviewLineVertex *previewGridMapped_ = nullptr; // 永続マップ（設定変更時に内容だけ書き換える）

    // 選択エミッタのワイヤーフレーム線（共有 DrawLine3D を使わずプレビュー専用VBで描画）。
    static constexpr uint32_t kPreviewWireMaxVerts_ = 24000; // 上限（超過分は切り捨て）
    Microsoft::WRL::ComPtr<ID3D12Resource> previewWireVB_;
    D3D12_VERTEX_BUFFER_VIEW previewWireVBView_{};
    uint32_t previewWireVertexCount_ = 0;
    PreviewLineVertex *previewWireMapped_ = nullptr;

    // プレビュー表示設定（背景色・グリッド）
    float previewBgColor_[4] = {0.02f, 0.02f, 0.03f, 1.0f};
    bool previewShowGrid_ = true;
    bool previewShowEmitterWire_ = true; // 選択エミッタのワイヤーフレームをプレビューに描く
    int previewGridDivision_ = 300;      // 線の本数（半径方向。半径 = 分割数 × 間隔 / 2）
    float previewGridHalfSize_ = 150.0f; // グリッド半径（カメラ注視点を中心に追従し無限風に見せる）
    // 既定はやや暗いグレー（線PSOは不透明描画なので暗背景に対して半透明風に見える）。
    Vector4 previewGridColor_ = {0.28f, 0.28f, 0.32f, 1.0f};
    bool previewGridDirty_ = false; // グリッド設定が変わったら true（RenderPreview で再構築）

    // プレビューカメラの viewProject を渡す CB（kLine3d ルートパラメータ0）。
    Microsoft::WRL::ComPtr<ID3D12Resource> previewLineCB_;
    Matrix4x4 *previewLineCBData_ = nullptr;

    // 選択エミッタをプレビューRTへ隔離描画するための専用 per-view CB（共有グループの VP を汚さない）。
    Microsoft::WRL::ComPtr<ID3D12Resource> previewPerViewCB_;
    PerView *previewPerViewData_ = nullptr;

    // オービットカメラ（8c でマウス操作を追加予定）。
    float previewCamYaw_ = 0.6f;
    float previewCamPitch_ = 0.45f;
    float previewCamDistance_ = 16.0f;
    Vector3 previewCamTarget_ = {0.0f, 0.0f, 0.0f};

    // グリッド頂点バッファを最大容量で確保し永続マップする（初回のみ）。
    void BuildPreviewGrid();
    // 現在のグリッド設定（分割数/サイズ/色）をマップ済みVBへ書き込み、頂点数を更新する。
    void RebuildPreviewGridContents();
    // ワイヤーフレーム用VBを最大容量で確保し永続マップする（初回のみ）。
    void BuildPreviewWireBuffer();
    // 現在のカメラパラメータから view 行列と view*projection 行列を計算する。
    void ComputePreviewMatrices(Matrix4x4 &outView, Matrix4x4 &outViewProj) const;
};
} // namespace Hagine
