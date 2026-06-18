#define NOMINMAX
#include "Input.h"
#include <Line/DrawLine3D.h>
#include <assert.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")
#include "Object/Base/BaseObject.h"

namespace Hagine {
std::unique_ptr<Mouse> Input::mouse_ = nullptr;

template bool Input::GetJoystickState<DIJOYSTATE2>(int32_t stickNo, DIJOYSTATE2 &out) const;
template bool Input::GetJoystickState<XINPUT_STATE>(int32_t stickNo, XINPUT_STATE &out) const;
template bool Input::GetJoystickStatePrevious<DIJOYSTATE2>(int32_t stickNo, DIJOYSTATE2 &out) const;
template bool Input::GetJoystickStatePrevious<XINPUT_STATE>(int32_t stickNo, XINPUT_STATE &out) const;

Input *Input::GetInstance() {
    static Input instance;
    return &instance;
}

void Input::Init(HINSTANCE hInstance, HWND hWnd) {
    // DirectInputの初期化
    HRESULT result = DirectInput8Create(
        hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8,
        (void **)&directInput_, nullptr);
    assert(SUCCEEDED(result));

    // キーボードデバイスの生成
    result = directInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
    assert(SUCCEEDED(result));

    // 入力データ形式のセット
    result = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(result));

    // 排他制御レベルのセット
    result = keyboard_->SetCooperativeLevel(
        hWnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(result));

    // マウス初期化
    mouse_ = std::make_unique<Mouse>();
    mouse_->Init(directInput_, hWnd);

    // XInputデバイスの追加
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        XINPUT_STATE state;
        ZeroMemory(&state, sizeof(XINPUT_STATE));
        if (XInputGetState(i, &state) == ERROR_SUCCESS) {
            Joystick joystick = {};
            joystick.type_ = PadType::XInput;
            joystick.state_ = state;
            joystick.statePre_ = state;
            joystick.deadZoneL_ = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
            joystick.deadZoneR_ = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
            joysticks_.push_back(joystick);
        }
    }
}

void Input::Update() {
    keyPre_ = key_;
    keyboard_->Acquire();
    keyboard_->GetDeviceState(sizeof(key_), key_.data());

    mouse_->Update();

    for (auto &joystick : joysticks_) {
        if (joystick.type_ == PadType::XInput) {
            DWORD dwResult;
            XINPUT_STATE state;
            ZeroMemory(&state, sizeof(XINPUT_STATE));
            dwResult = XInputGetState(0, &state);
            if (dwResult == ERROR_SUCCESS) {
                joystick.statePre_ = joystick.state_;
                joystick.state_ = state;
            }
        }
    }
}

// キーボード*************************************************************

bool Input::PushKey(BYTE keyNumber) const {
    return (key_[keyNumber] & 0x80);
}

// 全キーが現在押されているかを判定する
bool Input::PushKey(std::initializer_list<BYTE> keys) const {
    for (BYTE key : keys) {
        if (!(key_[key] & 0x80)) {
            return false;
        }
    }
    return true;
}

bool Input::TriggerKey(BYTE keyNumber) const {
    return (key_[keyNumber] & 0x80) && !(keyPre_[keyNumber] & 0x80);
}

// 全キーが今フレームで揃い、前フレームでは少なくとも1つが押されていなかった瞬間を検出する
// キーを1つずつ押す際のフレームズレを吸収するため、「全て揃った最初のフレーム」で発火する
bool Input::TriggerKey(std::initializer_list<BYTE> keys) const {
    // 今フレームで全キーが押されているか
    for (BYTE key : keys) {
        if (!(key_[key] & 0x80)) {
            return false;
        }
    }
    // 前フレームで少なくとも1つが押されていなければトリガーとみなす
    for (BYTE key : keys) {
        if (!(keyPre_[key] & 0x80)) {
            return true;
        }
    }
    return false;
}

bool Input::ReleaseKey(BYTE keyNumber) const {
    return !(key_[keyNumber] & 0x80);
}

// いずれかのキーが押されていない状態をリリースとみなす
bool Input::ReleaseKey(std::initializer_list<BYTE> keys) const {
    for (BYTE key : keys) {
        if (!(key_[key] & 0x80)) {
            return true;
        }
    }
    return false;
}

bool Input::ReleaseMomentKey(BYTE keyNumber) const {
    return !(key_[keyNumber] & 0x80) && (keyPre_[keyNumber] & 0x80);
}

// 前フレームで全キーが揃っており、今フレームでいずれかが離された瞬間を検出する
bool Input::ReleaseMomentKey(std::initializer_list<BYTE> keys) const {
    // 前フレームで全キーが押されていたか
    for (BYTE key : keys) {
        if (!(keyPre_[key] & 0x80)) {
            return false;
        }
    }
    // 今フレームで少なくとも1つが離されているか
    for (BYTE key : keys) {
        if (!(key_[key] & 0x80)) {
            return true;
        }
    }
    return false;
}

// ゲームパッド*******************************************************************

template <typename T>
bool Input::GetJoystickState(int32_t stickNo, T &out) const {
    if (stickNo < 0 || stickNo >= joysticks_.size()) {
        return false;
    }
    const Joystick &joystick = joysticks_[stickNo];
    if (joystick.type_ == PadType::DirectInput) {
        if constexpr (std::is_same<T, DIJOYSTATE2>::value) {
            if (std::holds_alternative<T>(joystick.state_)) {
                out = std::get<T>(joystick.state_);
                return true;
            }
        }
    } else if (joystick.type_ == PadType::XInput) {
        if constexpr (std::is_same<T, XINPUT_STATE>::value) {
            if (std::holds_alternative<T>(joystick.state_)) {
                out = std::get<T>(joystick.state_);
                return true;
            }
        }
    }
    return false;
}

template <typename T>
bool Input::GetJoystickStatePrevious(int32_t stickNo, T &out) const {
    if (stickNo < 0 || stickNo >= joysticks_.size()) {
        return false;
    }
    const Joystick &joystick = joysticks_[stickNo];
    if (joystick.type_ == PadType::DirectInput) {
        if constexpr (std::is_same<T, DIJOYSTATE2>::value) {
            if (std::holds_alternative<T>(joystick.statePre_)) {
                out = std::get<T>(joystick.statePre_);
                return true;
            }
        }
    } else if (joystick.type_ == PadType::XInput) {
        if constexpr (std::is_same<T, XINPUT_STATE>::value) {
            if (std::holds_alternative<T>(joystick.statePre_)) {
                out = std::get<T>(joystick.statePre_);
                return true;
            }
        }
    }
    return false;
}

void Input::SetJoystickDeadZone(int32_t stickNo, int32_t deadZoneL, int32_t deadZoneR) {
    if (stickNo < 0 || stickNo >= static_cast<int32_t>(joysticks_.size())) {
        return;
    }
    joysticks_[stickNo].deadZoneL_ = deadZoneL;
    joysticks_[stickNo].deadZoneR_ = deadZoneR;
}

size_t Input::GetNumberOfJoysticks() const {
    return joysticks_.size();
}

// マウス****************************************************************

bool Input::IsPressMouse(int32_t buttonNumber) {
    return mouse_->IsPressMouse(buttonNumber);
}

bool Input::IsTriggerMouse(int32_t buttonNumber) {
    return mouse_->IsTriggerMouse(buttonNumber);
}

MouseMove Input::GetMouseMove() {
    return mouse_->GetMouseMove();
}

Vector3 Input::GetMousePos3D(const ViewProjection &viewprojection, float depthFactor, float blockSpacing) {
    return mouse_->GetMousePos3D(viewprojection, depthFactor, blockSpacing);
}

int32_t Input::GetWheel() {
    return mouse_->GetWheel();
}

Vector2 Input::GetMousePos() {
    return mouse_->GetMousePos();
}

// レイ*****************************************************************

void Input::UpdateRay(const ViewProjection &viewprojection, const SceneViewport &viewport, float rayLength) {
    currentViewport_ = viewport;
    Vector2 mousePos = GetMousePos();
    currentRay_ = CreateRayFromMouse(mousePos, viewprojection, viewport, rayLength);
}

Ray Input::CreateRayFromMouse(const Vector2 &mousePos, const ViewProjection &viewprojection,
                              const SceneViewport &viewport, float rayLength) {
    Ray ray;

    float relativeX = mousePos.x - viewport.position.x;
    float relativeY = mousePos.y - viewport.position.y;

    // シーン外の場合は無効なレイを返す
    if (relativeX < 0 || relativeY < 0 || relativeX > viewport.size.x || relativeY > viewport.size.y) {
        ray.origin = {0, 0, 0};
        ray.direction = {0, 0, 1};
        ray.length = 0;
        return ray;
    }

    // シーン内の正規化座標に変換 [-1, 1]
    float ndcX = (2.0f * relativeX / viewport.size.x) - 1.0f;
    float ndcY = 1.0f - (2.0f * relativeY / viewport.size.y);

    Vector3 nearPoint = {ndcX, ndcY, 0.0f};
    Vector3 farPoint = {ndcX, ndcY, 1.0f};

    // クリップ空間からワールド空間へ逆変換
    Matrix4x4 invViewProj = Inverse((viewprojection.matView_ * viewprojection.matProjection_));
    Vector3 worldNear = Transformation(nearPoint, invViewProj);
    Vector3 worldFar = Transformation(farPoint, invViewProj);

    ray.origin = worldNear;
    ray.direction = (worldFar - worldNear).Normalize();
    ray.length = rayLength;

    return ray;
}

// ---- AABB 衝突判定（行列直接指定版）------------------------------------
// スラブ法により、ワールド行列から構築したAABBとレイの交差を判定する
bool Input::RayIntersectAABBByMatrix(const Ray &ray, const Matrix4x4 &worldMatrix, RayHitInfo &hitInfo, const AABB &aabb) {
    // ローカル空間のAABB 8頂点をワールド空間に変換
    Vector3 localCorners[8] = {
        {aabb.min.x, aabb.min.y, aabb.min.z},
        {aabb.max.x, aabb.min.y, aabb.min.z},
        {aabb.min.x, aabb.max.y, aabb.min.z},
        {aabb.max.x, aabb.max.y, aabb.min.z},
        {aabb.min.x, aabb.min.y, aabb.max.z},
        {aabb.max.x, aabb.min.y, aabb.max.z},
        {aabb.min.x, aabb.max.y, aabb.max.z},
        {aabb.max.x, aabb.max.y, aabb.max.z},
    };

    Vector3 worldCorners[8];
    for (int i = 0; i < 8; ++i) {
        worldCorners[i] = Transformation(localCorners[i], worldMatrix);
    }

    // ワールド空間での AABB min/max を算出
    Vector3 boxMin = worldCorners[0];
    Vector3 boxMax = worldCorners[0];
    for (int i = 1; i < 8; ++i) {
        boxMin.x = std::min(boxMin.x, worldCorners[i].x);
        boxMin.y = std::min(boxMin.y, worldCorners[i].y);
        boxMin.z = std::min(boxMin.z, worldCorners[i].z);
        boxMax.x = std::max(boxMax.x, worldCorners[i].x);
        boxMax.y = std::max(boxMax.y, worldCorners[i].y);
        boxMax.z = std::max(boxMax.z, worldCorners[i].z);
    }

    // スラブ法による交差判定
    float tMin = 0.0f;
    float tMax = ray.length;

    for (int i = 0; i < 3; ++i) {
        float rayDir = (&ray.direction.x)[i];
        float rayOrig = (&ray.origin.x)[i];
        float boxMinVal = (&boxMin.x)[i];
        float boxMaxVal = (&boxMax.x)[i];

        if (std::abs(rayDir) < 1e-6f) {
            if (rayOrig < boxMinVal || rayOrig > boxMaxVal) {
                hitInfo.hit = false;
                return false;
            }
        } else {
            float t1 = (boxMinVal - rayOrig) / rayDir;
            float t2 = (boxMaxVal - rayOrig) / rayDir;
            if (t1 > t2)
                std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                hitInfo.hit = false;
                return false;
            }
        }
    }

    if (tMin < 0.0f)
        tMin = 0.0f;

    if (tMin >= 0 && tMin <= ray.length) {
        hitInfo.hit = true;
        hitInfo.distance = tMin;
        hitInfo.hitPoint = ray.origin + (ray.direction * tMin);

        // 最も近い面の法線を選択
        Vector3 center = (boxMin + boxMax) * 0.5f;
        Vector3 toHit = hitInfo.hitPoint - center;
        Vector3 extent = (boxMax - boxMin) * 0.5f;

        float maxComponent = 0;
        int maxIndex = 0;
        for (int i = 0; i < 3; ++i) {
            float normalized = (&toHit.x)[i] / (&extent.x)[i];
            if (std::abs(normalized) > maxComponent) {
                maxComponent = std::abs(normalized);
                maxIndex = i;
            }
        }
        hitInfo.hitNormal = {0, 0, 0};
        (&hitInfo.hitNormal.x)[maxIndex] = ((&toHit.x)[maxIndex] > 0) ? 1.0f : -1.0f;
        return true;
    }

    hitInfo.hit = false;
    return false;
}

// ---- AABB 衝突判定（BaseObject版）------------------------------------
// ワールド行列を BaseObject から取得し、行列版に委譲する
bool Input::RayIntersectAABB(const Ray &ray, BaseObject *targetObject, RayHitInfo &hitInfo, const AABB &aabb) {
    Matrix4x4 worldMatrix = targetObject->GetWorldTransform()->matWorld_;
    bool result = RayIntersectAABBByMatrix(ray, worldMatrix, hitInfo, aabb);
    if (result) {
        hitInfo.objectName = targetObject->GetName();
    }
    return result;
}

// ---- スフィア衝突判定（行列直接指定版）---------------------------------
// ワールド行列からスケール付き球の交差判定を行う
bool Input::RayIntersectSphereByMatrix(const Ray &ray, const Matrix4x4 &worldMatrix, RayHitInfo &hitInfo, const Sphere &sphere) {
    Vector3 worldCenter = Transformation(sphere.center, worldMatrix);

    // スケールを考慮した半径（非一様スケールには最大軸を使用）
    Vector3 scale = {
        sqrt(worldMatrix.m[0][0] * worldMatrix.m[0][0] + worldMatrix.m[1][0] * worldMatrix.m[1][0] + worldMatrix.m[2][0] * worldMatrix.m[2][0]),
        sqrt(worldMatrix.m[0][1] * worldMatrix.m[0][1] + worldMatrix.m[1][1] * worldMatrix.m[1][1] + worldMatrix.m[2][1] * worldMatrix.m[2][1]),
        sqrt(worldMatrix.m[0][2] * worldMatrix.m[0][2] + worldMatrix.m[1][2] * worldMatrix.m[1][2] + worldMatrix.m[2][2] * worldMatrix.m[2][2])};
    float worldRadius = sphere.radius * std::max({scale.x, scale.y, scale.z});

    Vector3 oc = ray.origin - worldCenter;

    float a = ray.direction.Dot(ray.direction);
    float b = 2.0f * oc.Dot(ray.direction);
    float c = oc.Dot(oc) - worldRadius * worldRadius;

    float discriminant = b * b - 4 * a * c;
    if (discriminant < 0) {
        hitInfo.hit = false;
        return false;
    }

    float sqrtD = sqrt(discriminant);
    float t1 = (-b - sqrtD) / (2.0f * a);
    float t2 = (-b + sqrtD) / (2.0f * a);

    float t = -1;
    if (t1 >= 0 && t1 <= ray.length)
        t = t1;
    else if (t2 >= 0 && t2 <= ray.length)
        t = t2;

    if (t >= 0) {
        hitInfo.hit = true;
        hitInfo.distance = t;
        hitInfo.hitPoint = ray.origin + (ray.direction * t);
        hitInfo.hitNormal = (hitInfo.hitPoint - worldCenter).Normalize();
        return true;
    }

    hitInfo.hit = false;
    return false;
}

// ---- スフィア衝突判定（BaseObject版）----------------------------------
// ワールド行列を BaseObject から取得し、行列版に委譲する
bool Input::RayIntersectSphere(const Ray &ray, BaseObject *targetObject, RayHitInfo &hitInfo, const Sphere &sphere) {
    Matrix4x4 worldMatrix = targetObject->GetWorldTransform()->matWorld_;
    bool result = RayIntersectSphereByMatrix(ray, worldMatrix, hitInfo, sphere);
    if (result) {
        hitInfo.objectName = targetObject->GetName();
    }
    return result;
}

RayHitInfo Input::RaycastMultipleAABB(const Ray &ray, const std::vector<BaseObject *> baseObjects) {
    RayHitInfo closestHit;
    closestHit.hit = false;
    closestHit.distance = FLT_MAX;

    for (const auto &targetObject : baseObjects) {
        RayHitInfo currentHit;
        if (RayIntersectAABB(ray, targetObject, currentHit)) {
            if (currentHit.distance < closestHit.distance) {
                closestHit = currentHit;
                closestHit.objectName = targetObject->GetName();
            }
        }
    }

    return closestHit;
}

RayHitInfo Input::RaycastMultipleSphere(const Ray &ray, const std::vector<BaseObject *> baseObjects) {
    RayHitInfo closestHit;
    closestHit.hit = false;
    closestHit.distance = FLT_MAX;

    for (const auto &targetObject : baseObjects) {
        RayHitInfo currentHit;
        if (RayIntersectSphere(ray, targetObject, currentHit)) {
            if (currentHit.distance < closestHit.distance) {
                closestHit = currentHit;
                closestHit.objectName = targetObject->GetName();
            }
        }
    }

    return closestHit;
}
} // namespace Hagine
