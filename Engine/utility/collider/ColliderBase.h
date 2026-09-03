#pragma once
#include "camera/projection/ViewProjection.h"
#include "collider/ColliderTagManager.h"
#include "data/DataHandler.h"
#include "MyMath.h"
#include "type/Quaternion.h"
#include "type/Vector3.h"
#include <functional>
#include <memory>
#include <string>

namespace Hagine {

/// <summary>
/// コライダーの形状種別
/// </summary>
enum class ColliderType
{
    Sphere,
    AABB,
    OBB,
    Cylinder,
    Mesh
};

/// <summary>
/// 形状種別の名前を返す（保存JSONのファイル名に使う）
/// </summary>
/// <param name="type">形状種別</param>
/// <returns>const char*: 種別名（"Sphere" など）</returns>
inline const char *ColliderTypeName(ColliderType type)
{
    switch (type)
    {
    case ColliderType::Sphere:
        return "Sphere";
    case ColliderType::AABB:
        return "AABB";
    case ColliderType::OBB:
        return "OBB";
    case ColliderType::Cylinder:
        return "Cylinder";
    case ColliderType::Mesh:
        return "Mesh";
    }
    return "Unknown";
}

class CollisionManager;

/// <summary>
/// 全コライダーの基底クラス
/// 衝突コールバック、衝突マスク、タグ、描画色などの共通機能を提供する
/// </summary>
class ColliderBase
{
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
    /// <param name="pOther">衝突した相手のコライダー</param>
    void TriggerCollisionEnter(ColliderBase *pOther)
    {
        if (onCollisionEnter_)
            onCollisionEnter_(pOther);
    }

    /// <summary>
    /// 衝突継続中のコールバックを実行
    /// </summary>
    /// <param name="pOther">衝突した相手のコライダー</param>
    void TriggerCollision(ColliderBase *pOther)
    {
        if (onCollision_)
            onCollision_(pOther);
    }

    /// <summary>
    /// 衝突が離れた瞬間のコールバックを実行
    /// </summary>
    /// <param name="pOther">衝突した相手のコライダー</param>
    void TriggerCollisionExit(ColliderBase *pOther)
    {
        if (onCollisionExit_)
            onCollisionExit_(pOther);
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
    void AddCollisionMask(const std::string &tag)
    {
        if (ColliderTagManager::GetInstance()->HasTag(tag))
        {
            collisionMask_.insert(tag);
        }
    }

    /// <summary>
    /// 衝突対象のタグをマスクから除去
    /// </summary>
    /// <param name="tag">除去するタグ名</param>
    void RemoveCollisionMask(const std::string &tag)
    {
        collisionMask_.erase(tag);
    }

    /// <summary>
    /// 衝突マスクを全てクリア
    /// </summary>
    void ClearCollisionMask()
    {
        collisionMask_.clear();
    }

    /// <summary>
    /// 衝突マスクを取得
    /// </summary>
    /// <returns>const std::unordered_set&lt;std::string&gt;&: 衝突対象タグの集合</returns>
    const std::unordered_set<std::string> &GetCollisionMask() const
    {
        return collisionMask_;
    }

    /// <summary>
    /// 指定コライダーと衝突すべきか（タグがマスクに含まれるか）を判定
    /// </summary>
    /// <param name="pOther">判定対象のコライダー</param>
    /// <returns>bool: 衝突対象なら true</returns>
    bool ShouldCollideWith(const ColliderBase *pOther) const
    {
        return collisionMask_.find(pOther->GetTag()) != collisionMask_.end();
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
    void SetName(const std::string &name)
    {
        if (name_ == name)
        {
            return;
        }
        name_ = name;
        // 保存先のファイル名は名前から決まるので、次の保存／読込で作り直させる
        dataHandler_.reset();
    }

    /// <summary>
    /// このコライダーを持つオブジェクトの名前を設定（保存JSONへ所有者として書き出す）
    /// </summary>
    /// <param name="ownerName">オブジェクト名</param>
    void SetOwnerName(const std::string &ownerName) { ownerName_ = ownerName; }

    /// <summary>
    /// このコライダーを持つオブジェクトの名前を取得
    /// </summary>
    /// <returns>const std::string&: オブジェクト名</returns>
    const std::string &GetOwnerName() const { return ownerName_; }

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
    /// コライダー設定を jsons/Collider/&lt;名前&gt;.json へ保存する。
    /// コライダーの設定はここが唯一の保存場所で、オブジェクトJSONには持たせない
    /// </summary>
    void SaveToJson();

    /// <summary>
    /// jsons/Collider/&lt;名前&gt;.json からコライダー設定を読み込む
    /// </summary>
    void LoadFromJson();

    // 中心座標・回転を外部から取得するための関数オブジェクト
    std::function<Vector3()> getPositionFunc_;
    std::function<Quaternion()> getRotationFunc_;

    /// <summary>
    /// 中心座標を取得（取得関数が未設定なら原点を返す）
    /// </summary>
    /// <returns>Vector3: 中心座標</returns>
    Vector3 GetCenterPosition() const
    {
        return getPositionFunc_ ? getPositionFunc_() : Vector3{0, 0, 0};
    }

    /// <summary>
    /// 中心回転を取得（取得関数が未設定なら単位回転を返す）
    /// </summary>
    /// <returns>Quaternion: 中心回転</returns>
    Quaternion GetCenterRotation() const
    {
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
#ifdef USE_IMGUI
    /// <summary>
    /// ImGuiでタグ設定UIを表示
    /// </summary>
    void ImGuiTagSettings();
#endif

  protected:
    /// ===================================================
    /// protected method
    /// ===================================================

    /// <summary>
    /// 形状ごとの値を保存する（派生クラスで実装）
    /// </summary>
    /// <param name="json">保存先</param>
    virtual void SaveShapeToJson(DataHandler &json) {}

    /// <summary>
    /// 形状ごとの値を読み込む（派生クラスで実装）
    /// </summary>
    /// <param name="json">読み込み元</param>
    virtual void LoadShapeFromJson(DataHandler &json) {}

    /// <summary>
    /// 保存値のタグを反映する。SetTag と違い未登録のタグでも捨てない
    /// （タグを登録し切る前にオブジェクトを読み込んでも設定が消えないようにするため）
    /// </summary>
    /// <param name="tag">反映するタグ名</param>
    void ApplyLoadedTag(const std::string &tag);

    /// ===================================================
    /// protected variables
    /// ===================================================

    std::string name_;                              // 名前
    std::string ownerName_;                         // このコライダーを持つオブジェクト名
    std::string tag_ = "None";                      // 自身のタグ
    std::unordered_set<std::string> collisionMask_; // 衝突対象タグの集合
    bool collideWithAll_ = false;                   // タグ無視で全コライダーと判定（デバッグ用）
    bool isEnabled_ = true;                         // 衝突判定の有効フラグ
    bool isVisible_ = true;                         // デバッグ表示の可視フラグ
    bool isCollidingInCurrentFrame_ = false;        // 現フレームの衝突フラグ

    Vector4 color_ = {1.0f, 1.0f, 1.0f, 1.0f}; // デバッグ描画色

    CollisionCallback onCollisionEnter_; // 衝突した瞬間のコールバック
    CollisionCallback onCollision_;      // 衝突継続中のコールバック
    CollisionCallback onCollisionExit_;  // 衝突が離れた瞬間のコールバック

    std::unique_ptr<DataHandler> dataHandler_; // データ管理
};
} // namespace Hagine
