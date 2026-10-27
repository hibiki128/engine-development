#pragma once
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Hagine {

class BaseObject;
class ColliderBase;
struct ContactManifold;

/// <summary>
/// 物理ワールドのシミュレーション設定
/// </summary>
struct PhysicsSettings
{
    int subSteps = 4;                   // 1フレームの分割数
    int velocityIterations = 10;        // 速度拘束の反復回数
    float penetrationSlop = 0.005f;     // 許容めり込み量
    float positionCorrection = 0.8f;    // 1ステップで解消するめり込みの割合
    float maxCorrection = 0.2f;         // 1ステップのめり込み解消量の上限
    float restitutionThreshold = 0.5f;  // 反発を適用する最小接近速度
    bool enableSleep = true;            // スリープの有効フラグ
    float sleepLinearSpeed = 0.05f;     // スリープ判定の速度
    float sleepAngularSpeed = 0.08f;    // スリープ判定の角速度
    float sleepTime = 0.5f;             // スリープまでの静止秒数
    float maxKinematicSpeed = 30.0f;    // ワープ扱いにする静的物体の移動速度
    float maxKinematicAngularSpeed = 30.0f; // ワープ扱いにする静的物体の回転速度
};

/// <summary>
/// 回転有効のリジッドボディを、静的・キネマティック物体や他の剛体との接触込みでまとめて解く物理ワールド
/// </summary>
class PhysicsWorld
{
  public:
    /// <summary>
    /// インスタンスを取得
    /// </summary>
    static PhysicsWorld *GetInstance();

    /// <summary>
    /// 1フレーム分のシミュレーションを実行
    /// </summary>
    /// <param name="objects">対象オブジェクト</param>
    /// <param name="deltaTime">前フレームからの経過秒</param>
    void Step(const std::unordered_map<std::string, BaseObject *> &objects, float deltaTime);

    /// <summary>
    /// 時間の連続性の途切れを通知（一時停止中など。次ステップで静的物体の速度推定をリセット）
    /// </summary>
    void MarkDiscontinuity() { discontinuity_ = true; }

    /// <summary>
    /// 接触キャッシュ・姿勢履歴・スリープ状態を破棄
    /// </summary>
    void Reset();

    /// <summary>
    /// 剛体がスリープ中かを取得
    /// </summary>
    bool IsSleeping(const BaseObject *pObject) const;

    /// <summary>
    /// 剛体のスリープを解除
    /// </summary>
    void WakeUp(const BaseObject *pObject);

    /// <summary>
    /// シミュレーション設定を取得
    /// </summary>
    PhysicsSettings &GetSettings() { return settings_; }

#ifdef USE_IMGUI
    /// <summary>
    /// 設定UIを描画
    /// </summary>
    void DrawSettingsImGui();
#endif // USE_IMGUI

  private:
    /// <summary>
    /// 3x3 行列（慣性テンソル用）
    /// </summary>
    struct Matrix3
    {
        float m[3][3] = {};

        Vector3 operator*(const Vector3 &v) const;
        Matrix3 Inverse() const;
    };

    /// <summary>
    /// 1ステップ中の物体の状態
    /// </summary>
    struct Body
    {
        BaseObject *pObject = nullptr;
        bool dynamic = false;              // 剛体として解くか
        bool sleeping = false;             // スリープ中か
        bool moved = false;                // 静的物体が今フレーム動いたか
        bool interpolate = false;          // サブステップ間で姿勢を補間するか（動いた静的物体）
        Vector3 startPosition{};           // 補間開始位置（前フレーム）
        Vector3 endPosition{};             // 補間終了位置（今フレーム）
        Quaternion startRotation{};        // 補間開始回転
        Quaternion endRotation{};          // 補間終了回転
        float inverseMass = 0.0f;          // 質量の逆数
        Matrix3 inverseInertia;            // ワールド空間の逆慣性テンソル
        Vector3 centerOfMass{};            // 重心（ワールド）
        Vector3 linearVelocity{};          // 重心速度（静的物体は推定値）
        Vector3 angularVelocity{};         // 角速度（静的物体は推定値）
        float friction = 0.3f;             // 摩擦係数
        float restitution = 0.0f;          // 反発係数
        std::vector<ColliderBase *> colliders;
        Vector3 boundsCenter{};            // 全コライダーを含む球の中心
        float boundingRadius = 0.0f;       // 全コライダーを含む球の半径
    };

    /// <summary>
    /// 接触したコライダーの組（位置補正用）
    /// </summary>
    struct ManifoldRecord
    {
        int bodyA = -1;
        int bodyB = -1;
        ColliderBase *pColliderA = nullptr;
        ColliderBase *pColliderB = nullptr;
    };

    /// <summary>
    /// 接触点1つ分の拘束
    /// </summary>
    struct ContactConstraint
    {
        int bodyA = -1;
        int bodyB = -1;
        uint64_t key = 0;
        Vector3 normal{};   // A を B から押し出す向き
        Vector3 tangent1{};
        Vector3 tangent2{};
        Vector3 rA{};
        Vector3 rB{};
        float depth = 0.0f;
        float normalMass = 0.0f;
        float tangentMass1 = 0.0f;
        float tangentMass2 = 0.0f;
        float targetSpeed = 0.0f;
        float friction = 0.0f;
        float normalImpulse = 0.0f;
        float tangentImpulse1 = 0.0f;
        float tangentImpulse2 = 0.0f;
    };

    /// <summary>
    /// 接触インパルスのキャッシュ（ウォームスタート用）
    /// </summary>
    struct CachedImpulse
    {
        float normal = 0.0f;
        Vector3 tangent{}; // 接線方向インパルス（ワールド）
    };

    /// <summary>
    /// フレームをまたいで保持する物体ごとの情報
    /// </summary>
    struct BodyRecord
    {
        std::string name;
        Vector3 position{};             // 前ステップ終了時のワールド位置
        Quaternion rotation{};          // 前ステップ終了時のワールド回転
        bool sleeping = false;
        float sleepTimer = 0.0f;
    };

    /// <summary>
    /// 物体リストを構築し、静的物体の速度を推定
    /// </summary>
    void BuildBodies(const std::unordered_map<std::string, BaseObject *> &objects, float frameTime);

    /// <summary>
    /// 剛体の重心と慣性テンソルを算出
    /// </summary>
    void ComputeMassProperties(Body &body) const;

    /// <summary>
    /// 1サブステップ分のシミュレーション
    /// </summary>
    /// <param name="stepTime">サブステップの秒数</param>
    /// <param name="endFraction">サブステップ終了時点のフレーム内割合（0〜1）</param>
    void SubStep(float stepTime, float endFraction);

    /// <summary>
    /// 動いた静的物体をフレーム内割合 fraction の姿勢へ移動
    /// </summary>
    void SetKinematicPoses(float fraction);

    /// <summary>
    /// 接触を検出して拘束を構築
    /// </summary>
    void BuildContacts(float stepTime);

    /// <summary>
    /// 2コライダー間の接触情報を算出
    /// </summary>
    bool CollideColliders(ColliderBase *a, ColliderBase *b, ContactManifold &outManifold) const;

    /// <summary>
    /// 接触点での相対速度（A − B）
    /// </summary>
    Vector3 RelativeVelocity(const ContactConstraint &c) const;

    /// <summary>
    /// 接触点に互いに逆向きのインパルスを加える
    /// </summary>
    void ApplyImpulse(const ContactConstraint &c, const Vector3 &impulse);

    /// <summary>
    /// 方向 direction に対する有効質量
    /// </summary>
    float EffectiveMass(const ContactConstraint &c, const Vector3 &direction) const;

    /// <summary>
    /// 剛体の姿勢を速度で進め、トランスフォームへ反映
    /// </summary>
    void IntegratePositions(float stepTime);

    /// <summary>
    /// めり込みを位置補正で解消
    /// </summary>
    void CorrectPositions();

    /// <summary>
    /// 剛体を平行移動し、トランスフォームとコライダーを更新
    /// </summary>
    void TranslateBody(Body &body, const Vector3 &offset);

    /// <summary>
    /// スリープ中の剛体を起こす
    /// </summary>
    void WakeBody(Body &body);

    /// <summary>
    /// 速度を持って解く対象か（スリープ中の剛体は静的物体として扱う）
    /// </summary>
    static bool IsSimulated(const Body &body);

    /// <summary>
    /// スリープ判定速度を明確に超えて動いているか
    /// </summary>
    bool IsMoving(const Body &body) const;

    /// <summary>
    /// スリープ判定と状態保存
    /// </summary>
    void UpdateSleepAndRecords(float frameTime);

    PhysicsSettings settings_;
    std::vector<Body> bodies_;
    std::vector<ContactConstraint> contacts_;
    std::vector<ManifoldRecord> manifolds_;
    std::unordered_map<BaseObject *, int> bodyIndex_;
    std::unordered_map<const BaseObject *, BodyRecord> records_;
    std::unordered_map<uint64_t, CachedImpulse> impulseCache_;
    bool discontinuity_ = true;
};

} // namespace Hagine
