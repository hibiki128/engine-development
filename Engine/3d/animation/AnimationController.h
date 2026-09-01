#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {
class Object3d;
class Animator;

/// <summary>
/// アニメーションクリップ定義
/// 1つのアニメーションファイルに対する再生パラメータをまとめて保持する
/// </summary>
struct AnimationClip
{
    std::string name;           // クリップの識別名（ステート名など）
    std::string filePath;       // アニメーションファイルパス（"animation/Player/Idle_Ground.gltf" 等）
    bool loop = true;           // ループ再生するか
    float speed = 1.0f;         // 再生速度倍率
    float blendDuration = 0.3f; // このクリップへ切り替える際の補間時間（秒）
};

/// <summary>
/// アニメーション制御クラス
/// Object3d が持つアニメーション機能をラップし、名前付きクリップの登録・再生・
/// 再生制御（一時停止・スクラブ・速度変更）・キーフレーム編集・ImGui調整を一括で扱う
/// </summary>
class AnimationController
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="pObject">制御対象の Object3d（gltfモデルであること）</param>
    void Initialize(Object3d *pObject);

    /// <summary>
    /// クリップを登録する（モデルへの追加とループ設定も行う）
    /// </summary>
    /// <param name="name">識別名</param>
    /// <param name="filePath">アニメーションファイルパス</param>
    /// <param name="loop">ループ再生するか</param>
    /// <param name="speed">再生速度倍率</param>
    /// <param name="blendDuration">切り替え時の補間時間（秒）</param>
    void RegisterClip(const std::string &name, const std::string &filePath,
                      bool loop = true, float speed = 1.0f, float blendDuration = 0.3f);

    /// <summary>
    /// 登録済みクリップを名前で再生（補間あり）
    /// 同一クリップ再生時は何もしない
    /// </summary>
    /// <param name="name">クリップ名</param>
    void Play(const std::string &name);

    /// <summary>
    /// 登録済みクリップを名前で即時再生（補間なし）
    /// </summary>
    /// <param name="name">クリップ名</param>
    void PlayImmediate(const std::string &name);

    /// <summary>
    /// ファイルパスを直接指定して再生（未登録なら自動登録）
    /// コンボなど動的に差し替えるアニメーション向け
    /// </summary>
    /// <param name="filePath">アニメーションファイルパス</param>
    /// <param name="loop">ループ再生するか</param>
    /// <param name="speed">再生速度倍率</param>
    /// <param name="blendDuration">補間時間（秒）</param>
    void PlayFile(const std::string &filePath, bool loop = false,
                  float speed = 1.0f, float blendDuration = 0.15f);

    /// <summary>
    /// クリップが登録済みかを取得
    /// </summary>
    /// <param name="name">クリップ名</param>
    /// <returns>bool: 登録済みなら true</returns>
    bool HasClip(const std::string &name) const;

    /// <summary>
    /// 登録済みクリップを部分レイヤーとして再生する
    /// SetLayerMaskRoot() で指定したジョイント以下だけがこのクリップで上書きされ、
    /// それ以外は通常再生中のクリップのまま進む（走りながら撃つ等）。
    /// 同じクリップの再指定は先頭から再生し直す
    /// </summary>
    /// <param name="name">クリップ名</param>
    /// <param name="fadeDuration">レイヤーの出入りにかける時間（秒）</param>
    void PlayLayer(const std::string &name, float fadeDuration = 0.1f);

    /// <summary>
    /// レイヤー再生を解除する
    /// </summary>
    /// <param name="fadeDuration">フェードアウトにかける時間（秒）</param>
    void StopLayer(float fadeDuration = 0.1f);

    /// <summary>
    /// レイヤー再生中かを取得
    /// </summary>
    /// <returns>bool: 再生中なら true</returns>
    bool IsLayerPlaying() const;

    /// <summary>
    /// ImGuiによる調整・編集UIの描画（ウィンドウは生成せず内容のみ描画）
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// クリップ設定（速度・補間・ループ）をJSONへ保存
    /// </summary>
    /// <param name="folder">フォルダ名</param>
    /// <param name="file">ファイル名</param>
    void SaveClips(const std::string &folder, const std::string &file);

    /// <summary>
    /// クリップ設定（速度・補間・ループ）をJSONから読み込み
    /// 既存登録クリップのパラメータを上書きする
    /// </summary>
    /// <param name="folder">フォルダ名</param>
    /// <param name="file">ファイル名</param>
    void LoadClips(const std::string &folder, const std::string &file);

    /// ===================================================
    /// Getter
    /// ===================================================
    const std::string &GetCurrentClipName() const { return currentClipName_; }

    /// <summary>
    /// レイヤー再生中のクリップ名（PlayLayerFile の場合はファイルパス）を取得する。
    /// 自動解除された場合も名前は残るため、IsLayerPlaying() と併せて判定すること
    /// </summary>
    /// <returns>const std::string&: レイヤーのクリップ名</returns>
    const std::string &GetLayerClipName() const { return layerClipName_; }
    bool IsFinished() const;
    bool IsBlending() const;
    bool IsPaused() const { return paused_; }
    float GetAnimationTime() const;
    float GetDuration() const;
    float GetGlobalSpeed() const { return globalSpeed_; }

    /// ===================================================
    /// Setter
    /// ===================================================

    /// <summary>
    /// 一時停止／再開を設定する
    /// </summary>
    /// <param name="paused">true で一時停止</param>
    void SetPaused(bool paused);

    /// <summary>
    /// 再生時間を直接指定する（スクラブ）
    /// </summary>
    /// <param name="time">再生時間（秒）</param>
    void SetTime(float time);

    /// <summary>
    /// 全クリップ共通の速度倍率を設定する
    /// </summary>
    /// <param name="speed">速度倍率</param>
    void SetGlobalSpeed(float speed);

    /// <summary>
    /// レイヤー再生で上書きする範囲の根ジョイントを設定する
    /// （例：上半身だけ差し替えるなら背骨の根ジョイント名）。
    /// 未設定のあいだ PlayLayer() は何もしない
    /// </summary>
    /// <param name="jointName">ジョイント名</param>
    void SetLayerMaskRoot(const std::string &jointName) { layerMaskRoot_ = jointName; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 現在のアニメーターを取得する（未生成なら nullptr）
    /// </summary>
    /// <returns>Animator*: アニメーター</returns>
    Animator *GetAnimator() const;

    /// <summary>
    /// クリップの再生パラメータを Object3d へ適用する
    /// </summary>
    /// <param name="clip">適用するクリップ</param>
    void ApplyClipParams(const AnimationClip &clip);

    /// <summary>
    /// ファイルパスから登録済みクリップのインデックスを検索する
    /// </summary>
    /// <param name="filePath">アニメーションファイルパス</param>
    /// <returns>int: 見つかればインデックス、なければ -1</returns>
    int FindClipIndexByFilePath(const std::string &filePath) const;

    /// <summary>
    /// ImGuiの再生トランスポート部を描画する
    /// </summary>
    void DrawTransportImGui();

    /// <summary>
    /// ImGuiのクリップ一覧部を描画する
    /// </summary>
    void DrawClipListImGui();

    /// <summary>
    /// ImGuiのキーフレーム編集部を描画する
    /// </summary>
    void DrawKeyframeImGui();

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    Object3d *pObject_ = nullptr;                 // 制御対象
    std::vector<AnimationClip> clips_;           // 登録クリップ（順序保持）
    std::unordered_map<std::string, int> index_; // クリップ名 -> clips_ のインデックス

    std::string currentClipName_;   // 現在再生中のクリップ名

    // クリップ設定の保存先。SaveClips / LoadClips を呼んだときに覚え、
    // エディタの保存・読込ボタンから同じ場所を使う。
    // （エンジンがゲーム固有のファイル名を決め打ちしないため。既定値は無害な汎用名）
    std::string clipsFolder_ = "AnimationController";
    std::string clipsFile_ = "Clips";
    std::string layerClipName_;     // レイヤー再生中のクリップ名
    std::string layerMaskRoot_;     // レイヤーで上書きする範囲の根ジョイント名（空ならレイヤー無効）
    float currentClipSpeed_ = 1.0f; // 現在クリップ固有の速度（globalSpeed_ と乗算）
    float globalSpeed_ = 1.0f;      // 全体速度倍率
    bool paused_ = false;           // 一時停止中フラグ

    // ImGui編集用の状態
    int selectedNodeIndex_ = 0; // キーフレーム編集で選択中のノード
    int selectedChannel_ = 0;   // 0:Translate 1:Rotate 2:Scale
};
} // namespace Hagine
