#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "OffScreen.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Hagine {
class DirectXCommon;
class SrvManager;
class SceneManager;
class CollisionManager;

/// <summary>
/// 描画レイヤー（後方互換用）
/// </summary>
enum class DrawLayer {
    kPreEffect = 0,
    kPostEffect = 1,
};

/// <summary>
/// 描画エントリ（登録された描画処理1件分の情報）
/// </summary>
struct DrawEntry {
    std::string name;                                       // エントリ名
    int stageIndex = 0;                                     // 0,1,2... = 3Dステージ; kUILayer = UI（ポストエフェクトなし）
    std::function<void(const ViewProjection &)> draw;       // 描画処理
    bool enabled = true;                                    // 有効フラグ
};

/// <summary>
/// 描画パイプライン全体を管理するシングルトン
///
/// 【ステージ仕様】
///   stageIndex 0,1,2... : 各ステージで3Dオブジェクトを描画しポストエフェクトを適用。
///                         ステージN+1にはN以前の結果が背景として合成される。
///   stageIndex kUILayer : ポストエフェクトなし。全ステージ結果の上にUI/スプライトを描画。
///
/// 【描画順】
///   stage0 → PostEffect0 → stage1(+bg) → PostEffect1 → ... → UI → SceneTransition
/// </summary>
class DrawSystem {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    static constexpr int kUILayer = -1; // UIレイヤー（ポストエフェクトなし）
    // GPU パーティクル Compute フェーズ専用ステージ
    // 3D ステージより先に実行され、全エミッターのコンピュートを一括実行して Direct Queue に Wait を挿入する
    static constexpr int kGPUParticleCompute = -2;

    /// <summary>
    /// インスタンスを取得
    /// </summary>
    /// <returns>DrawSystem*: シングルトンインスタンス</returns>
    static DrawSystem *GetInstance() {
        static DrawSystem instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="dxCommon">DirectX共通処理</param>
    /// <param name="srvManager">SRVマネージャー</param>
    /// <param name="offscreen">オフスクリーン</param>
    /// <param name="sceneManager">シーンマネージャー</param>
    /// <param name="collision">衝突マネージャー</param>
    void Initialize(DirectXCommon *dxCommon, SrvManager *srvManager,
                    OffScreen *offscreen, SceneManager *sceneManager,
                    CollisionManager *collision);

    /// ===================================================
    /// 描画エントリ登録 API
    /// ===================================================

    /// <summary>
    /// 描画処理を登録（後方互換 API、DrawLayer 指定）
    /// </summary>
    /// <param name="name">エントリ名</param>
    /// <param name="layer">描画レイヤー</param>
    /// <param name="drawFunc">描画処理</param>
    void Register(std::string name, DrawLayer layer,
                  std::function<void(const ViewProjection &)> drawFunc);

    /// <summary>
    /// 描画処理を登録（マルチステージ API、stageIndex 指定）
    /// </summary>
    /// <param name="name">エントリ名</param>
    /// <param name="stageIndex">ステージインデックス</param>
    /// <param name="drawFunc">描画処理</param>
    void Register(std::string name, int stageIndex,
                  std::function<void(const ViewProjection &)> drawFunc);

    /// <summary>
    /// 描画エントリの登録を解除
    /// </summary>
    /// <param name="name">解除するエントリ名</param>
    void Unregister(const std::string &name);

    /// <summary>
    /// 全描画エントリをクリア
    /// </summary>
    void Clear();

    /// ===================================================
    /// マルチステージ管理 API
    /// ===================================================

    /// <summary>
    /// 新しいレンダリングステージを作成
    /// </summary>
    /// <returns>int: 割り当てられたステージインデックス</returns>
    int CreateStage();

    /// <summary>
    /// 指定ステージの OffScreen を取得（ポストエフェクト設定用）
    /// </summary>
    /// <param name="stageIndex">ステージインデックス</param>
    /// <returns>OffScreen*: 該当ステージのオフスクリーン</returns>
    OffScreen *GetStageOffScreen(int stageIndex);

    /// ===================================================
    /// 描画 / デバッグ
    /// ===================================================

    /// <summary>
    /// 登録された全エントリを描画
    /// </summary>
    /// <param name="vp">ビュープロジェクション</param>
    void Draw(const ViewProjection &vp);

    /// <summary>
    /// ImGuiでの描画設定UIを更新
    /// </summary>
    /// <param name="open">ウィンドウの表示状態。閉じるボタン押下で false になる（nullptr で閉じるボタン非表示）</param>
    void UpdateImGui(bool *open = nullptr);

    /// <summary>
    /// 描画設定をJsonへ保存
    /// </summary>
    /// <param name="fileName">保存ファイル名</param>
    void SaveConfig(const std::string &fileName = "DrawSystem");

    /// <summary>
    /// 描画設定をJsonから読み込み
    /// </summary>
    /// <param name="fileName">読み込みファイル名</param>
    void LoadConfig(const std::string &fileName = "DrawSystem");

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    DrawSystem() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    ~DrawSystem() = default;
    DrawSystem(const DrawSystem &) = delete;
    DrawSystem &operator=(const DrawSystem &) = delete;

    /// <summary>
    /// 描画処理登録の実体
    /// </summary>
    /// <param name="name">エントリ名</param>
    /// <param name="stageIndex">ステージインデックス</param>
    /// <param name="drawFunc">描画処理</param>
    void RegisterImpl(std::string name, int stageIndex,
                      std::function<void(const ViewProjection &)> drawFunc);

    /// ===================================================
    /// private variants
    /// ===================================================

    std::vector<DrawEntry> entries_; // 登録された描画エントリ

    DirectXCommon *dxCommon_ = nullptr;     // DirectX共通処理
    SrvManager *srvManager_ = nullptr;      // SRVマネージャー
    SceneManager *sceneManager_ = nullptr;  // シーンマネージャー
    CollisionManager *collision_ = nullptr; // 衝突マネージャー

    // stageIndex → OffScreen* (所有しないステージ0 + 所有するステージ1以降)
    std::map<int, OffScreen *> stageOffScreens_;
    std::vector<std::unique_ptr<OffScreen>> ownedOffScreens_; // 所有するオフスクリーン
    int nextStageIndex_ = 1;                                  // 次に割り当てるステージインデックス
};
} // namespace Hagine
