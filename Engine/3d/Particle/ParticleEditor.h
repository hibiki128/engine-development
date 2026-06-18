#pragma once

#include "Camera/ViewProjection/ViewProjection.h"
#include "ParticleEmitter.h"
#include "ParticleGroup.h"
#include "ParticleGroupManager.h"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ParticleStruct.h"

namespace Hagine {

/// <summary>
/// パーティクルのエミッター・グループをImGuiで編集するエディタ（シングルトン）
/// エミッターの追加・描画・統計表示・テンプレート生成・保存/読み込みを行う
/// </summary>
class ParticleEditor {
  private:
    /// ===================================================
    /// private method / variants
    /// ===================================================

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ParticleEditor() = default;
    ParticleEditor(const ParticleEditor &) = delete;
    ParticleEditor &operator=(const ParticleEditor &) = delete;

    // パーティクルエミッター保持用マップ
    std::unordered_map<std::string, std::unique_ptr<ParticleEmitter>> emitters_;
    int selectedEmitterIndex_ = 0;    // 選択されたエミッターのインデックス
    std::string selectedEmitterName_; // 選択されたエミッターの名前

    // パーティクルグループマネージャーポインタ
    ParticleGroupManager *particleGroupManager_ = nullptr;

    std::unordered_map<std::string, ParticleStats> currentFrameStats_; // 現在フレームの統計
    std::unordered_map<std::string, ParticleStats> displayStats_;      // 表示用の統計（前フレーム確定分）
    uint64_t currentFrameNumber_ = 0;
    uint64_t lastUpdateFrame_ = 0;

    // ローカル変数（UIで使用）
    std::string localName_;                         // パーティクルグループ名
    std::string localFileObj_;                      // .objファイルパス
    std::string localTexturePath_;                  // テクスチャパス
    std::string localEmitterName_;                  // エミッター名
    PrimitiveType localType_ = PrimitiveType::None; // プリミティブタイプ

#ifdef USE_IMGUI
    // CollapsingHeaderの色を定義
    ImVec4 headerColors_[6];
#endif // USE_IMGUI

    // ロード関連変数
    bool isLoad_ = false;
    bool statsCleared_ = false;
    bool statsDisplayedThisFrame_ = false;
    std::string name_;
    std::string fileName_;
    std::string texturePath_;

    /// <summary>カラーテーマを設定</summary>
    void SetupColors();

    /// <summary>カラー付きCollapsingHeaderを表示</summary>
    /// <param name="label">ヘッダーのラベル</param>
    /// <param name="colorIndex">使用する色のインデックス</param>
    /// <returns>bool: 展開されていれば true</returns>
    bool ColoredCollapsingHeader(const char *label, int colorIndex);

    /// <summary>ファイルセレクタを表示</summary>
    void ShowFileSelector();

    /// <summary>JSONファイル一覧を取得</summary>
    /// <returns>std::vector&lt;std::string&gt;: JSONファイル名一覧</returns>
    std::vector<std::string> GetJsonFiles();

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>インスタンスを取得</summary>
    /// <returns>ParticleEditor*: シングルトンインスタンス</returns>
    static ParticleEditor *GetInstance() {
        static ParticleEditor instance;
        return &instance;
    }

    /// <summary>終了処理</summary>
    void Finalize();

    /// <summary>初期化</summary>
    void Initialize();

    /// <summary>パーティクルエミッターを追加（名前指定）</summary>
    /// <param name="name">エミッター名</param>
    void AddParticleEmitter(const std::string &name);

    /// <summary>パーティクルエミッターを追加（名前・ファイル・テクスチャ指定）</summary>
    /// <param name="name">エミッター名</param>
    /// <param name="fileName">ファイル名</param>
    /// <param name="texturePath">テクスチャパス</param>
    void AddParticleEmitter(const std::string &name, const std::string &fileName, const std::string &texturePath);

    /// <summary>パーティクルグループを追加（OBJモデル使用）</summary>
    /// <param name="name">グループ名</param>
    /// <param name="fileName">モデルファイル名</param>
    /// <param name="texturePath">テクスチャパス</param>
    void AddParticleGroup(const std::string &name, const std::string &fileName, const std::string &texturePath);

    /// <summary>パーティクルグループを追加（プリミティブ使用）</summary>
    /// <param name="name">グループ名</param>
    /// <param name="texturePath">テクスチャパス</param>
    /// <param name="type">プリミティブの種類</param>
    void AddPrimitiveParticleGroup(const std::string &name, const std::string &texturePath, PrimitiveType type);

    /// <summary>外部パーティクル数をセット（シーン側から呼び出し）</summary>
    /// <param name="name">エミッター名</param>
    /// <param name="count">パーティクル数</param>
    void SetExternalParticleCount(const std::string &name, size_t count);

    /// <summary>シーン全体のパーティクル数を集計</summary>
    void SceneParticleCount();

    /// <summary>フレームごとの統計を更新</summary>
    void UpdateFrameStats();

    /// <summary>テンプレートからエミッターを生成</summary>
    /// <param name="name">テンプレート名</param>
    /// <returns>std::unique_ptr&lt;ParticleEmitter&gt;: 生成されたエミッター</returns>
    std::unique_ptr<ParticleEmitter> CreateEmitterFromTemplate(const std::string &name);

    /// <summary>ImGuiエディターウィンドウを表示</summary>
    void EditorWindow();

    /// <summary>すべてのエミッターを描画</summary>
    /// <param name="vp_">ビュープロジェクション</param>
    void DrawAll(const ViewProjection &vp_);

    /// <summary>
    /// プレビュー窓用: 選択中エミッターを指定VPで更新＆描画する。
    /// エミッター枠(ワイヤー)は描かず、共有 DrawLine3D も汚さない。
    /// CPUエディタのエミッターはシーンには描かれないため、ここでのみ確認できる。
    /// </summary>
    /// <param name="vp">プレビューカメラのビュープロジェクション</param>
    void DrawSelectedForPreview(const ViewProjection &vp);

    /// <summary>すべてのエミッターのデバッグ情報を表示</summary>
    void DebugAll();

    /// <summary>ImGuiエディターの表示処理</summary>
    void ShowImGuiEditor();

    /// <summary>データの読み込み</summary>
    void Load();

    /// <summary>登録済みエミッター名の一覧を取得（描画グループ管理UIで使用）</summary>
    /// <returns>エミッター名一覧</returns>
    std::vector<std::string> GetEmitterNames() const;

    /// <summary>名前からエミッターを取得（なければ nullptr）</summary>
    /// <param name="name">エミッター名</param>
    /// <returns>ParticleEmitter*</returns>
    ParticleEmitter *GetEmitterByName(const std::string &name);
};
} // namespace Hagine
