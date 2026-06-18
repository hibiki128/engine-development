#include "BaseFollowCamera.h"
#include"Input.h"
#include <cmath>

namespace Hagine {
void BaseFollowCamera::Init() {
    // 遠クリップ面の設定と初期化
	viewProjection_.farZ_ = 1100;
	viewProjection_.Initialize();
	worldTransform_.Initialize();

    // 追従パラメータの初期化
	yaw_ = 0.0f;
	distanceFromTarget_ = -7.0f;
	heightOffset_ = 1.5f;
}

void BaseFollowCamera::Update() {
	// 追従対象が存在する場合のみ処理
	if (target_) {
		// ユーザー入力によるカメラ回転の更新
		Move();

		// ターゲットの位置に基づいて、極座標系からカメラの座標を計算
		Vector3 targetPosition = target_->translation_;
		worldTransform_.translation_.x = targetPosition.x + std::sin(yaw_) * distanceFromTarget_;
		worldTransform_.translation_.z = targetPosition.z + std::cos(yaw_) * distanceFromTarget_;
		worldTransform_.translation_.y = targetPosition.y + heightOffset_;

		// カメラがターゲットを向くための回転角度(Y軸)を算出
		Vector3 lookAt = targetPosition - worldTransform_.translation_;
		worldTransform_.quateRotation_.y = std::atan2(lookAt.x, lookAt.z);

		// ワールド行列の再計算
		worldTransform_.UpdateMatrix();
	}

	// 自身の変換状態をビュープロジェクションに反映
	viewProjection_.translation_ = worldTransform_.translation_;
	viewProjection_.quateRotation_ = worldTransform_.quateRotation_;
	viewProjection_.matWorld_ = worldTransform_.matWorld_;

	// ビュー行列の最終計算
	viewProjection_.UpdateMatrix();
}

void BaseFollowCamera::imgui() {
#ifdef USE_IMGUI
	ImGui::Begin("FollowCamera");
	ImGui::DragFloat3("wt position", &worldTransform_.translation_.x, 0.1f);
	ImGui::DragFloat3("vp position", &viewProjection_.translation_.x, 0.1f);
	ImGui::End();
#endif
}

void BaseFollowCamera::Move() {
    // キー入力に応じてヨー角を更新(左右回転)
	if (Input::GetInstance()->PushKey(DIK_LEFT)) {
		yaw_ -= 0.04f;
	}
	if (Input::GetInstance()->PushKey(DIK_RIGHT)) {
		yaw_ += 0.04f;
	}
}
} // namespace Hagine
