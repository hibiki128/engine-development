#define NOMINMAX
#include "CameraManager.h"
#include "frame/Frame.h"
#include <algorithm>
#ifdef USE_IMGUI
#include <imgui.h>
#include "utility/debug/imgui/DebugUIHelper.h"
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

namespace Hagine {

void CameraManager::Finalize()
{
    Clear();
    blendedCamera_.reset();
    blendFromSnapshot_.reset();
    outputCamera_.reset();
}

Camera *CameraManager::Create(const std::string &name)
{
    auto it = cameras_.find(name);
    if (it != cameras_.end())
    {
        return it->second.get(); // 同名は作り直さず既存を返す
    }

    auto camera = std::make_unique<Camera>();
    camera->Initialize(name);
    Camera *pCamera = camera.get();
    cameras_[name] = std::move(camera);

    // 描画へ渡す出力用カメラ（アドレス固定）。最初のカメラ生成時に1度だけ作る。
    if (!outputCamera_)
    {
        outputCamera_ = std::make_unique<Camera>();
        outputCamera_->Initialize("__output__");
    }

    // 最初の1台は自動でアクティブにする（1台しか使わないシーンは切り替え操作が要らない）
    if (!pActiveCamera_)
    {
        pActiveCamera_ = pCamera;
        selectedName_ = name;
        RefreshOutput(); // 生成直後に描画されても正しい行列になるようにしておく
    }
    return pCamera;
}

Camera *CameraManager::Find(const std::string &name)
{
    auto it = cameras_.find(name);
    return (it != cameras_.end()) ? it->second.get() : nullptr;
}

void CameraManager::Remove(const std::string &name)
{
    auto it = cameras_.find(name);
    if (it == cameras_.end())
    {
        return;
    }
    Camera *pCamera = it->second.get();
    // 破棄するカメラを参照していたら参照を切る（ダングリング防止）
    if (pActiveCamera_ == pCamera)
    {
        pActiveCamera_ = nullptr;
    }
    if (pBlendToCamera_ == pCamera || pBlendFromCamera_ == pCamera)
    {
        blending_ = false;
        pBlendFromCamera_ = nullptr;
        pBlendToCamera_ = nullptr;
    }
    cameras_.erase(it);

    // アクティブが消えたら残っているどれかを繋ぎで有効にする
    if (!pActiveCamera_ && !cameras_.empty())
    {
        pActiveCamera_ = cameras_.begin()->second.get();
        selectedName_ = cameras_.begin()->first;
    }
}

void CameraManager::Clear()
{
    cameras_.clear();
    pActiveCamera_ = nullptr;
    pBlendFromCamera_ = nullptr;
    pBlendToCamera_ = nullptr;
    blending_ = false;
    blendTime_ = 0.0f;
    blendDuration_ = 0.0f;
    selectedName_.clear();
}

void CameraManager::SetActive(const std::string &name)
{
    SetActive(Find(name));
}

void CameraManager::SetActive(Camera *pCamera)
{
    if (!pCamera)
    {
        return;
    }
    pActiveCamera_ = pCamera;
    selectedName_ = pCamera->GetName();
    // 切り替え途中だった補間は破棄する（即切り替えが優先）
    blending_ = false;
    pBlendFromCamera_ = nullptr;
    pBlendToCamera_ = nullptr;
    RefreshOutput();
}

void CameraManager::BlendTo(const std::string &name, float duration, EasingType easing)
{
    BlendTo(Find(name), duration, easing);
}

void CameraManager::BlendTo(Camera *pCamera, float duration, EasingType easing)
{
    if (!pCamera || pCamera == pActiveCamera_)
    {
        return;
    }
    // 現在のカメラが無い、または時間が無いなら即切り替え
    if (!pActiveCamera_ || duration <= 0.0f)
    {
        SetActive(pCamera);
        return;
    }

    if (!blendedCamera_)
    {
        blendedCamera_ = std::make_unique<Camera>();
        blendedCamera_->Initialize("__blend__");
    }
    if (!blendFromSnapshot_)
    {
        blendFromSnapshot_ = std::make_unique<Camera>();
        blendFromSnapshot_->Initialize("__blendFrom__");
    }
    // 切り替え元は「今の見た目」で固定する。元カメラが動き続けても始点が揺れない。
    // 補間の途中から別のカメラへ乗り換えた場合は、補間結果を始点にする（繋がって見える）。
    blendFromSnapshot_->CopyStateFrom(blending_ ? *blendedCamera_ : *pActiveCamera_);

    pBlendFromCamera_ = blendFromSnapshot_.get();
    pBlendToCamera_ = pCamera;
    blendDuration_ = duration;
    blendTime_ = 0.0f;
    blendEasing_ = easing;
    blending_ = true;
    selectedName_ = pCamera->GetName();
}

void CameraManager::Update()
{
    if (cameras_.empty())
    {
        return; // カメラを使っていないシーンでは何もしない
    }

    // 全カメラを更新（アクティブでないカメラもカメラワークを進めておく）
    for (auto &[name, camera] : cameras_)
    {
        camera->Update();
    }

    if (blending_)
    {
        blendTime_ += Frame::DeltaTime();
        const float duration = (std::max)(blendDuration_, 0.0001f);
        if (blendTime_ >= duration)
        {
            // 補間終了。切り替え先をアクティブにする
            Camera *pTo = pBlendToCamera_;
            blending_ = false;
            pBlendFromCamera_ = nullptr;
            pBlendToCamera_ = nullptr;
            pActiveCamera_ = pTo;
        }
        else
        {
            // 時間はクランプしてから渡す（duration を超えると外挿して行き過ぎる）
            const float time = std::clamp(blendTime_, 0.0f, duration);
            const float t = ApplyEasing(blendEasing_, 0.0f, 1.0f, time, duration);
            blendedCamera_->BlendFrom(*pBlendFromCamera_, *pBlendToCamera_, t);
        }
    }

    RefreshOutput();
}

void CameraManager::RefreshOutput()
{
    if (!outputCamera_)
    {
        return;
    }
    // 描画に使う状態（アクティブ or 補間結果）を出力用カメラへ写して行列を作り直す。
    // 出力用カメラのアドレスは変わらないので、ViewProjection のポインタを保持している側は
    // カメラが切り替わっても持ち替える必要がない。
    const Camera *pSource = (blending_ && blendedCamera_) ? blendedCamera_.get() : pActiveCamera_;
    if (!pSource)
    {
        return;
    }
    // CopyStateFrom が行列まで作り直すので Update() は呼ばない
    // （出力用カメラは映すだけの器。Update するとコピーした揺れが自分のシェイク処理で消える）
    outputCamera_->CopyStateFrom(*pSource);
}

const ViewProjection *CameraManager::GetActiveViewProjection() const
{
    if (outputCamera_)
    {
        return &outputCamera_->GetViewProjection();
    }
    return nullptr;
}

std::vector<std::string> CameraManager::GetCameraNames() const
{
    std::vector<std::string> names;
    names.reserve(cameras_.size());
    for (const auto &[name, camera] : cameras_)
    {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void CameraManager::DrawImGui()
{
#ifdef USE_IMGUI
    // 新規追加（ここで作って位置を決めて保存 → コードからは Find(名前)->Load() で呼び出せる）
    static std::string newCameraName;
    ImGui::SetNextItemWidth(-90.0f);
    ImGui::InputTextWithHint("##cameranewname", "新しいカメラの名前", &newCameraName);
    ImGui::SameLine();
    ImGui::BeginDisabled(newCameraName.empty());
    if (PrimaryButton(ICON_FA_PLUS " 追加##cameraadd"))
    {
        Camera *pCreated = Create(newCameraName);
        selectedName_ = pCreated->GetName();
        newCameraName.clear();
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    if (cameras_.empty())
    {
        ImGui::TextColored(DebugTheme::kTextDim,
                           "カメラ未登録（上の入力欄か CameraManager::Create で追加できます）");
        return;
    }

    // 一覧（アクティブなものに印を付ける）
    std::vector<std::string> names = GetCameraNames();
    std::sort(names.begin(), names.end());
    std::string pendingRemove;
    ImGui::BeginChild("##cameralist", ImVec2(-1.0f, 5.5f * ImGui::GetTextLineHeightWithSpacing()), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    for (const std::string &name : names)
    {
        const bool isActive = (pActiveCamera_ && pActiveCamera_->GetName() == name);
        ImGui::PushID(name.c_str());
        // 使用中は黄色の●、それ以外はカメラの絵
        ImGui::TextColored(isActive ? DebugTheme::kAccentYellow : DebugTheme::kTextDim, isActive ? ICON_FA_CIRCLE : ICON_FA_VIDEO);
        ImGui::SetItemTooltip(isActive ? "いま画面に映っているカメラ" : "使っていないカメラ");
        ImGui::SameLine();
        if (ImGui::Selectable(name.c_str(), selectedName_ == name, ImGuiSelectableFlags_AllowDoubleClick))
        {
            selectedName_ = name;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                SetActive(name); // ダブルクリックで切り替え
            }
        }
        ImGui::SetItemTooltip("クリックで選択 / ダブルクリックでこのカメラに切り替え / 右クリックでメニュー");
        if (ImGui::BeginPopupContextItem("##cameraRowMenu"))
        {
            selectedName_ = name;
            if (ImGui::MenuItem(ICON_FA_EXCHANGE_ALT " このカメラに切り替え", nullptr, false, !isActive))
                SetActive(name);
            if (ImGui::MenuItem(ICON_FA_SIGN_IN_ALT " 寄せて切り替え（1秒）", nullptr, false, !isActive))
                BlendTo(name, 1.0f);
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_SAVE " 保存"))
                Find(name)->Save();
            if (ImGui::MenuItem(ICON_FA_UPLOAD " 読み込み"))
                Find(name)->Load();
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_TRASH " 削除"))
                pendingRemove = name;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!pendingRemove.empty())
    {
        Remove(pendingRemove);
        return; // 破棄したので以降は触らない
    }

    Camera *pSelected = Find(selectedName_);
    if (!pSelected)
    {
        return;
    }

    const bool selectedIsActive = (pSelected == pActiveCamera_);
    ImGui::BeginDisabled(selectedIsActive);
    if (PrimaryButton(ICON_FA_EXCHANGE_ALT " 切り替え##camerasetactive"))
    {
        SetActive(pSelected);
    }
    ImGui::SetItemTooltip("このカメラですぐ映す");
    ImGui::SameLine();
    static float blendDuration = 1.0f;
    if (NeutralButton(ICON_FA_SIGN_IN_ALT " 寄せて切り替え##camerablend"))
    {
        BlendTo(pSelected, blendDuration);
    }
    ImGui::SetItemTooltip("今のカメラから、指定の秒数をかけて寄せる");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::DragFloat("秒##camerablendtime", &blendDuration, 0.05f, 0.0f, 10.0f, "%.2f");

    if (blending_)
    {
        ImGui::TextColored(DebugTheme::kAccentYellow, ICON_FA_SPINNER " 切り替え中 %.2f / %.2f 秒", blendTime_, blendDuration_);
    }

    ImGui::Separator();
    pSelected->DrawImGui();
#endif // USE_IMGUI
}
} // namespace Hagine
