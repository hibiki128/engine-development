#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Hagine {

/// <summary>
/// 描画グループの登録と表示／非表示を一元管理するシングルトン（ヘッダオンリー）
///
/// オブジェクト・スプライト・CPU/GPU パーティクルエミッターはそれぞれ別マネージャーが
/// 保持するが、「どのグループが存在し、表示中か」という横断的な情報はここに集約する。
/// 各マネージャーは描画時に IsGroupVisible() を参照し、非表示グループをスキップする。
/// 所属（どの要素がどのグループか）は各要素側が文字列で保持する。
/// </summary>
class DrawGroupManager {
  public:
    /// <summary>
    /// 描画グループ＝描画ステージ（パス）。この2つのみを正準値として扱う。
    ///   "3D" : ポストエフェクト前の3Dステージ（stageIndex 0）で描画される
    ///   "UI" : UIレイヤー（ポストエフェクト後・最前面で合成）で描画される
    /// </summary>
    static constexpr const char *kLayer3D = "3D";
    static constexpr const char *kLayerUI = "UI";

    // DrawSystem のステージ番号に対応（kStageUI は DrawSystem::kUILayer と同値）
    static constexpr int kStage3D = 0;
    static constexpr int kStageUI = -1;

    /// <summary>
    /// グループ名を描画ステージ番号へ変換する（"UI" 以外はすべて 3D 扱い）
    /// </summary>
    static int StageOf(const std::string &group) {
        return (group == kLayerUI) ? kStageUI : kStage3D;
    }

    /// <summary>既定グループ名（3Dステージ）</summary>
    static constexpr const char *kDefaultGroup = kLayer3D;

    /// <summary>インスタンスを取得</summary>
    static DrawGroupManager *GetInstance() {
        static DrawGroupManager instance;
        return &instance;
    }

    /// <summary>
    /// グループを登録する（未登録なら表示状態で追加。既存なら何もしない）
    /// </summary>
    /// <param name="group">グループ名（空文字は無視）</param>
    void RegisterGroup(const std::string &group) {
        if (group.empty()) {
            return;
        }
        groupVisible_.try_emplace(group, true);
    }

    /// <summary>
    /// グループが表示状態かを取得する（未登録グループは表示扱い）
    /// </summary>
    /// <param name="group">グループ名</param>
    /// <returns>表示中なら true</returns>
    bool IsGroupVisible(const std::string &group) const {
        auto it = groupVisible_.find(group);
        return (it == groupVisible_.end()) ? true : it->second;
    }

    /// <summary>グループの表示／非表示を設定する</summary>
    /// <param name="group">グループ名</param>
    /// <param name="visible">表示するか</param>
    void SetGroupVisible(const std::string &group, bool visible) {
        if (group.empty()) {
            return;
        }
        groupVisible_[group] = visible;
    }

    /// <summary>
    /// 既知のグループ名一覧を取得する（昇順。kDefaultGroup を必ず含む）
    /// </summary>
    /// <returns>グループ名一覧</returns>
    std::vector<std::string> GetGroups() const {
        std::set<std::string> set;
        set.insert(kLayer3D); // 2つの正準レイヤーは常に存在させる
        set.insert(kLayerUI);
        for (const auto &[group, _] : groupVisible_) {
            set.insert(group);
        }
        return std::vector<std::string>(set.begin(), set.end());
    }

  private:
    DrawGroupManager() = default;
    ~DrawGroupManager() = default;
    DrawGroupManager(const DrawGroupManager &) = delete;
    DrawGroupManager &operator=(const DrawGroupManager &) = delete;

    // グループ名 → 表示フラグ（セッション内の表示切替に使用）
    std::map<std::string, bool> groupVisible_;
};
} // namespace Hagine
