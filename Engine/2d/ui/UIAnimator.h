#pragma once
#include "Easing.h"
#include <nlohmann/json.hpp>
#include <string>
#include <type/Vector2.h>
#include <vector>

namespace Hagine {

/// <summary>
/// トゥイーンの操作対象の種別
/// </summary>
enum class UITargetKind
{
    Sprite, // 単体スプライト
    Group,  // スプライトグループ（相対位置を保つ束）
};

/// <summary>
/// トゥイーンで動かすプロパティ（1チャンネル＝スカラー値）
/// </summary>
enum class UIChannel
{
    PositionX, // 位置X（ピクセル）
    PositionY, // 位置Y（ピクセル）
    ScaleX,    // スケールX
    ScaleY,    // スケールY
    RotationZ, // 回転Z（ラジアン）
    Alpha,     // 不透明度（0〜1）
    Count,     // 種類数（配列サイズ用・実チャンネルではない）
};

/// <summary>
/// 1本のトゥイーン。対象の1プロパティを 初期値→目標値 へイージング補間する。
/// </summary>
struct UITween
{
    UITargetKind targetKind = UITargetKind::Sprite; // 対象種別
    std::string targetName;                         // 対象スプライト名／グループ名
    UIChannel channel = UIChannel::PositionX;       // 動かすプロパティ
    bool fromCurrent = false;                       // 再生時点の現在値を初期値にする
    float startValue = 0.0f;                        // 初期値（fromCurrent=falseのとき使用）
    float endValue = 0.0f;                          // 目標値
    float duration = 0.5f;                          // 補間にかける秒数
    float delay = 0.0f;                             // 再生開始からの遅延秒数
    EasingType easing = EasingType::Linear;         // イージング種別

    // ---- 実行時状態（保存しない） ----
    float elapsed_ = 0.0f;       // 遅延込みの経過時間
    float resolvedStart_ = 0.0f; // 実際に使う初期値（fromCurrent解決後）
    bool finished_ = false;      // 補間完了フラグ
};

/// <summary>
/// 名前付きクリップ。複数トゥイーンの束で、コードから名前で再生する単位。
/// </summary>
struct UIClip
{
    std::string name;             // クリップ名（Playの引数）
    std::vector<UITween> tweens;  // 含まれるトゥイーン
    bool loop = false;            // ループ再生するか

    // ---- 実行時状態 ----
    bool playing_ = false; // 再生中フラグ
};

/// <summary>
/// グループのメンバー（スプライト名と原点からの相対オフセット）
/// </summary>
struct UIGroupMember
{
    std::string spriteName;         // メンバースプライト名
    Vector2 offset = {0.0f, 0.0f};  // グループ原点からの相対位置（ピクセル）

    // ---- 実行時状態（保存しない） ----
    // 前のフレームにグループが書き込んだ位置。今の位置がこれと違えば
    // 「ギズモなどで外から動かされた」と分かるので、相対位置を取り直す
    Vector2 lastApplied_ = {0.0f, 0.0f};
    bool hasApplied_ = false;
};

/// <summary>
/// 相対位置を保つスプライトの束。原点を動かすと全メンバーが相対を保って追従する。
/// </summary>
struct UIGroup
{
    std::string name;                    // グループ名
    Vector2 origin = {0.0f, 0.0f};       // 原点（基準トランスフォーム）
    std::vector<UIGroupMember> members;  // メンバー
};

/// <summary>
/// UIスプライトのグループ管理と、名前付きトゥイーン（イージング）の再生を担うシングルトン。
/// エディタで作成した「クリップ」を UIAnimator::Play("名前") でコード側から再生できる。
/// 毎フレーム Update() を呼ぶことで再生中クリップの補間とグループ相対位置の反映を行う。
/// </summary>
class UIAnimator
{
  public:
    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    static UIAnimator *GetInstance();

    /// <summary>
    /// 保存済みのグループ・クリップを読み込む（未ロードなら初回に自動で呼ばれる）
    /// </summary>
    void Initialize();

    /// <summary>
    /// 毎フレーム呼ぶ。再生中クリップの補間更新と、全グループの相対位置反映を行う。
    /// </summary>
    /// <param name="deltaTime">前フレームからの経過秒数</param>
    void Update(float deltaTime);

    /// ===================================================
    /// コードから使う再生API
    /// ===================================================

    /// <summary>
    /// 名前でクリップを先頭から再生する
    /// </summary>
    void Play(const std::string &clipName);

    /// <summary>
    /// 指定クリップの再生を止める
    /// </summary>
    void Stop(const std::string &clipName);

    /// <summary>
    /// すべてのクリップの再生を止める
    /// </summary>
    void StopAll();

    /// <summary>
    /// 指定クリップが再生中か
    /// </summary>
    bool IsPlaying(const std::string &clipName) const;

    /// ===================================================
    /// 保存・読み込み
    /// ===================================================
    void Save();
    void Load();

#ifdef USE_IMGUI
    /// <summary>
    /// UIエディタのImGuiを描画する
    /// </summary>
    /// <param name="open">ウィンドウの表示フラグ（×ボタンと同期）</param>
    void DrawImGui(bool *open);
#endif // USE_IMGUI

  private:
    UIAnimator() = default;
    ~UIAnimator() = default;
    UIAnimator(const UIAnimator &) = delete;
    UIAnimator &operator=(const UIAnimator &) = delete;

    UIClip *FindClip(const std::string &name);
    UIGroup *FindGroup(const std::string &name);

    /// 未ロードなら一度だけ Load() する
    void EnsureLoaded();
    /// クリップの再生状態を先頭にリセットし、fromCurrentの初期値を解決する
    void RewindClip(UIClip &clip);
    /// 1本のトゥイーンを進めて対象へ反映する
    void UpdateTween(UITween &tween, float deltaTime);
    /// チャンネルの現在値を取得（fromCurrent用）
    float GetChannelValue(UITargetKind kind, const std::string &target, UIChannel ch);
    /// チャンネルへ値を適用する
    void ApplyChannel(UITargetKind kind, const std::string &target, UIChannel ch, float value);
    /// 全グループの相対位置をメンバースプライトへ反映する
    void ApplyGroups();

    /// グループとクリップを保存形式の JSON にまとめる（{ "groups": [...], "clips": [...] }）
    nlohmann::json ToJson() const;

  private:
    std::vector<UIGroup> groups_; // 登録済みグループ
    std::vector<UIClip> clips_;   // 登録済みクリップ
    bool loaded_ = false;         // Load済みか

#ifdef USE_IMGUI
    int selectedGroup_ = -1;      // エディタで選択中のグループindex
    int selectedClip_ = -1;       // エディタで選択中のクリップindex
    std::string newGroupName_;    // 新しく作るグループの名前
    std::string newClipName_;     // 新しく作るクリップの名前
    std::string addMemberTarget_; // メンバーに足すスプライト
    nlohmann::json savedState_;   // 最後に保存・読み込みした内容（未保存の変更があるかを見る）
    int deleteGroupRequest_ = -1; // 削除の確認を出しているグループ
    int deleteClipRequest_ = -1;  // 削除の確認を出しているクリップ

    /// 「グループ」タブ
    void DrawGroupTab();
    /// 「クリップ」タブ
    void DrawClipTab();
    /// 選んだグループの中身（原点・メンバー）
    void DrawGroupDetail(UIGroup &group);
    /// 1本のトゥイーンの設定欄
    void DrawTweenEditor(UITween &tween);
    /// 新しいトゥイーンの対象の候補（シーンで選んでいるスプライト → 直前のトゥイーンの対象）
    std::string SuggestTweenTarget(const UIClip &clip) const;

    // ---- タイムライン・プレビュー（UIAnimatorTimeline.cpp）----
    /// <summary>プレビューする前の値（「プレビュー前に戻す」で書き戻す）</summary>
    struct PreviewValue
    {
        UITargetKind kind;
        std::string target;
        UIChannel channel;
        float value;
    };
    int selectedTween_ = -1;                   // タイムラインで選んだトゥイーン
    float scrubTime_ = 0.0f;                   // 目盛りで指している時刻
    bool scrubbing_ = false;                   // 目盛りをつまんでいる最中か
    std::vector<PreviewValue> previewSnapshot_; // プレビュー前の値
    std::string previewClip_;                   // どのクリップのプレビューか

    /// クリップの長さ（遅延+秒数のいちばん長い物）
    float ClipLength(const UIClip &clip) const;
    /// 再生中のクリップの今の時刻
    float ClipTime(const UIClip &clip) const;
    /// クリップが動かす値を覚えておく
    void CapturePreviewSnapshot(const UIClip &clip);
    /// 覚えておいた値へ戻す
    void RestorePreviewSnapshot();
    /// 指定の時刻の姿を当てる（途中の確認用）
    void PreviewClipAt(UIClip &clip, float time);
    /// タイムラインを描く。行を押してトゥイーンを選んだら true
    bool DrawClipTimeline(UIClip &clip);
    /// グループとメンバーを階層で描く
    void DrawGroupTree();
#endif // USE_IMGUI
};

} // namespace Hagine
