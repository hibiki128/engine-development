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
struct AnimationClip {
    std::string name;            // クリップの識別名（ステート名など）
    std::string filePath;        // アニメーションファイルパス（"animation/Player/Idle_Ground.gltf" 等）
    bool loop = true;            // ループ再生するか
    float speed = 1.0f;          // 再生速度倍率
    float blendDuration = 0.3f;  // このクリップへ切り替える際の補間時間（秒）
};

/// <summary>
/// アニメーション制御クラス
/// Object3d が持つアニメーション機能をラップし、名前付きクリップの登録・再生・
/// 再生制御（一時停止・スクラブ・速度変更）・キーフレーム編集・ImGui調整を一括で扱う
/// </summary>
class AnimationController {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="object">制御対象の Object3d（gltfモデルであること）</param>
    void Initialize(Object3d *object);

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
    /// private variants
    /// ===================================================

    Object3d *object_ = nullptr;                  // 制御対象
    std::vector<AnimationClip> clips_;            // 登録クリップ（順序保持）
    std::unordered_map<std::string, int> index_; // クリップ名 -> clips_ のインデックス

    std::string currentClipName_;                // 現在再生中のクリップ名
    float currentClipSpeed_ = 1.0f;              // 現在クリップ固有の速度（globalSpeed_ と乗算）
    float globalSpeed_ = 1.0f;                   // 全体速度倍率
    bool paused_ = false;                        // 一時停止中フラグ

    // ImGui編集用の状態
    int selectedNodeIndex_ = 0; // キーフレーム編集で選択中のノード
    int selectedChannel_ = 0;   // 0:Translate 1:Rotate 2:Scale
};
} // namespace Hagine
