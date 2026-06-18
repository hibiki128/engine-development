#pragma once
#include "Model/ModelStructs.h"
#include <map>
#include <string>
#include <type/Quaternion.h>
#include <type/Vector3.h>
#include <unordered_map>
#include <vector>

/// <summary>
/// アニメーション補間の状態を管理する構造体
/// </summary>
namespace Hagine {
struct AnimationBlendState {
    Animation fromAnimation;        // 補間元のアニメーション
    Animation toAnimation;          // 補間先のアニメーション
    float blendFactor = 0.0f;       // 補間係数 (0.0 ~ 1.0)
    float blendDuration = 0.5f;     // 補間にかける時間
    float blendTimer = 0.0f;        // 補間の経過時間
    bool isBlending = false;        // 補間中かどうか
    float fromAnimationTime = 0.0f; // 補間元のアニメーション時間
    float toAnimationTime = 0.0f;   // 補間先のアニメーション時間
    std::string toDirectoryPath;    // 補間先のディレクトリパス
    std::string toFilename;         // 補間先のファイル名
};

/// <summary>
/// アニメーション管理クラス
/// アニメーションの再生、切り替え、補間処理を行う
/// </summary>
class Animator {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="directorypath">ディレクトリパス</param>
    /// <param name="filename">アニメーションファイル名</param>
    void Initialize(const std::string &directorypath, const std::string &filename);

    /// <summary>
    /// アニメーション更新（補間対応）
    /// </summary>
    /// <param name="loop">ループ再生フラグ</param>
    void Update(bool loop);

    /// <summary>
    /// アニメーション切り替え（補間付き）
    /// </summary>
    /// <param name="newAnimation">切り替え先のアニメーション</param>
    /// <param name="blendDuration">補間時間（秒）</param>
    void BlendToAnimation(const Animation &newAnimation, float blendDuration = 0.5f);

    /// <summary>
    /// アニメーション切り替え（ファイルパス指定）
    /// </summary>
    /// <param name="directoryPath">ディレクトリパス</param>
    /// <param name="filename">アニメーションファイル名</param>
    /// <param name="blendDuration">補間時間（秒）</param>
    void BlendToAnimation(const std::string &directoryPath, const std::string &filename, float blendDuration = 0.5f);

    /// <summary>
    /// 補間中かどうかを取得
    /// </summary>
    /// <returns>bool: 補間中であればtrue</returns>
    bool IsBlending() const { return blendState_.isBlending; }

    /// <summary>
    /// 現在のアニメーションデータを取得（補間済み）
    /// </summary>
    /// <returns>Animation: 補間済みアニメーションデータ</returns>
    Animation GetCurrentAnimation() const;

    /// <summary>
    /// 現在のファイル情報を更新
    /// </summary>
    /// <param name="directoryPath">ディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    void UpdateCurrentFileInfo(const std::string &directoryPath, const std::string &filename);

    /// <summary>
    /// 補間されたノードアニメーションを取得
    /// </summary>
    /// <returns>std::map<std::string, NodeAnimation>: ノードアニメーションマップ</returns>
    std::map<std::string, NodeAnimation> GetBlendedNodeAnimations() const;

    /// <summary>
    /// 値の計算(Vector3)
    /// </summary>
    /// <param name="keyframes">キーフレーム配列</param>
    /// <param name="time">時間</param>
    /// <returns>Vector3: 計算された値</returns>
    static Vector3 CalculateValue(const std::vector<KeyframeVector3> &keyframes, float time);

    /// <summary>
    /// 値の計算(Quaternion)
    /// </summary>
    /// <param name="keyframes">キーフレーム配列</param>
    /// <param name="time">時間</param>
    /// <returns>Quaternion: 計算された値</returns>
    static Quaternion CalculateValue(const std::vector<KeyframeQuaternion> &keyframes, float time);

    /// <summary>
    /// Getter
    /// </summary>
    Animation GetAnimation() const { return currentAnimation_; }

    /// <summary>
    /// 現在のアニメーションへの可変参照を取得（キーフレーム編集用）
    /// 補間中は表示用ポーズが別途生成されるため、編集結果は補間終了後に反映される
    /// </summary>
    /// <returns>Animation&: 現在のアニメーションデータへの参照</returns>
    Animation &GetMutableAnimation() { return currentAnimation_; }
    bool IsFinish() const { return isFinish_; }
    bool IsFinished() const { return isFinish_; }
    bool IsPlaying() const { return isAnimation_; }
    float GetAnimationTime() const { return animationTime_; }
    std::string GetCurrentFilename() const { return filename_; }
    std::string GetCurrentDirectoryPath() const { return directorypath_; }
    Matrix4x4 GetLocalMatrix() { return localMatrix_; }
    float GetSpeed() const { return speed_; }
    float GetBlendDuration() const { return blendDuration_; }

    /// ===================================================
    /// Setter
    /// ===================================================
    void SetIsAnimation(bool isAnimation) { isAnimation_ = isAnimation; }
    void SetAnimationTime(float time) { animationTime_ = time; }
    void SetModelData(ModelData modelData) { modelData_ = modelData; }
    void SetSpeed(float speed) { speed_ = speed; }
    void SetBlendDuration(float duration) { blendDuration_ = duration; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// アニメーションファイル読み込み
    /// </summary>
    /// <param name="directoryPath">ディレクトリパス</param>
    /// <param name="filename">ファイル名</param>
    /// <returns>Animation: 読み込んだアニメーションデータ</returns>
    Animation LoadAnimationFile(const std::string &directoryPath, const std::string &filename);

    /// <summary>
    /// 補間更新処理
    /// </summary>
    /// <param name="loop">ループ再生フラグ</param>
    void UpdateBlend(bool loop);

    /// <summary>
    /// 通常のアニメーション更新処理
    /// </summary>
    /// <param name="loop">ループ再生フラグ</param>
    void UpdateSingle(bool loop);

    /// <summary>
    /// 補間された値を計算（Vector3用）
    /// </summary>
    /// <param name="fromKeyframes">補間元のキーフレーム</param>
    /// <param name="toKeyframes">補間先のキーフレーム</param>
    /// <param name="fromTime">補間元の時間</param>
    /// <param name="toTime">補間先の時間</param>
    /// <param name="blendFactor">補間係数</param>
    /// <returns>Vector3: 補間された値</returns>
    Vector3 CalculateBlendedValue(
        const std::vector<KeyframeVector3> &fromKeyframes,
        const std::vector<KeyframeVector3> &toKeyframes,
        float fromTime, float toTime, float blendFactor) const;

    /// <summary>
    /// 補間された値を計算（Quaternion用）
    /// </summary>
    /// <param name="fromKeyframes">補間元のキーフレーム</param>
    /// <param name="toKeyframes">補間先のキーフレーム</param>
    /// <param name="fromTime">補間元の時間</param>
    /// <param name="toTime">補間先の時間</param>
    /// <param name="blendFactor">補間係数</param>
    /// <returns>Quaternion: 補間された値</returns>
    Quaternion CalculateBlendedValue(
        const std::vector<KeyframeQuaternion> &fromKeyframes,
        const std::vector<KeyframeQuaternion> &toKeyframes,
        float fromTime, float toTime, float blendFactor) const;

  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    std::string filename_;           // 現在のファイル名
    std::string directorypath_;      // 現在のディレクトリパス
    float animationTime_ = 0.0f;      // アニメーション再生時間
    Animation currentAnimation_;     // 現在のアニメーション
    AnimationBlendState blendState_; // 補間状態
    Matrix4x4 localMatrix_;           // ローカル行列
    bool isAnimation_ = true;        // アニメーション再生フラグ
    bool isFinish_ = false;          // アニメーション終了フラグ
    ModelData modelData_;            // モデルデータ
    float speed_ = 1.0f;             // アニメーション速度
    float blendDuration_ = 0.5f;     // アニメーション補間時間

    static std::unordered_map<std::string, Animation> animationCache_; // アニメーションキャッシュ
};
} // namespace Hagine
