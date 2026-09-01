#pragma once
#include "Easing.h"
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

  private:
    std::vector<UIGroup> groups_; // 登録済みグループ
    std::vector<UIClip> clips_;   // 登録済みクリップ
    bool loaded_ = false;         // Load済みか

#ifdef USE_IMGUI
    int selectedGroup_ = -1;      // エディタで選択中のグループindex
    int selectedClip_ = -1;       // エディタで選択中のクリップindex
    char newNameBuffer_[128] = {}; // 新規作成・リネーム用の入力バッファ
#endif // USE_IMGUI
};

} // namespace Hagine
