#define NOMINMAX
#include "AnimationController.h"
#include "BlendSpace.h"
#include "object/Object3d.h"
#include <algorithm>
#include <cmath>
#include <data/DataHandler.h>
#include <string>

#ifdef USE_IMGUI
#include <imgui.h>
#include "utility/debug/imgui/DebugUIHelper.h"
#endif

namespace Hagine {

void AnimationController::RegisterBlendSpace(const std::string &name, BlendSpaceMode mode,
                                             const std::vector<BlendSpacePoint> &points, float fadeDuration)
{
    if (!pObject_)
    {
        return;
    }
    AnimationBlendSpace *pSpace = FindBlendSpace(name);
    if (!pSpace)
    {
        blendSpaces_.push_back(std::make_shared<AnimationBlendSpace>());
        pSpace = blendSpaces_.back().get();
        pSpace->SetName(name);
    }
    pSpace->SetMode(mode);
    pSpace->SetFadeDuration(fadeDuration);
    RebuildBlendSpace(*pSpace, points);
}

void AnimationController::RebuildBlendSpace(AnimationBlendSpace &space, const std::vector<BlendSpacePoint> &points)
{
    std::vector<std::string> filePaths;
    filePaths.reserve(points.size());
    for (const BlendSpacePoint &point : points)
    {
        auto it = index_.find(point.clipName);
        filePaths.push_back(it != index_.end() ? clips_[it->second].filePath : std::string());
    }
    space.SetPoints(points, filePaths);
}

AnimationBlendSpace *AnimationController::FindBlendSpace(const std::string &name) const
{
    for (const std::shared_ptr<AnimationBlendSpace> &space : blendSpaces_)
    {
        if (space->GetName() == name)
        {
            return space.get();
        }
    }
    return nullptr;
}

bool AnimationController::HasBlendSpace(const std::string &name) const
{
    return FindBlendSpace(name) != nullptr;
}

void AnimationController::PlayBlendSpace(const std::string &name, const Vector2 &parameter)
{
    if (!pObject_)
    {
        return;
    }
    for (const std::shared_ptr<AnimationBlendSpace> &space : blendSpaces_)
    {
        if (space->GetName() != name)
        {
            continue;
        }
        // エディタの配置図で動かしている間は、ゲーム側からの値で上書きしない
        if (!blendSpacePreview_)
        {
            space->SetTargetParameter(parameter);
        }
        pObject_->PlayBlendSpace(space, space->GetFadeDuration());
        currentBlendSpaceName_ = name;
        currentClipName_ = name;
        paused_ = false;
        return;
    }
}

void AnimationController::StopBlendSpaceForClip(float fadeDuration)
{
    if (!pObject_ || currentBlendSpaceName_.empty())
    {
        return;
    }
    pObject_->StopBlendSpace(fadeDuration);
    currentBlendSpaceName_.clear();
}

void AnimationController::SaveBlendSpaces(DataHandler &data) const
{
    data.Save("blendSpaceCount", static_cast<int>(blendSpaces_.size()));
    for (size_t i = 0; i < blendSpaces_.size(); ++i)
    {
        const AnimationBlendSpace &space = *blendSpaces_[i];
        const std::string prefix = "blendSpace" + std::to_string(i) + "_";
        data.Save(prefix + "name", space.GetName());
        data.Save(prefix + "mode", static_cast<int>(space.GetMode()));
        data.Save(prefix + "fade", space.GetFadeDuration());
        data.Save(prefix + "smooth", space.GetSmoothTime());
        data.Save(prefix + "speed", space.GetPlaybackSpeed());
        const std::vector<BlendSpacePoint> &points = space.GetPoints();
        data.Save(prefix + "pointCount", static_cast<int>(points.size()));
        for (size_t j = 0; j < points.size(); ++j)
        {
            const std::string p = prefix + "point" + std::to_string(j) + "_";
            data.Save(p + "clip", points[j].clipName);
            data.Save(p + "x", points[j].position.x);
            data.Save(p + "y", points[j].position.y);
            data.Save(p + "speed", points[j].speed);
        }
    }
}

void AnimationController::LoadBlendSpaces(DataHandler &data)
{
    const int count = data.Load<int>("blendSpaceCount", 0);
    for (int i = 0; i < count; ++i)
    {
        const std::string prefix = "blendSpace" + std::to_string(i) + "_";
        // コードで登録していないブレンドスペースは作らない（クリップと同じ扱い）
        AnimationBlendSpace *pSpace = FindBlendSpace(data.Load<std::string>(prefix + "name", ""));
        if (!pSpace)
        {
            continue;
        }
        pSpace->SetMode(static_cast<BlendSpaceMode>(data.Load<int>(prefix + "mode", static_cast<int>(pSpace->GetMode()))));
        pSpace->SetFadeDuration(data.Load<float>(prefix + "fade", pSpace->GetFadeDuration()));
        pSpace->SetSmoothTime(data.Load<float>(prefix + "smooth", pSpace->GetSmoothTime()));
        pSpace->SetPlaybackSpeed(data.Load<float>(prefix + "speed", pSpace->GetPlaybackSpeed()));

        const int pointCount = data.Load<int>(prefix + "pointCount", -1);
        if (pointCount < 0)
        {
            continue;
        }
        std::vector<BlendSpacePoint> points;
        for (int j = 0; j < pointCount; ++j)
        {
            const std::string p = prefix + "point" + std::to_string(j) + "_";
            BlendSpacePoint point;
            point.clipName = data.Load<std::string>(p + "clip", "");
            point.position.x = data.Load<float>(p + "x", 0.0f);
            point.position.y = data.Load<float>(p + "y", 0.0f);
            point.speed = data.Load<float>(p + "speed", 1.0f);
            points.push_back(point);
        }
        RebuildBlendSpace(*pSpace, points);
    }
}

void AnimationController::DrawBlendSpaceImGui()
{
#ifdef USE_IMGUI
    if (!ImGui::CollapsingHeader("ブレンドスペース（クリップを混ぜる）"))
    {
        blendSpacePreview_ = false;
        return;
    }
    if (blendSpaces_.empty())
    {
        DimText("（コードで RegisterBlendSpace するとここに出ます）");
        blendSpacePreview_ = false;
        return;
    }

    // ---- 選択 ----
    selectedBlendSpace_ = std::clamp(selectedBlendSpace_, 0, static_cast<int>(blendSpaces_.size()) - 1);
    AnimationBlendSpace &space = *blendSpaces_[selectedBlendSpace_];
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##blendSpace", space.GetName().c_str()))
    {
        for (int i = 0; i < static_cast<int>(blendSpaces_.size()); ++i)
        {
            if (ImGui::Selectable(blendSpaces_[i]->GetName().c_str(), i == selectedBlendSpace_))
            {
                selectedBlendSpace_ = i;
            }
        }
        ImGui::EndCombo();
    }

    const bool isCurrent = (currentBlendSpaceName_ == space.GetName());
    const bool isActive = isCurrent && pObject_->GetBlendSpace() == &space;
    if (isActive)
    {
        StatusBadge("再生中", DebugTheme::kAccentGreen);
        ImGui::SameLine();
        ImGui::TextDisabled("効き %.0f%%  周期 %.0f%%", pObject_->GetBlendSpaceWeight() * 100.0f, space.GetPhase() * 100.0f);
    }
    else
    {
        StatusBadge("停止中", DebugTheme::kTextDim);
        ImGui::SameLine();
        ImGui::TextDisabled("キャラがこのブレンドスペースを使う状態のときに動きます");
    }

    // ---- 設定 ----
    static const char *kModeNames[] = {"向き＋長さ（中心と周り）", "自由配置", "一直線（X だけ）"};
    int mode = static_cast<int>(space.GetMode());
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("混ぜ方", &mode, kModeNames, IM_ARRAYSIZE(kModeNames)))
    {
        space.SetMode(static_cast<BlendSpaceMode>(mode));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("向き＋長さ: 原点の点（待機）と周りの点（前後左右）。向きで隣の2つを混ぜ、長さで中心と混ぜる\n"
                          "自由配置: 点をどこに置いてもよい（近い点ほど強く効く）\n"
                          "一直線: 歩き→走りのように X だけで並べる");
    }
    float fade = space.GetFadeDuration();
    float smooth = space.GetSmoothTime();
    float speed = space.GetPlaybackSpeed();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragFloat("切り替え(秒)", &fade, 0.01f, 0.0f, 2.0f, "%.2f"))
    {
        space.SetFadeDuration((std::max)(fade, 0.0f));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragFloat("なめらかさ(秒)", &smooth, 0.005f, 0.0f, 1.0f, "%.3f"))
    {
        space.SetSmoothTime((std::max)(smooth, 0.0f));
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("入力の変化に重みが追いつくまでの時間。0 だと入力どおりにパキッと変わる");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::DragFloat("速度", &speed, 0.01f, 0.0f, 4.0f, "%.2f"))
    {
        space.SetPlaybackSpeed((std::max)(speed, 0.0f));
    }

    // ---- 配置図: 点と今のパラメータ ----
    const std::vector<BlendSpacePoint> &points = space.GetPoints();
    const std::vector<float> &weights = space.GetWeights();
    float extent = 1.0f;
    for (const BlendSpacePoint &point : points)
    {
        extent = (std::max)(extent, (std::max)(std::abs(point.position.x), std::abs(point.position.y)));
    }
    extent *= 1.25f;

    const float canvasSize = (std::min)(ImGui::GetContentRegionAvail().x, 280.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 center = {origin.x + canvasSize * 0.5f, origin.y + canvasSize * 0.5f};
    const float scale = canvasSize * 0.5f / extent;
    const bool linear = (space.GetMode() == BlendSpaceMode::Linear1D);
    auto toScreen = [&](const Vector2 &p) { return ImVec2(center.x + p.x * scale, linear ? center.y : center.y - p.y * scale); };

    ImDrawList *pDraw = ImGui::GetWindowDrawList();
    pDraw->AddRectFilled(origin, {origin.x + canvasSize, origin.y + canvasSize}, IM_COL32(24, 26, 32, 255), 4.0f);
    pDraw->AddLine({origin.x, center.y}, {origin.x + canvasSize, center.y}, IM_COL32(70, 74, 86, 255));
    if (!linear)
    {
        pDraw->AddLine({center.x, origin.y}, {center.x, origin.y + canvasSize}, IM_COL32(70, 74, 86, 255));
        pDraw->AddCircle(center, scale, IM_COL32(60, 64, 76, 255), 48);
    }

    const ImU32 pointColor = ImGui::ColorConvertFloat4ToU32(DebugTheme::kAccentBlue);
    for (size_t i = 0; i < points.size(); ++i)
    {
        const ImVec2 sp = toScreen(points[i].position);
        const float w = (i < weights.size()) ? weights[i] : 0.0f;
        // 重いほど大きく明るく
        pDraw->AddCircleFilled(sp, 4.0f + 10.0f * w, IM_COL32(115, 153, 199, static_cast<int>(60 + 195 * w)));
        pDraw->AddCircle(sp, 4.0f, pointColor);
        pDraw->AddText({sp.x + 7.0f, sp.y - 16.0f}, IM_COL32(200, 205, 215, 255), points[i].clipName.c_str());
    }

    const ImU32 accent = ImGui::ColorConvertFloat4ToU32(DebugTheme::kAccentOrange);
    pDraw->AddCircle(toScreen(space.GetTargetParameter()), 7.0f, accent, 16, 1.5f);
    pDraw->AddCircleFilled(toScreen(space.GetParameter()), 4.5f, accent);

    // 配置図をドラッグしている間はパラメータを手で動かす（ゲーム側の値は無視）
    ImGui::InvisibleButton("##blendCanvas", {canvasSize, canvasSize});
    blendSpacePreview_ = ImGui::IsItemActive();
    if (blendSpacePreview_)
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        Vector2 p = {(mouse.x - center.x) / scale, linear ? 0.0f : (center.y - mouse.y) / scale};
        space.SetTargetParameter(p);
        if (!isCurrent)
        {
            PlayBlendSpace(space.GetName(), p);
        }
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("ドラッグでパラメータを動かして確かめられます（離すとゲームの値に戻る）\n"
                          "橙の輪 = 目標、橙の点 = なめらかにした今の値、青の大きさ = 重み");
    }
    ImGui::TextDisabled("パラメータ (%.2f, %.2f)", space.GetParameter().x, space.GetParameter().y);

    // ---- 点の一覧 ----
    std::vector<BlendSpacePoint> edited = points;
    bool rebuild = false;
    int removeIndex = -1;
    if (ImGui::BeginTable("##blendPoints", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
        ImGui::TableSetupColumn("クリップ", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("位置", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("速度", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("重み", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < edited.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##clip", edited[i].clipName.c_str()))
            {
                for (const AnimationClip &clip : clips_)
                {
                    if (ImGui::Selectable(clip.name.c_str(), clip.name == edited[i].clipName))
                    {
                        edited[i].clipName = clip.name;
                        rebuild = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat2("##pos", &edited[i].position.x, 0.01f, -10.0f, 10.0f, "%.2f"))
            {
                space.GetMutablePoints()[i].position = edited[i].position; // 読み直し不要
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##speed", &edited[i].speed, 0.01f, 0.05f, 4.0f, "%.2f"))
            {
                space.GetMutablePoints()[i].speed = (std::max)(edited[i].speed, 0.05f);
            }
            ImGui::TableNextColumn();
            const float w = (i < weights.size()) ? weights[i] : 0.0f;
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, DebugTheme::kAccentBlue);
            ImGui::ProgressBar(w, {-1.0f, 0.0f}, "");
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            if (DangerButton("x", {20.0f, 0.0f}))
            {
                removeIndex = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIndex >= 0)
    {
        edited.erase(edited.begin() + removeIndex);
        rebuild = true;
    }
    if (NeutralButton("点を追加") && !clips_.empty())
    {
        BlendSpacePoint point;
        point.clipName = clips_.front().name;
        edited.push_back(point);
        rebuild = true;
    }
    if (rebuild)
    {
        RebuildBlendSpace(space, edited);
    }
    ImGui::SameLine();
    DimText("保存は下の「クリップ設定 セーブ」と一緒");
#endif // USE_IMGUI
}

} // namespace Hagine
