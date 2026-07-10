#include "BaseScene.h"

namespace Hagine {
void BaseScene::Initialize() {
    audio_ = Audio::GetInstance();
    input_ = Input::GetInstance();
    lightGroup_ = LightGroup::GetInstance();
    ptEditor_ = ParticleEditor::GetInstance();
    ptCSEditor_ = ParticleCSEditor::GetInstance();
    spriteManager_ = SpriteManager::GetInstance();
    objectManager_ = BaseObjectManager::GetInstance();
}

void BaseScene::Finalize() {
    if (drawSystem_) {
        drawSystem_->Clear();
    }
}

void BaseScene::Update() {
}

void BaseScene::Draw() {
}

void BaseScene::AddSceneSetting() {
}

void BaseScene::AddObjectSetting() {
}

void BaseScene::AddParticleSetting() {
}

void BaseScene::DrawForOffScreen() {
}

void BaseScene::DrawAllObjects() {
    spriteManager_->DrawAll();
    objectManager_->Draw(vp_);

    ptEditor_->DrawAll(vp_);
    ptCSEditor_->DrawAll(vp_);
}

} // namespace Hagine
