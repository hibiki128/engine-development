#include "ComboSystem.h"
#include "Application/Utility/MotionEditor/MotionEditor.h"
#include "Engine/Utility/Debug/ImGui/ImGuiNotification.h"
#include "Object/Base/BaseObject.h"
#include <algorithm>

#ifdef _DEBUG
#include "imgui.h"
#endif

// 定数定義
using namespace Hagine;

// ある段を繰り出してから次の段を受け付けるまでのクールダウン（秒）。
// この間にコンボ入力されても即発動はせず、先行入力としてバッファされる
const float ComboSystem::COMBO_INTERVAL = 0.45f;

// クールダウン中に行われた先行入力を保持する有効時間（秒）。
// この時間内であればクールダウンが明けた瞬間に次の段が自動発動する
const float ComboSystem::INPUT_BUFFER_DURATION = 0.4f;

// 最終段を出し切ってから開始姿勢へ戻すまでの待機時間（秒）。
// フィニッシュモーションを見せる余韻として確保する
const float ComboSystem::FINAL_RETURN_DELAY = 0.5f;

// コンボ継続中に次の入力が来ないまま経過すると、コンボを打ち切って
// 開始姿勢へ戻すまでの放置タイムアウト時間（秒）
const float ComboSystem::COMBO_TIMEOUT_DURATION = 0.5f;

ComboSystem::ComboSystem()
    : comboIndex_(0), comboCooldown_(0.0f), comboStarted_(false),
      waitingForReturn_(false), returnDelay_(0.0f), comboTimeout_(0.0f),
      inputBuffered_(false), inputBufferTime_(0.0f) {
}

ComboSystem::~ComboSystem() {
    Clear();
}

ComboSystem &ComboSystem::Add(BaseObject *target, const std::string &attackData,
                              float damage, float knockbackPower,
                              float colliderActiveDuration, float colliderActivateDelay) {
    // コンボデータの追加
    comboData_.emplace_back(target, attackData,
                            damage, knockbackPower,
                            colliderActiveDuration, colliderActivateDelay);

    // 開始オブジェクトの登録
    if (target != nullptr) {
        auto it = std::find(comboStartObjects_.begin(), comboStartObjects_.end(), target);
        if (it == comboStartObjects_.end()) {
            comboStartObjects_.push_back(target);
        }
    }

    return *this;
}

void ComboSystem::Clear() {
    comboData_.clear();
    comboStartObjects_.clear();
    ResetCombo();
}

void ComboSystem::SaveAttackParams() {
    if (!dataHandler_) {
        dataHandler_ = std::make_unique<DataHandler>("ComboSystem", name_);
    }

    // 各コンボ段のパラメータを保存
    for (size_t i = 0; i < comboData_.size(); ++i) {
        const ComboData &cd = comboData_[i];
        const std::string key = cd.attackData;

        dataHandler_->Save(key + "_damage", cd.damage);
        dataHandler_->Save(key + "_knockback", cd.knockbackPower);
        dataHandler_->Save(key + "_duration", cd.colliderActiveDuration);
        dataHandler_->Save(key + "_delay", cd.colliderActivateDelay);
    }
    ImGuiNotification::Post("コンボパラメータを保存しました: " + name_, {0.2f, 0.8f, 0.2f, 1.0f});
}

void ComboSystem::LoadAttackParams() {
    if (!dataHandler_) {
        dataHandler_ = std::make_unique<DataHandler>("ComboSystem", name_);
    }

    // 各コンボ段のパラメータをロード
    for (size_t i = 0; i < comboData_.size(); ++i) {
        ComboData &cd = comboData_[i];
        const std::string key = cd.attackData;

        cd.damage = dataHandler_->Load<float>(key + "_damage", cd.damage);
        cd.knockbackPower = dataHandler_->Load<float>(key + "_knockback", cd.knockbackPower);
        cd.colliderActiveDuration = dataHandler_->Load<float>(key + "_duration", cd.colliderActiveDuration);
        cd.colliderActivateDelay = dataHandler_->Load<float>(key + "_delay", cd.colliderActivateDelay);
    }
    ImGuiNotification::Post("コンボパラメータを読み込みました: " + name_, {0.2f, 0.8f, 0.8f, 1.0f});
}

bool ComboSystem::TryExecuteCombo() {
    if (waitingForReturn_ || comboData_.empty()) {
        return false;
    }

    if (comboCooldown_ <= 0.0f) {
        ExecuteComboAttack();
        return true;
    } else {
        // 先行入力
        if (comboStarted_) {
            inputBuffered_ = true;
            inputBufferTime_ = INPUT_BUFFER_DURATION;
        }
        return false;
    }
}

void ComboSystem::Update(float deltaTime) {
    comboCooldown_ -= deltaTime;
    returnDelay_ -= deltaTime;
    inputBufferTime_ -= deltaTime;

    bool attackExecuted = false;

    // 先行入力の処理
    if (inputBuffered_ && comboCooldown_ <= 0.0f) {
        inputBuffered_ = false;
        inputBufferTime_ = 0.0f;
        ExecuteComboAttack();
        attackExecuted = true;
    }

    // 先行入力有効期限切れ
    if (inputBufferTime_ <= 0.0f) {
        inputBuffered_ = false;
    }

    // 最終攻撃後の戻り
    if (waitingForReturn_ && returnDelay_ <= 0.0f) {
        for (BaseObject *obj : comboStartObjects_) {
            if (obj != nullptr) {
                MotionEditor::GetInstance()->ReturnToComboStart(obj);
            }
        }
        ResetCombo();
    }

    // タイムアウトによるコンボ終了
    if (comboStarted_ && !waitingForReturn_ && !attackExecuted) {
        comboTimeout_ += deltaTime;
        if (comboTimeout_ >= COMBO_TIMEOUT_DURATION) {
            for (BaseObject *obj : comboStartObjects_) {
                if (obj != nullptr) {
                    MotionEditor::GetInstance()->ReturnToComboStart(obj);
                }
            }
            ResetCombo();
        }
    }
}

void ComboSystem::ExecuteComboAttack() {
    if (comboIndex_ >= static_cast<int>(comboData_.size())) {
        return;
    }

    const ComboData &currentCombo = comboData_[comboIndex_];
    BaseObject *currentTarget = currentCombo.target;

    // 以前の攻撃が終了していたら戻す
    if (comboIndex_ > 0) {
        for (int i = comboIndex_ - 1; i >= 0; i--) {
            BaseObject *prevTarget = comboData_[i].target;
            if (prevTarget && prevTarget == currentTarget) {
                if (MotionEditor::GetInstance()->IsAttackFinishedWithInterval(prevTarget)) {
                    MotionEditor::GetInstance()->ReturnToComboStart(prevTarget);
                    MotionEditor::GetInstance()->ClearAttackEndInterval(prevTarget);
                }
                break;
            }
        }
    }

    comboCooldown_ = COMBO_INTERVAL;

    if (!comboStarted_) {
        SaveComboStartPositions();
        comboStarted_ = true;
        comboTimeout_ = 0.0f;
    } else {
        comboTimeout_ = 0.0f;
    }

    // 攻撃発火コールバック実行
    if (onAttackFired_) {
        onAttackFired_(
            currentCombo.damage,
            currentCombo.knockbackPower,
            currentCombo.colliderActiveDuration,
            currentCombo.colliderActivateDelay);
    }

    // モーション再生
    if (currentTarget != nullptr && !currentCombo.attackData.empty()) {
        MotionEditor::GetInstance()->Stop(currentTarget->GetName());
        MotionEditor::GetInstance()->PlayFromFile(currentTarget, currentCombo.attackData);
    }

    comboIndex_++;
    // 全コンボ終了時の設定
    if (comboIndex_ >= static_cast<int>(comboData_.size())) {
        waitingForReturn_ = true;
        returnDelay_ = FINAL_RETURN_DELAY;
        comboIndex_ = 0;
    }
}

void ComboSystem::ResetCombo() {
    comboStarted_ = false;
    waitingForReturn_ = false;
    comboIndex_ = 0;
    comboTimeout_ = 0.0f;
    inputBuffered_ = false;
    inputBufferTime_ = 0.0f;
    comboCooldown_ = 0.0f;
}

void ComboSystem::SaveComboStartPositions() {
    for (BaseObject *obj : comboStartObjects_) {
        if (obj != nullptr) {
            MotionEditor::GetInstance()->SetComboStartPosition(obj);
        }
    }
}

bool ComboSystem::IsObjectAttackCompleted(BaseObject *target) const {
    if (!target) {
        return true;
    }
    return !MotionEditor::GetInstance()->IsAttackFinishedWithInterval(target);
}

bool ComboSystem::IsCurrentAttackCompleted() const {
    if (comboIndex_ == 0 || comboIndex_ > static_cast<int>(comboData_.size())) {
        return true;
    }
    const ComboData &prevCombo = comboData_[comboIndex_ - 1];
    return !IsObjectAttackCompleted(prevCombo.target);
}

#ifdef _DEBUG
void ComboSystem::DrawImGui() {
    if (comboData_.empty()) {
        ImGui::TextDisabled("コンボデータなし");
        return;
    }

    ImGui::Text("コンボ名: %s", name_.c_str());
    ImGui::Text("現在のインデックス: %d / %d",
                comboIndex_, static_cast<int>(comboData_.size()));
    ImGui::Separator();

    // 攻撃パラメータ編集
    for (size_t i = 0; i < comboData_.size(); ++i) {
        ComboData &cd = comboData_[i];

        std::string nodeLabel = std::to_string(i + 1) + "段目: " + cd.attackData;
        if (ImGui::TreeNodeEx(nodeLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {

            if (comboStarted_ && comboIndex_ > 0 &&
                static_cast<int>(i) == comboIndex_ - 1) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "<<< 実行中");
            }

            ImGui::PushID(static_cast<int>(i));

            ImGui::DragFloat("ダメージ##dmg", &cd.damage, 0.01f);
            ImGui::DragFloat("ノックバック##kb", &cd.knockbackPower, 0.01f);
            ImGui::Separator();
            ImGui::DragFloat("判定有効時間(秒)##dur", &cd.colliderActiveDuration, 0.01f);
            ImGui::DragFloat("判定開始遅延(秒)##dly", &cd.colliderActivateDelay, 0.01f);

            ImGui::PopID();
            ImGui::TreePop();
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    // セーブ/ロード
    if (ImGui::Button("全攻撃パラメータを保存", ImVec2(200.0f, 0.0f))) {
        SaveAttackParams();
    }
    ImGui::SameLine();
    if (ImGui::Button("再読み込み", ImVec2(120.0f, 0.0f))) {
        LoadAttackParams();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("保存先: ComboSystem/%s.json", name_.c_str());
}
#endif