#include "SceneTransition.h"
#include "Easing.h"
#include "Engine/Frame/Frame.h"
#include "SpriteCommon.h"
#include "algorithm"
#include "myMath.h"
#include <vector>

namespace Hagine {
void SceneTransition::Finalize() {
    transitionSprite_.reset();
    instanceSizes_.clear();
    sprite_.reset();
}

void SceneTransition::Initialize() {
    sprite_ = std::make_unique<Sprite>();
    sprite_->Initialize("debug/black1x1.png", {0, 0}, {1.0f, 1.0f, 1.0f, 1.0f});
    sprite_->SetSize(Vector2(WinApp::kClientWidth, WinApp::kClientHeight)); // 画面全体を覆うサイズ
    sprite_->SetAlpha(0.0f);                                                // 最初は完全に透明
    duration_ = 1.0f;                                                       // フェードの持続時間（例: 1秒）
    counter_ = 0.0f;                                                        // 経過時間カウンターを初期化
    fadeInFinish_ = false;
    fadeOutFinish_ = false;
    fadeInStart_ = false;
    fadeOutStart_ = false;
    isEnd_ = false;
    useTransition_ = true; // デフォルトはトランジションを使用

    // インスタンシング用の初期化
    rows_ = 15;                                      // 縦方向のスプライト数
    cols_ = 23;                                      // 横方向のスプライト数
    float size = 0.0f;                               // 初期サイズを 0.0f に設定
    Vector4 defaultColor = {1.0f, 1.0f, 1.0f, 1.0f}; // スプライトの初期色

    // インスタンシング用Spriteを作成
    transitionSprite_ = std::make_unique<Sprite>();
    transitionSprite_->Initialize("debug/black1x1.png", {0, 0}, defaultColor, {0.5f, 0.5f});
    transitionSprite_->SetSize(Vector2(size, size));

    // インスタンス数を設定
    totalInstances_ = rows_ * cols_;
    transitionSprite_->SetInstanceCount(totalInstances_);

    // 各インスタンスのサイズを保存する配列を初期化
    instanceSizes_.resize(rows_);
    for (int row = 0; row < rows_; ++row) {
        instanceSizes_[row].resize(cols_, size);
    }

    // 初期の変換行列を設定
    UpdateTransitionInstances();
}

void SceneTransition::Update() {
    // トランジションを使用しない場合は即座に完了状態にする
    if (!useTransition_) {
        if (fadeInStart_ && !fadeInFinish_) {
            fadeInFinish_ = true;
        }
        if (fadeOutStart_ && !fadeOutFinish_) {
            fadeOutFinish_ = true;
        }
        if (fadeInFinish_ && fadeOutFinish_) {
            isEnd_ = true;
            fadeInStart_ = false;
            fadeOutStart_ = false;
        }
        return;
    }

    FadeUpdate();
}

void SceneTransition::Draw() {
    SpriteCommon::GetInstance()->DrawCommonSetting();
    // トランジションを使用しない場合は描画しない
    if (!useTransition_) {
        return;
    }

    // インスタンシング描画で一度に全てのスプライトを描画
    transitionSprite_->Draw();
}

void SceneTransition::Debug() {
#ifdef USE_IMGUI
    ImGui::Begin("遷移");
    ImGui::DragFloat2("位置", &spPos_.x, 0.1f);
    ImGui::Checkbox("トランジション使用", &useTransition_);
    ImGui::End();
#endif // USE_IMGUI
}

void SceneTransition::FadeUpdate() {
    if (fadeInStart_) {
        // フェードイン中
        if (!fadeInFinish_) {
            FadeIn();
        }
    }
    if (fadeOutStart_) {
        // フェードインが終わったら、フェードアウトを開始
        if (fadeInFinish_ && !fadeOutFinish_) {
            FadeOut();
        }
    }

    // トランジションが終了したら、終了フラグを立てる
    if (fadeInFinish_ && fadeOutFinish_) {
        isEnd_ = true;
        fadeInStart_ = false;
        fadeOutStart_ = false;
    }
}

void SceneTransition::FadeIn() {
    // DefaultFadeIn();
    ReverseFadeIn();

    counter_ += 1.0f / 60.0f; // フレームレートを基にカウント（1フレームごとに0.0167秒進む）
    if (counter_ >= duration_) {
        counter_ = duration_; // 終了時間を超えないように制限
        fadeInFinish_ = true;  // フェードイン終了フラグを立てる
    }
}

void SceneTransition::FadeOut() {
    // DefaultFadeOut();
    ReverseFadeOut();

    // カウンターを減少（フレームレートに基づく）
    counter_ -= 1.0f / 60.0f;
    if (counter_ <= 0.0f) {
        counter_ = 0.0f;      // カウンターが負になるのを防ぐ
        fadeOutFinish_ = true; // フェードアウト完了フラグを立てる
    }
}

void SceneTransition::DefaultFadeIn() {
    // アルファ値の計算（0.0fから1.0fに増加）
    float alpha = counter_ / duration_;
    sprite_->SetAlpha(alpha);
}

void SceneTransition::DefaultFadeOut() {
    // アルファ値の計算（1.0fから0.0fに減少）
    float alpha = counter_ / duration_; // カウンターが減るほどアルファも減る
    sprite_->SetAlpha(alpha);           // アルファ値を設定
}

void SceneTransition::ReverseFadeIn() {
    // 遅延の最大値（秒）
    const float maxDelay = 0.3f;
    bool needsUpdate = false;

    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            // 位置に基づいて遅延を計算（左上から右下へ）
            float delay = ((float)(row + col) / (float)(rows_ + cols_ - 2)) * maxDelay;
            // カウンターから遅延を引いた値を使用
            float localTime = counter_ - delay;
            float newSize = 0.0f; // 毎フレーム初期化

            if (localTime >= 0.0f && localTime <= duration_) {
                // イージング関数で0 → 80に拡大
                float progress = localTime / duration_;
                newSize = EaseInSine<float>(0.0f, 80.0f, progress, 0.4f);
            } else if (localTime > duration_) {
                // 最大サイズに到達したら固定
                newSize = 80.0f;
            } else {
                // 遅延待ち中は初期サイズを維持
                newSize = 0.0f;
            }

            // 常に更新（サイズが変わらなくても）
            instanceSizes_[row][col] = newSize;
            needsUpdate = true;
        }
    }

    // 毎フレーム更新
    if (needsUpdate) {
        UpdateTransitionInstances();
    }
}

void SceneTransition::ReverseFadeOut() {
    // 遅延の最大値（秒）
    const float maxDelay = 0.3f;
    bool needsUpdate = false;

    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            // 位置に基づいて遅延を計算（右下から左上へ）
            // (rows-1-row)と(cols-1-col)で座標を反転
            float delay = ((float)((rows_ - 1 - row) + (cols_ - 1 - col)) / (float)(rows_ + cols_ - 2)) * maxDelay;

            // カウンターから遅延を引いた値を使用
            float localTime = counter_ - delay;
            float newSize = instanceSizes_[row][col];

            if (localTime >= 0.0f && localTime <= duration_) {
                // counter_が1→0で変化
                float progress = localTime / duration_;
                newSize = EaseInSine<float>(0.0f, 80.0f, progress, 0.4f);
                needsUpdate = true;
            } else if (localTime > duration_) {
                // サイズが 0 に到達したら固定
                newSize = 0.0f;
            } else if (localTime < 0.0f) {
                // 遅延待ち中も0サイズを維持
                newSize = 0.0f;
            }

            if (instanceSizes_[row][col] != newSize) {
                instanceSizes_[row][col] = newSize;
                needsUpdate = true;
            }
        }
    }

    if (needsUpdate) {
        UpdateTransitionInstances();
    }
}

void SceneTransition::UpdateTransitionInstances() {
    int instanceIndex = 0;
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            Vector2 position = {col * 80.0f, row * 80.0f};
            float size = instanceSizes_[row][col];

            Transform transform{{size, size, 1.0f}, {0.0f, 0.0f, 0.0f}, {position.x, position.y, 0.0f}};
            Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
            Matrix4x4 viewMatrix = MakeIdentity4x4();
            Matrix4x4 projectionMatrix = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 100.0f);

            TransformationMatrix transformMatrix;
            transformMatrix.WVP = worldMatrix * viewMatrix * projectionMatrix;
            transformMatrix.World = worldMatrix;

            transitionSprite_->SetInstanceTransform(instanceIndex, transformMatrix);
            instanceIndex++;
        }
    }
}

// トランジション状態をリセット
void SceneTransition::Reset() {
    counter_ = 0.0f;
    fadeInFinish_ = false;
    fadeOutFinish_ = false;
    fadeInStart_ = false;
    fadeOutStart_ = false;
    isEnd_ = false;
    sprite_->SetAlpha(0.0f); // 最初の透明状態に戻す

    // 全インスタンスのサイズを0にリセット
    for (int row = 0; row < rows_; ++row) {
        for (int col = 0; col < cols_; ++col) {
            instanceSizes_[row][col] = 0.0f;
        }
    }
    UpdateTransitionInstances();
}
} // namespace Hagine
