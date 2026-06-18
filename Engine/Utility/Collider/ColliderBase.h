#pragma once
#include "Camera/ViewProjection/ViewProjection.h"
#include "Collider/ColliderTagManager.h"
#include "Data/DataHandler.h"
#include "myMath.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <functional>
#include <memory>
#include <string>

namespace Hagine {

/// <summary>
/// コライダーの形状種別
/// </summary>
enum class ColliderType {
    Sphere,
    AABB,
    OBB,
    Cylinder,
    Mesh
};

class CollisionManager;

/// <summary>
/// 全コライダーの基底クラス
/// 衝突コールバック、衝突マスク、タグ、描画色などの共通機能を提供する
/// </summary>
class ColliderBase {
  public:
    /// ===================================================
    /// public method
    /// ===================================================

    using CollisionCallback = std::function<void(ColliderBase *)>;

    /// <summary>
    /// コンストラクタ
    /// </summary>
    ColliderBase() = default;

    /// <summary>
    /// デストラクタ
    /// </summary>
    virtual ~ColliderBase();

    /// <summary>
    /// ワールド変換を更新（派生クラスで実装）
    /// </summary>
    virtual void UpdateWorldTransform() = 0;

    /// <summary>
    /// デバッグ描画（派生クラスで実装）
    /// </summary>
    /// <param name="viewProjection">ビュープロジェクション</param>
    virtual void DebugDraw(const ViewProjection &viewProjection) = 0;

    /// <summary>
    /// コライダーの形状種別を取得（派生クラスで実装）
    /// </summary>
    /// <returns>ColliderType: 形状種別</returns>
    virtual ColliderType GetType() const = 0;

    /// <summary>
    /// 衝突した瞬間のコールバックを設定
    /// </summary>
    /// <param name="callback">登録するコールバック</param>
    void SetOnCollisionEnter(CollisionCallback callback) { onCollisionEnter_ = callback; }

    /// <summary>
    /// 衝突継続中のコールバックを設定
    /// </summary>
    /// <param name="callback">登録するコールバック</param>
    void SetOnCollision(CollisionCallback callback) { onCollision_ = callback; }

    /// <summary>
    /// 衝突が離れた瞬間のコールバックを設定
    /// </summary>
    /// <param name="callback">登録するコールバック</param>
    void SetOnCollisionExit(CollisionCallback callback) { onCollisionExit_ = callback; }

    /// <summary>
    /// 衝突した瞬間のコールバックを実行
    /// </summary>
    /// <param name="other">衝突した相手のコライダー</param>
    void TriggerCollisionEnter(ColliderBase *other) {
        if (onCollisionEnter_)
            onCollisionEnter_(other);
    }

    /// <summary>
    /// 衝突継続中のコールバックを実行
    /// </summary>
    /// <param name="other">衝突した相手のコライダー</param>
    void TriggerCollision(ColliderBase *other) {
        if (onCollision_)
            onCollision_(other);
    }

    /// <summary>
    /// 衝突が離れた瞬間のコールバックを実行
    /// </summary>
    /// <param name="other">衝突した相手のコライダー</param>
    void TriggerCollisionExit(ColliderBase *other) {
        if (onCollisionExit_)
            onCollisionExit_(other);
    }

    /// <summary>
    /// 自身のタグを設定
    /// </summary>
    /// <param name="tag">設定するタグ名</param>
    void SetTag(const std::string &tag);

    /// <summary>
    /// 自身のタグを取得
    /// </summary>
    /// <returns>const std::string&: タグ名</returns>
    const std::string &GetTag() const { return tag_; }

    /// <summary>
    /// 衝突対象のタグをマスクに追加（登録済みタグのみ有効）
    /// </summary>
    /// <param name="tag">追加するタグ名</param>
    void AddCollisionMask(const std::string &tag) {
        if (ColliderTagManager::GetInstance()->HasTag(tag)) {
            collisionMask_.insert(tag);
        }
    }

    /// <summary>
    /// 衝突対象のタグをマスクから除去
    /// </summary>
    /// <param name="tag">除去するタグ名</param>
    void RemoveCollisionMask(const std::string &tag) {
        collisionMask_.erase(tag);
    }

    /// <summary>
    /// 衝突マスクを全てクリア
    /// </summary>
    void ClearCollisionMask() {
        collisionMask_.clear();
    }

    /// <summary>
    /// 衝突マスクを取得
    /// </summary>
    /// <returns>const std::unordered_set&lt;std::string&gt;&: 衝突対象タグの集合</returns>
    const std::unordered_set<std::string> &GetCollisionMask() const {
        return collisionMask_;
    }

    /// <summary>
    /// 指定コライダーと衝突すべきか（タグがマスクに含まれるか）を判定
    /// </summary>
    /// <param name="other">判定対象のコライダー</param>
    /// <returns>bool: 衝突対象なら true</returns>
    bool ShouldCollideWith(const ColliderBase *other) const {
        return collisionMask_.find(other->GetTag()) != collisionMask_.end();
    }

    /// <summary>
    /// タグ・マスクを無視して全コライダーと判定するか設定（デバッグ用）
    /// </summary>
    /// <param name="enable">全判定を有効にするなら true</param>
    void SetCollideWithAll(bool enable) { collideWithAll_ = enable; }

    /// <summary>
    /// タグ・マスクを無視して全コライダーと判定するかを取得
    /// </summary>
    /// <returns>bool: 全判定が有効なら true</returns>
    bool CollidesWithAll() const { return collideWithAll_; }

    /// <summary>
    /// 衝突判定の有効/無効を設定
    /// </summary>
    /// <param name="enabled">有効にするなら true</param>
    void SetEnabled(bool enabled) { isEnabled_ = enabled; }

    /// <summary>
    /// 衝突判定が有効かを取得
    /// </summary>
    /// <returns>bool: 有効なら true</returns>
    bool IsEnabled() const { return isEnabled_; }

    /// <summary>
    /// デバッグ表示の可視性を設定
    /// </summary>
    /// <param name="visible">表示するなら true</param>
    void SetVisible(bool visible) { isVisible_ = visible; }

    /// <summary>
    /// デバッグ表示が可視かを取得
    /// </summary>
    /// <returns>bool: 可視なら true</returns>
    bool IsVisible() const { return isVisible_; }

    /// <summary>
    /// 名前を取得
    /// </summary>
    /// <returns>const std::string&: 名前</returns>
    const std::string &GetName() const { return name_; }

    /// <summary>
    /// 名前を設定
    /// </summary>
    /// <param name="name">設定する名前</param>
    void SetName(const std::string &name) { name_ = name; }

    /// <summary>
    /// 描画色を設定
    /// </summary>
    /// <param name="color">設定する色</param>
    void SetColor(const Vector4 &color) { color_ = color; }

    /// <summary>
    /// 描画色を取得
    /// </summary>
    /// <returns>const Vector4&: 現在の色</returns>
    const Vector4 &GetColor() const { return color_; }

    /// <summary>
    /// 描画色を衝突中の色（赤）に設定
    /// </summary>
    void SetHitColor() { color_ = {1.0f, 0.0f, 0.0f, 1.0f}; }

    /// <summary>
    /// 描画色を既定色（白）に設定
    /// </summary>
    void SetDefaultColor() { color_ = {1.0f, 1.0f, 1.0f, 1.0f}; }

    /// <summary>
    /// 現フレームの衝突状態を設定
    /// </summary>
    /// <param name="colliding">衝突中なら true</param>
    void SetCollidingInCurrentFrame(bool colliding) { isCollidingInCurrentFrame_ = colliding; }

    /// <summary>
    /// 現フレームで衝突中かを取得
    /// </summary>
    /// <returns>bool: 衝突中なら true</returns>
    bool IsCollidingInCurrentFrame() const { return isCollidingInCurrentFrame_; }

    /// <summary>
    /// 現フレームの衝突フラグをリセット
    /// </summary>
    void ResetCollisionFlag() { isCollidingInCurrentFrame_ = false; }

    /// <summary>
    /// コライダー設定をJsonへ保存
    /// </summary>
    virtual void SaveToJson();

    /// <summary>
    /// コライダー設定をJsonから読み込み
    /// </summary>
    virtual void LoadFromJson();

    // 中心座標・回転を外部から取得するための関数オブジェクト
    std::function<Vector3()> getPositionFunc_;
    std::function<Quaternion()> getRotationFunc_;

    /// <summary>
    /// 中心座標を取得（取得関数が未設定なら原点を返す）
    /// </summary>
    /// <returns>Vector3: 中心座標</returns>
    Vector3 GetCenterPosition() const {
        return getPositionFunc_ ? getPositionFunc_() : Vector3{0, 0, 0};
    }

    /// <summary>
    /// 中心回転を取得（取得関数が未設定なら単位回転を返す）
    /// </summary>
    /// <returns>Quaternion: 中心回転</returns>
    Quaternion GetCenterRotation() const {
        return getRotationFunc_ ? getRotationFunc_() : Quaternion::IdentityQuaternion();
    }

    /// <summary>
    /// 中心座標の取得関数を設定
    /// </summary>
    /// <param name="func">座標を返す関数</param>
    void SetPositionGetter(std::function<Vector3()> func) { getPositionFunc_ = func; }

    /// <summary>
    /// 中心回転の取得関数を設定
    /// </summary>
    /// <param name="func">回転を返す関数</param>
    void SetRotationGetter(std::function<Quaternion()> func) { getRotationFunc_ = func; }

    bool isRegistered_ = false; // 登録済みフラグ
#ifdef _DEBUG
    /// <summary>
    /// ImGuiでタグ設定UIを表示
    /// </summary>
    void ImGuiTagSettings();
#endif

  protected:
    /// ===================================================
    /// protected variants
    /// ===================================================

    std::string name_;                              // 名前
    std::string tag_ = "None";                      // 自身のタグ
    std::unordered_set<std::string> collisionMask_; // 衝突対象タグの集合
    bool collideWithAll_ = false;                    // タグ無視で全コライダーと判定（デバッグ用）
    bool isEnabled_ = true;                          // 衝突判定の有効フラグ
    bool isVisible_ = true;                          // デバッグ表示の可視フラグ
    bool isCollidingInCurrentFrame_ = false;         // 現フレームの衝突フラグ

    Vector4 color_ = {1.0f, 1.0f, 1.0f, 1.0f}; // デバッグ描画色

    CollisionCallback onCollisionEnter_; // 衝突した瞬間のコールバック
    CollisionCallback onCollision_;      // 衝突継続中のコールバック
    CollisionCallback onCollisionExit_;  // 衝突が離れた瞬間のコールバック

    std::unique_ptr<DataHandler> dataHandler_; // データ管理
};
} // namespace Hagine
