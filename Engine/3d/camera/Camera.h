#pragma once
#include "camera/projection/ViewProjection.h"
#include "math/Easing.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <string>
#include <vector>

namespace Hagine {
class WorldTransform;

/// <summary>
/// カメラワークの1区間（この位置・向きへ、この時間をかけて動く）
/// </summary>
struct CameraWorkKey
{
    Vector3 position{};                        // 目標位置
    Vector3 rotation{};                        // 目標の向き（オイラー角・ラジアン）
    float duration = 1.0f;                     // ここへ移動するのにかける時間(秒)
    float wait = 0.0f;                         // 到着後の待ち時間(秒)
    EasingType easing = EasingType::InOutSine; // 補間のしかた
    bool lookAtTarget = false;                 // true なら rotation を使わず注視点(SetTarget)を向く
};

/// <summary>
/// カメラクラス
///
/// ViewProjection を内包し、「位置・向き・画角」を直感的に扱えるようにしたもの。
/// 描画系は従来どおり ViewProjection を受け取るので、GetViewProjection() を渡せばよい。
///
/// できること:
///   ・位置/回転の設定、前後左右の移動、注視(LookAt)、周回(Orbit)
///   ・画角/クリップ距離の設定
///   ・カメラワーク: 1区間のイージング移動(EaseTo) / 複数区間の連続再生(PlayCameraWork)
///   ・揺れ(Shake)
///   ・他カメラとの補間(Blend) … カメラ切り替え演出に使う
///
/// 生成・切り替えは CameraManager から行うと名前で扱えて楽（単体でも使える）。
/// </summary>
class Camera
{
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    Camera() = default;
    ~Camera() = default;

    /// <summary>
    /// 初期化
    /// </summary>
    /// <param name="name">カメラ名（保存/読み込みのJSON名にも使う）</param>
    void Initialize(const std::string &name = "Camera");

    /// <summary>
    /// 更新（カメラワーク・シェイクを進め、行列を作り直す）
    /// </summary>
    void Update();

    /// <summary>
    /// ImGuiでの編集UIを表示する
    /// </summary>
    void DrawImGui();

    /// <summary>
    /// 専用ウィンドウを開いて編集UIを表示する（旧 ViewProjection::ShowDebugInfo 相当）
    /// </summary>
    void ShowDebugWindow();

    /// ===================================================
    /// 位置・向き
    /// ===================================================

    /// <summary>位置を設定する（行列は即座に作り直されるので同フレームで参照できる）</summary>
    void SetPosition(const Vector3 &position);
    /// <summary>位置を取得する</summary>
    const Vector3 &GetPosition() const { return position_; }

    /// <summary>向きをオイラー角(ラジアン)で設定する</summary>
    void SetRotation(const Vector3 &eulerRadians);
    /// <summary>向きをオイラー角(ラジアン)で取得する</summary>
    const Vector3 &GetRotation() const { return rotation_; }

    /// <summary>向きをクォータニオンで設定する（以降クォータニオン運用になる）</summary>
    void SetQuaternion(const Quaternion &rotation);
    /// <summary>向きをクォータニオンで取得する（クォータニオン運用中の生値）</summary>
    const Quaternion &GetQuaternion() const { return quaternion_; }

    /// <summary>
    /// 今の向きをクォータニオンで取得する。
    /// オイラー角で動かしているカメラでも変換して返すので、運用方法を気にしなくてよい。
    /// </summary>
    /// <returns>Quaternion: 現在の向き</returns>
    Quaternion GetRotationQuaternion() const;

    /// <summary>ワールド座標のぶんだけ動かす</summary>
    void Translate(const Vector3 &worldDelta);

    /// <summary>カメラ基準（右/上/前）で動かす。前進は z を正にする</summary>
    void MoveLocal(const Vector3 &localDelta);

    /// <summary>
    /// 指定座標を向く（1回だけ）。以降も向き続けたい場合は SetTarget を使う。
    /// </summary>
    /// <param name="target">注視するワールド座標</param>
    void LookAt(const Vector3 &target);

    /// <summary>
    /// 注視点を設定する。設定中は Update のたびにその点を向き続ける。
    /// </summary>
    /// <param name="target">注視するワールド座標</param>
    void SetTarget(const Vector3 &target);

    /// <summary>
    /// 追従する注視対象を設定する（毎フレーム対象のワールド座標を向く）。
    /// nullptr を渡すと解除。
    /// </summary>
    /// <param name="pTarget">注視対象のワールド変換</param>
    void SetTargetTransform(const WorldTransform *pTarget) { pTargetTransform_ = pTarget; }

    /// <summary>注視をやめる（向きは手動設定に戻る）</summary>
    void ClearTarget();

    /// <summary>
    /// 中心の周りを回る位置へ移動して中心を向く（周回カメラ）。
    /// </summary>
    /// <param name="center">中心のワールド座標</param>
    /// <param name="yaw">水平角(ラジアン)</param>
    /// <param name="pitch">仰角(ラジアン)。正で見下ろし</param>
    /// <param name="distance">中心からの距離</param>
    void Orbit(const Vector3 &center, float yaw, float pitch, float distance);

    /// <summary>カメラの前方向（ワールド）を取得する</summary>
    Vector3 GetForward() const;
    /// <summary>カメラの右方向（ワールド）を取得する</summary>
    Vector3 GetRight() const;
    /// <summary>カメラの上方向（ワールド）を取得する</summary>
    Vector3 GetUp() const;

    /// ===================================================
    /// レンズ
    /// ===================================================

    /// <summary>垂直画角を度で設定する</summary>
    void SetFovYDegrees(float degrees);
    /// <summary>垂直画角を度で取得する</summary>
    float GetFovYDegrees() const;
    /// <summary>近面・遠面のクリップ距離を設定する</summary>
    void SetClipRange(float nearZ, float farZ);
    /// <summary>アスペクト比を設定する（既定は画面の仮想解像度から算出）</summary>
    void SetAspectRatio(float aspectRatio);

    /// ===================================================
    /// カメラワーク
    /// ===================================================

    /// <summary>
    /// 今の位置・向きから、指定の位置・向きへイージングで移動する
    /// </summary>
    /// <param name="position">目標位置</param>
    /// <param name="eulerRadians">目標の向き(オイラー角・ラジアン)</param>
    /// <param name="duration">かける時間(秒)</param>
    /// <param name="easing">補間のしかた</param>
    void EaseTo(const Vector3 &position, const Vector3 &eulerRadians,
                float duration, EasingType easing = EasingType::InOutSine);

    /// <summary>
    /// キーを順に辿るカメラワークを再生する（今の位置が開始点になる）
    /// </summary>
    /// <param name="keys">辿る区間の列</param>
    /// <param name="loop">最後まで再生したら先頭へ戻るか</param>
    void PlayCameraWork(const std::vector<CameraWorkKey> &keys, bool loop = false);

    /// <summary>カメラワークを止める（その場で停止）</summary>
    void StopCameraWork();

    /// <summary>カメラワーク（EaseTo を含む）を再生中か</summary>
    bool IsCameraWorkPlaying() const { return workPlaying_; }

    /// <summary>
    /// カメラを揺らす（衝撃演出用）。位置に対してランダムなオフセットを乗せる。
    /// </summary>
    /// <param name="duration">揺れる時間(秒)</param>
    /// <param name="strength">揺れ幅（ワールド単位）</param>
    void Shake(float duration, float strength);

    /// <summary>揺れを即座に止める</summary>
    void StopShake();

    /// <summary>
    /// 外部の演出（画面揺れなど）による一時的なずれを設定する。
    /// 毎フレーム上書きされるので、効かせ続けたい間は毎フレーム呼ぶこと。
    /// </summary>
    /// <param name="positionOffset">ビュー空間での位置のずれ</param>
    /// <param name="pitchOffset">ビュー空間での傾き(ラジアン)</param>
    void SetExternalOffset(const Vector3 &positionOffset, float pitchOffset = 0.0f);

    /// ===================================================
    /// カメラ同士の合成（切り替え演出）
    /// ===================================================

    /// <summary>他のカメラの位置・向き・レンズをそのままコピーする</summary>
    void CopyStateFrom(const Camera &other);

    /// <summary>
    /// 2つのカメラの間を補間して自分に反映する（0で from、1で to）
    /// </summary>
    /// <param name="from">開始カメラ</param>
    /// <param name="to">終了カメラ</param>
    /// <param name="t">補間係数[0,1]</param>
    void BlendFrom(const Camera &from, const Camera &to, float t);

    /// ===================================================
    /// 保存・読み込み（Assets/jsons/Camera/&lt;name&gt;.json）
    /// ===================================================

    /// <summary>現在の位置・向き・レンズを保存する</summary>
    /// <param name="jsonName">保存名（省略時はカメラ名）</param>
    void Save(const std::string &jsonName = {}) const;

    /// <summary>保存済みの位置・向き・レンズを読み込む</summary>
    /// <param name="jsonName">読み込み名（省略時はカメラ名）</param>
    void Load(const std::string &jsonName = {});

    /// <summary>
    /// 保存済みのカメラ位置・向きへイージング移動する（保存した構図へ寄せる演出）
    /// </summary>
    /// <param name="jsonName">読み込み名</param>
    /// <param name="duration">かける時間(秒)</param>
    /// <param name="easing">補間のしかた</param>
    void EaseToSaved(const std::string &jsonName, float duration, EasingType easing = EasingType::InOutSine);

    /// ===================================================
    /// Getter / Setter
    /// ===================================================

    /// <summary>描画系へ渡すビュープロジェクションを取得する</summary>
    ViewProjection &GetViewProjection() { return viewProjection_; }
    const ViewProjection &GetViewProjection() const { return viewProjection_; }

    const std::string &GetName() const { return name_; }
    void SetName(const std::string &name) { name_ = name; }

  private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>カメラワークの進行を1フレームぶん進める</summary>
    void UpdateCameraWork(float deltaTime);
    /// <summary>シェイクの進行を1フレームぶん進める</summary>
    void UpdateShake(float deltaTime);
    /// <summary>注視対象がいる場合にそちらを向く</summary>
    void UpdateLookAt();
    /// <summary>位置・向きから行列を作り直して ViewProjection へ反映する</summary>
    void ApplyToViewProjection();
    /// <summary>指定座標を向くオイラー角を求める</summary>
    Vector3 CalcLookAtRotation(const Vector3 &target) const;

  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::string name_ = "Camera";

    // 描画系へ渡す本体（行列と定数バッファはこの中で管理される）
    ViewProjection viewProjection_;

    Vector3 position_ = {0.0f, 0.0f, -10.0f};                    // 位置
    Vector3 rotation_ = {0.0f, 0.0f, 0.0f};                      // 向き（オイラー角・ラジアン）
    Quaternion quaternion_ = Quaternion::IdentityQuaternion();   // 向き（クォータニオン）
    bool useQuaternion_ = false;                                 // true ならクォータニオンで向きを決める

    // 注視
    bool hasTarget_ = false;                            // 注視点を使うか
    Vector3 target_{};                                  // 注視点（ワールド座標）
    const WorldTransform *pTargetTransform_ = nullptr;  // 追従する注視対象（あれば target_ より優先）

    // カメラワーク
    std::vector<CameraWorkKey> workKeys_; // 再生中のキー列
    size_t workIndex_ = 0;                // 再生中のキー番号
    float workTime_ = 0.0f;               // 現在キーの経過時間
    bool workPlaying_ = false;            // 再生中か
    bool workLoop_ = false;               // ループ再生か
    bool workWaiting_ = false;            // 到着後の待ち時間中か
    Vector3 workStartPosition_{};         // 現在キーの開始位置
    Vector3 workStartRotation_{};         // 現在キーの開始の向き

    // シェイク
    float shakeTime_ = 0.0f;     // 残り時間
    float shakeDuration_ = 0.0f; // 全体時間
    float shakeStrength_ = 0.0f; // 揺れ幅
    Vector3 shakeOffset_{};      // 今フレームの揺れオフセット

    // 外部演出（画面揺れクラスなど）から毎フレーム渡されるビュー空間のずれ
    Vector3 externalOffset_{};
    float externalPitch_ = 0.0f;
};
} // namespace Hagine
