#pragma once

#include <variant>
// std
#include <array>
#include <initializer_list>
#include <memory>
#include <vector>
#include <wrl.h>

#include "type/Vector2.h"

#define DIRECTNPUT_VERSION 0x0800
#include <XInput.h>
#include <dinput.h>
// input
#include "Mouse.h"
#include <Camera/ViewProjection/ViewProjection.h>
#include <myMath.h>
#include <type/Vector3.h>
#include <type/Vector4.h>

namespace Hagine {
struct Ray {
    Vector3 origin;    // レイの開始点
    Vector3 direction; // レイの方向（正規化済み）
    float length;      // レイの最大長
};

struct RayHitInfo {
    bool hit;               // ヒットしたかどうか
    Vector3 hitPoint;       // ヒット点
    Vector3 hitNormal;      // ヒット面の法線
    float distance;         // レイの開始点からヒット点までの距離
    std::string objectName; // ヒットしたオブジェクト名
};

// ImGuiシーン描画領域情報
struct SceneViewport {
    Vector2 position; // シーンウィンドウの左上座標
    Vector2 size;     // シーンウィンドウのサイズ
};

class BaseObject;
class Input {

  private:
    enum class PadType {
        DirectInput,
        XInput,
    };
    using State = std::variant<DIJOYSTATE2, XINPUT_STATE>;

    struct Joystick {
        Microsoft::WRL::ComPtr<IDirectInputDevice8> device_;
        int32_t deadZoneL_;
        int32_t deadZoneR_;
        PadType type_;
        State state_;
        State statePre_;
    };

  private:
    Microsoft::WRL::ComPtr<IDirectInput8> directInput_ = nullptr;
    Microsoft::WRL::ComPtr<IDirectInputDevice8> keyboard_ = nullptr;
    std::array<BYTE, 256> key_;
    std::array<BYTE, 256> keyPre_;
    std::vector<Joystick> joysticks_;
    // マウス
    static std::unique_ptr<Mouse> mouse_;

    Ray currentRay_;
    SceneViewport currentViewport_;

  public:
    // シングルトンインスタンスの取得
    static Input *GetInstance();
    void Init(HINSTANCE hInstance, HWND hwnd);
    void Update();

    /// <summary>
    /// 押し込んでいるか（単体キー）
    /// </summary>
    bool PushKey(BYTE keyNumber) const;

    /// <summary>
    /// 全キーを押し込んでいるか（複数キー同時押し）
    /// </summary>
    bool PushKey(std::initializer_list<BYTE> keys) const;

    /// <summary>
    /// トリガーしているか（単体キー）
    /// </summary>
    bool TriggerKey(BYTE keyNumber) const;

    /// <summary>
    /// 全キーが揃った瞬間か（複数キー同時押し）
    /// 前フレームで揃っておらず、今フレームで全て押されたときにtrueになる
    /// </summary>
    bool TriggerKey(std::initializer_list<BYTE> keys) const;

    /// <summary>
    /// 離しているか（単体キー）
    /// </summary>
    bool ReleaseKey(BYTE keyNumber) const;

    /// <summary>
    /// いずれかのキーが離されているか（複数キー同時押し）
    /// </summary>
    bool ReleaseKey(std::initializer_list<BYTE> keys) const;

    /// <summary>
    /// 離した瞬間か（単体キー）
    /// </summary>
    bool ReleaseMomentKey(BYTE keyNumber) const;

    /// <summary>
    /// 全キーが揃った状態から、いずれかを離した瞬間か（複数キー同時押し）
    /// </summary>
    bool ReleaseMomentKey(std::initializer_list<BYTE> keys) const;

    /// <summary>
    /// 現在のジョイスティック状態を取得する
    /// </summary>
    template <typename T>
    bool GetJoystickState(int32_t stickNo, T &out) const;

    /// <summary>
    /// 前回のジョイスティック状態を取得する
    /// </summary>
    template <typename T>
    bool GetJoystickStatePrevious(int32_t stickNo, T &out) const;

    /// <summary>
    /// デッドゾーンを設定する
    /// </summary>
    void SetJoystickDeadZone(int32_t stickNo, int32_t deadZoneL, int32_t deadZoneR);

    /// <summary>
    /// 接続されているジョイスティック数を取得する
    /// </summary>
    size_t GetNumberOfJoysticks() const;

    /// <summary>
    /// マウスの押下をチェック
    /// </summary>
    static bool IsPressMouse(int32_t mouseNumber);

    /// <summary>
    /// マウスのトリガーをチェック。押した瞬間だけtrueになる
    /// </summary>
    static bool IsTriggerMouse(int32_t buttonNumber);

    /// <summary>
    /// マウス移動量を取得
    /// </summary>
    static MouseMove GetMouseMove();

    /// <summary>
    /// ホイールスクロール量を取得する
    /// </summary>
    static int32_t GetWheel();

    /// <summary>
    /// マウスの位置を取得する（ウィンドウ座標系）
    /// </summary>
    static Vector2 GetMousePos();

    /// <summary>
    /// 3Dのマウス座標
    /// </summary>
    static Vector3 GetMousePos3D(const ViewProjection &viewprojection, float depthFactor, float blockSpacing = 1.0f);

    /// <summary>
    /// レイを更新する（毎フレーム呼び出す）
    /// </summary>
    void UpdateRay(const ViewProjection &viewprojection, const SceneViewport &viewport, float rayLength = 1000.0f);

    /// <summary>
    /// 現在のレイを取得する
    /// </summary>
    const Ray &GetCurrentRay() const { return currentRay_; }

    /// <summary>
    /// レイとAABBの衝突判定（BaseObject版）
    /// </summary>
    static bool RayIntersectAABB(const Ray &ray, BaseObject *targetObject, RayHitInfo &hitInfo,
                                 const AABB &aabb = {Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)});

    /// <summary>
    /// レイとAABBの衝突判定（ワールド行列直接指定版）
    /// BaseObjectを持たない対象（Sprite・ParticleEmitterなど）に使用する
    /// </summary>
    static bool RayIntersectAABBByMatrix(const Ray &ray, const Matrix4x4 &worldMatrix, RayHitInfo &hitInfo,
                                         const AABB &aabb = {Vector3(-1.0f, -1.0f, -1.0f), Vector3(1.0f, 1.0f, 1.0f)});

    /// <summary>
    /// レイとスフィアの衝突判定（BaseObject版）
    /// </summary>
    static bool RayIntersectSphere(const Ray &ray, BaseObject *targetObject, RayHitInfo &hitInfo,
                                   const Sphere &sphere = {Vector3(0.0f, 0.0f, 0.0f), 1.0f});

    /// <summary>
    /// レイとスフィアの衝突判定（ワールド行列直接指定版）
    /// BaseObjectを持たない対象（Sprite・ParticleEmitterなど）に使用する
    /// </summary>
    static bool RayIntersectSphereByMatrix(const Ray &ray, const Matrix4x4 &worldMatrix, RayHitInfo &hitInfo,
                                           const Sphere &sphere = {Vector3(0.0f, 0.0f, 0.0f), 1.0f});

    /// <summary>
    /// マウス位置からシーン内でのレイを生成する
    /// </summary>
    static Ray CreateRayFromMouse(const Vector2 &mousePos, const ViewProjection &viewprojection,
                                  const SceneViewport &viewport, float rayLength = 1000.0f);

    /// <summary>
    /// 複数のBaseObjectに対してAABBレイキャストを行い、最も近いヒット情報を返す
    /// </summary>
    static RayHitInfo RaycastMultipleAABB(const Ray &ray, const std::vector<BaseObject *> baseObjects);

    /// <summary>
    /// 複数のBaseObjectに対してスフィアレイキャストを行い、最も近いヒット情報を返す
    /// </summary>
    static RayHitInfo RaycastMultipleSphere(const Ray &ray, const std::vector<BaseObject *> baseObjects);

    const BYTE *GetKeyState() const { return key_.data(); }
    const BYTE *GetPreviousKeyState() const { return keyPre_.data(); }
};
} // namespace Hagine
