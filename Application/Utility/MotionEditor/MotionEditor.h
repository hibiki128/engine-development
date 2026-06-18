#pragma once
#include "Easing.h"
#include <Object/Base/BaseObject.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

/// <summary>
/// モーションのイージングタイプを表す列挙型
/// </summary>
enum class MotionEasingType {
    Linear,
    EaseInSine,
    EaseOutSine,
    EaseInOutSine,
    EaseInBack,
    EaseOutBack,
    EaseInOutBack,
    EaseInQuint,
    EaseOutQuint,
    EaseInOutQuint,
    EaseInCirc,
    EaseOutCirc,
    EaseInOutCirc,
    EaseInExpo,
    EaseOutExpo,
    EaseInOutExpo,
    EaseOutCubic,
    EaseInCubic,
    EaseInOutCubic,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutQuad,
    EaseInQuart,
    EaseOutQuart,
    EaseInBounce,
    EaseOutBounce,
    EaseInOutBounce,
    EaseInElastic,
    EaseOutElastic,
    EaseInOutElastic,
};

/// <summary>
/// モーション再生状態を表す列挙型
/// </summary>
enum class MotionStatus {
    Stopped, // 停止状態
    Playing, // 再生中
    Finished // 再生終了
};

/// <summary>
/// モーションデータ構造体
/// </summary>
struct Motion {
    Hagine::BaseObject *target = nullptr;
    std::string objectName;
    float totalTime = 1.0f;
    float currentTime = 0.0f;
    MotionStatus status = MotionStatus::Stopped;

    Hagine::Vector3 startPosOffset, endPosOffset;
    Hagine::Vector3 startRotOffset, endRotOffset;
    Hagine::Vector3 startScaleOffset = {0, 0, 0}, endScaleOffset = {0, 0, 0};

    Hagine::Vector3 basePos, baseRot, baseScale;

    Hagine::Vector3 initialPos, initialRot, initialScale;
    bool hasInitialTransform = false;

    Hagine::Quaternion actualStartRot, actualEndRot;
    Hagine::Vector3 actualStartScale, actualEndScale;

    std::vector<Hagine::Vector3> controlPoints;
    bool useCatmullRom = false;

    MotionEasingType easingType = MotionEasingType::Linear;
    float colliderOnTime = 0.3f;
    float colliderOffTime = 0.6f;

    bool isTemporary = false;

    bool returnToOriginal = false;

    Hagine::Vector3 comboStartPos, comboStartRot, comboStartScale;
    bool hasComboStartTransform = false;
};

/// <summary>
/// モーション管理とエディタのシングルトンクラス
/// </summary>
class MotionEditor {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// シングルトンインスタンスを取得
    /// </summary>
    /// <returns>MotionEditor*: インスタンスのポインタ</returns>
    static MotionEditor *GetInstance() {
        static MotionEditor instance;
        return &instance;
    }

    /// <summary>
    /// 終了処理
    /// </summary>
    void Finalize();

    /// <summary>
    /// オブジェクトを登録
    /// </summary>
    /// <param name="object">登録するオブジェクト</param>
    void Register(Hagine::BaseObject *object);

    /// <summary>
    /// 更新処理
    /// </summary>
    /// <param name="deltaTime">フレームの経過時間</param>
    void Update(float deltaTime);

    /// <summary>
    /// ImGuiの描画処理
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// モーションを保存
    /// </summary>
    /// <param name="fileName">保存ファイル名</param>
    void Save(const std::string &fileName);

    /// <summary>
    /// モーションを読み込み
    /// </summary>
    /// <param name="fileName">読み込みファイル名</param>
    /// <returns>Motion: 読み込まれたモーションデータ</returns>
    Motion Load(const std::string &fileName);

    /// <summary>
    /// コントロールポイントを描画
    /// </summary>
    void DrawControlPoints();

    /// <summary>
    /// キャットマルロム曲線を描画
    /// </summary>
    void DrawCatmullRomCurve();

    /// <summary>
    /// キャットマルロム補間を計算
    /// </summary>
    /// <param name="points">コントロールポイント</param>
    /// <param name="t">補間値（0.0～1.0）</param>
    /// <returns>Vector3: 補間された座標</returns>
    Hagine::Vector3 CatmullRomInterpolation(const std::vector<Hagine::Vector3> &points, float t);

    /// <summary>
    /// モーションを再生
    /// </summary>
    /// <param name="jsonName">JSONファイル名</param>
    void Play(const std::string &jsonName);

    /// <summary>
    /// 指定オブジェクトのモーションを停止
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    void Stop(const std::string &objectName);

    /// <summary>
    /// すべてのモーションを停止
    /// </summary>
    void StopAll();

    /// <summary>
    /// 元の位置に戻すフラグ付きでモーションをファイルから再生
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    /// <param name="fileName">ファイル名</param>
    /// <param name="returnToOriginal">元の位置に戻すか</param>
    /// <returns>bool: 再生成功フラグ</returns>
    bool PlayFromFile(Hagine::BaseObject *target, const std::string &fileName, bool returnToOriginal = false);

    /// <summary>
    /// 初期位置をリセット
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    void ResetInitialPosition(const std::string &objectName);

    /// <summary>
    /// コンボの開始位置を設定
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    void SetComboStartPosition(Hagine::BaseObject *target);

    /// <summary>
    /// コンボ終了時に開始位置に戻す
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    void ReturnToComboStart(Hagine::BaseObject *target);

    /// <summary>
    /// 特定オブジェクトのコンボ開始位置をクリア
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    void ClearComboStartPosition(Hagine::BaseObject *target);

    /// <summary>
    /// すべてのコンボ開始位置をクリア
    /// </summary>
    void ClearAllComboStartPositions();

    /// <summary>
    /// 攻撃が終了したかを判定
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    /// <returns>bool: 攻撃終了フラグ</returns>
    bool IsAttackFinished(Hagine::BaseObject *target);

    /// <summary>
    /// インターバル付きで攻撃が終了したかを判定
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    /// <returns>bool: 攻撃終了フラグ</returns>
    bool IsAttackFinishedWithInterval(Hagine::BaseObject *target);

    /// <summary>
    /// 攻撃終了後のインターバルを設定
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    /// <param name="interval">インターバル時間</param>
    void SetAttackEndInterval(Hagine::BaseObject *target, float interval = 0.3f);

    /// <summary>
    /// 攻撃終了後のインターバルをクリア
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    void ClearAttackEndInterval(Hagine::BaseObject *target);

    /// <summary>
    /// 一時的なモーションが終了したかを判定
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    /// <param name="fileName">ファイル名</param>
    /// <returns>bool: 終了フラグ</returns>
    bool IsTemporaryMotionFinished(Hagine::BaseObject *target, const std::string &fileName);

    /// <summary>
    /// 再生状態を取得
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>MotionStatus: 再生状態</returns>
    MotionStatus GetMotionStatus(const std::string &objectName);

    /// <summary>
    /// 再生中かを判定
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>bool: 再生中か</returns>
    bool IsPlaying(const std::string &objectName);

    /// <summary>
    /// 再生終了したかを判定
    /// </summary>
    /// <param name="objectName">オブジェクト名</param>
    /// <returns>bool: 再生終了か</returns>
    bool IsFinished(const std::string &objectName);

    /// <summary>
    /// 一時的なモーション名を取得
    /// </summary>
    /// <param name="target">対象オブジェクト</param>
    /// <param name="fileName">ファイル名</param>
    /// <returns>std::string: モーション名</returns>
    std::string GetTemporaryMotionName(Hagine::BaseObject *target, const std::string &fileName);

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// ローカル座標をワールド座標に変換
    /// </summary>
    /// <param name="localOffset">ローカルオフセット</param>
    /// <param name="worldMatrix">ワールドマトリックス</param>
    /// <returns>Vector3: ワールド座標</returns>
    Hagine::Vector3 TransformLocalToWorld(const Hagine::Vector3 &localOffset, const Hagine::Matrix4x4 &worldMatrix);

    /// <summary>
    /// 終了した一時モーションをクリーンアップ
    /// </summary>
    void CleanupFinishedTemporaryMotions();

    /// <summary>
    /// 親のワールド行列の逆行列を取得
    /// </summary>
    /// <param name="object">対象オブジェクト</param>
    /// <returns>Matrix4x4: 逆行列</returns>
    Hagine::Matrix4x4 GetParentInverseWorldMatrix(Hagine::BaseObject *object);

    /// <summary>
    /// ローカルコントロールポイントの位置を取得
    /// </summary>
    /// <param name="object">対象オブジェクト</param>
    /// <param name="worldPos">ワールド座標</param>
    /// <returns>Vector3: ローカル座標</returns>
    Hagine::Vector3 GetLocalControlPointPosition(Hagine::BaseObject *object, const Hagine::Vector3 &worldPos);

    /// <summary>
    /// ローカルコントロールポイントをワールド座標に変換
    /// </summary>
    /// <param name="object">対象オブジェクト</param>
    /// <param name="localPos">ローカル座標</param>
    /// <returns>Vector3: ワールド座標</returns>
    Hagine::Vector3 TransformLocalControlPointToWorld(Hagine::BaseObject *object, const Hagine::Vector3 &localPos);

  private:
    /// ===================================================
    /// private varians
    /// ===================================================

    std::unordered_map<Hagine::BaseObject *, Hagine::Vector3> comboStartPositions_; // コンボ開始位置
    std::unordered_map<Hagine::BaseObject *, Hagine::Vector3> comboStartRotations_; // コンボ開始回転
    std::unordered_map<Hagine::BaseObject *, Hagine::Vector3> comboStartScales_;    // コンボ開始スケール
    std::unordered_map<std::string, Motion> motions_;               // モーションマップ
    std::unordered_map<Hagine::BaseObject *, float> attackEndIntervals_;    // 攻撃終了インターバル

    static const float ATTACK_END_INTERVAL; // 攻撃終了後のインターバル時間

    std::string selectedName_;              // 選択中モーション名
    std::string jsonName_;                  // JSONファイル名
    int selectedControlPoint_ = -1;         // 選択中のコントロールポイント番号
};