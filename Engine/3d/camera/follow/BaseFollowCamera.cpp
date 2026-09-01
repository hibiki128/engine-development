#include "BaseFollowCamera.h"
#include <Input.h>
#include <camera/CameraManager.h>
#include <cmath>

namespace Hagine {
void BaseFollowCamera::Init(const std::string &cameraName)
{
    // カメラ本体は CameraManager が所有する（名前で切り替えられるようにするため）
    pCamera_ = CameraManager::GetInstance()->Create(cameraName);
    pCamera_->SetClipRange(0.1f, 1100.0f);

    // 追従パラメータの初期化
    yaw_ = 0.0f;
    distanceFromTarget_ = -7.0f;
    heightOffset_ = 1.5f;
}

void BaseFollowCamera::Update()
{
    // 追従対象が存在する場合のみ処理
    if (!pCamera_ || !pTarget_)
    {
        return;
    }

    // ユーザー入力によるカメラ回転の更新
    Move();

    // ターゲットの位置に基づいて、極座標系からカメラの座標を計算
    const Vector3 targetPosition = pTarget_->translation_;
    Vector3 cameraPosition;
    cameraPosition.x = targetPosition.x + std::sin(yaw_) * distanceFromTarget_;
    cameraPosition.z = targetPosition.z + std::cos(yaw_) * distanceFromTarget_;
    cameraPosition.y = targetPosition.y + heightOffset_;

    // 位置を決めてターゲットを向く（行列の計算はカメラ側が行う）
    pCamera_->SetPosition(cameraPosition);
    pCamera_->SetTarget(targetPosition);
}

void BaseFollowCamera::DrawImGui()
{
#ifdef USE_IMGUI
    ImGui::Begin("FollowCamera");
    if (pCamera_)
    {
        pCamera_->DrawImGui();
    }
    ImGui::DragFloat("ターゲットからの距離##followdistance", &distanceFromTarget_, 0.1f);
    ImGui::DragFloat("高さオフセット##followheight", &heightOffset_, 0.1f);
    ImGui::End();
#endif
}

void BaseFollowCamera::Move()
{
    // キー入力に応じてヨー角を更新(左右回転)
    if (Input::GetInstance()->PushKey(DIK_LEFT))
    {
        yaw_ -= 0.04f;
    }
    if (Input::GetInstance()->PushKey(DIK_RIGHT))
    {
        yaw_ += 0.04f;
    }
}
} // namespace Hagine
