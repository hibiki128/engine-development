#include "LightGroup.h"
#include "DirectXCommon.h"
#include <cassert>
#include <cctype>
#include <graphics/pipeline/PipelineManager.h>
#include <format>
#include <attachment/AttachmentManager.h>
#include <light/ToonSettings.h>
#include <line/LineRenderer.h>
#include <render/deferred/DeferredRenderer.h>
#include <utility/debug/imgui/DebugUIHelper.h>
#include <utility/debug/imgui/ImGuiNotification.h>
#ifdef USE_IMGUI
#include "LightUIHelper.h"
#include <utility/debug/imgui/ImGuizmoManager.h>
#endif

namespace Hagine {
namespace {
// ギズモ登録名の接頭辞。オブジェクトやスプライトと名前が衝突しないよう名前空間を分ける
constexpr const char *kGizmoPrefix = "光源/";
// スポットライトの向きハンドルにつける接尾辞
constexpr const char *kGizmoAimSuffix = " (向き)";
// 親子付けの登録名につける接頭辞（3Dオブジェクトの名前と衝突させないため）
constexpr const char *kAttachPrefix = "光源/";
} // namespace

// ===================================================
// 初期化・更新・描画
// ===================================================

void LightGroup::Finalize()
{
    // ギズモが持っているポインタはこの後 dangling になるので必ず先に外す
    UnregisterGizmoTargets();

    directionalLight_.Finalize();
    pointLights_.Finalize();
    spotLights_.Finalize();

    cameraForGPUResource_.Reset();
    pCameraForGPUData_ = nullptr;
}

void LightGroup::Initialize()
{
    pDxCommon_ = DirectXCommon::GetInstance();
    CreateCamera();
    pointLights_.Initialize(pDxCommon_);
    directionalLight_.Initialize(pDxCommon_);
    spotLights_.Initialize(pDxCommon_);
}

void LightGroup::CreateCamera()
{
    cameraForGPUResource_ = pDxCommon_->CreateBufferResource(sizeof(CameraForGPU));
    cameraForGPUResource_->Map(0, nullptr, reinterpret_cast<void **>(&pCameraForGPUData_));
    pCameraForGPUData_->worldPosition = {0.0f, 0.0f, -50.0f};
}

void LightGroup::Update(const ViewProjection &viewProjection)
{
    pCameraForGPUData_->worldPosition = viewProjection.translation_;
    cameraPosition_ = viewProjection.translation_;

    directionalLight_.Update();

#ifdef USE_IMGUI
    // ImGuizmoManager 側の登録が失われていたら（Finalize 後の再初期化など）登録し直す。
    // 先頭1件の有無だけ見れば足りる。
    if (!gizmoNames_.empty() && !ImGuizmoManager::GetInstance()->HasTarget(gizmoNames_.front()))
    {
        SyncGizmoTargets();
    }
#endif

    // コードから光源が増減した場合も親子付けの登録を追従させる
    if (lastAttachPointCount_ != pointLights_.GetCount() || lastAttachSpotCount_ != spotLights_.GetCount())
    {
        lastAttachPointCount_ = pointLights_.GetCount();
        lastAttachSpotCount_ = spotLights_.GetCount();
        SyncAttachTargets();
    }

    // ギズモで動かされた向きハンドルをスポットライトの向きへ反映する
    spotLights_.UpdateAimPoints();

    pointLights_.UpdateConstantBuffer(cameraPosition_);
    spotLights_.UpdateConstantBuffer();

    DrawLightVisualization();
}

void LightGroup::Draw()
{
    // 通常描画・スキニング・G-Buffer で番号が違うのでレジスタ番号で引く
    const ShaderRootSignature *rootSignature = PipelineManager::GetInstance()->GetCurrentRootSignature();
    assert(rootSignature && "ライトを使うパイプラインのルートシグネチャが未生成です");

    ID3D12GraphicsCommandList *pCommandList = pDxCommon_->GetCommandList().Get();

    pCommandList->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(1, D3D12_SHADER_VISIBILITY_PIXEL), directionalLight_.GetGpuAddress());

    pCommandList->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(2, D3D12_SHADER_VISIBILITY_PIXEL), cameraForGPUResource_->GetGPUVirtualAddress());

    pCommandList->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(3, D3D12_SHADER_VISIBILITY_PIXEL), pointLights_.GetConstantBufferAddress());

    pCommandList->SetGraphicsRootConstantBufferView(
        rootSignature->GetCbvIndex(4, D3D12_SHADER_VISIBILITY_PIXEL), spotLights_.GetConstantBufferAddress());

    // トゥーン設定（b6）。G-Bufferパスのシェーダーは b6 を使わないが、
    // ルートシグネチャは前方描画と共通なので同じように差してよい。
    // 使っていないパイプラインでは UINT_MAX が返るので、そのときは差さない。
    const UINT toonIndex = rootSignature->GetCbvIndex(6, D3D12_SHADER_VISIBILITY_PIXEL);
    const D3D12_GPU_VIRTUAL_ADDRESS toonAddress = ToonSettings::GetInstance()->GetGpuAddress();
    if (toonIndex != UINT_MAX && toonAddress != 0)
    {
        pCommandList->SetGraphicsRootConstantBufferView(toonIndex, toonAddress);
    }
}

void LightGroup::CommitPointLights()
{
    pointLights_.UpdateConstantBuffer(cameraPosition_);
    pointLights_.UploadStructuredBuffer();
}

// ===================================================
// 名前の一意化（点光源とスポットをまたいで見る）
// ===================================================

std::string LightGroup::MakeUniqueLightName(const std::string &desired, int ignorePoint, int ignoreSpot) const
{
    // 末尾の連番を外した「素の名前」を求める（"点光源3" → "点光源"）
    std::string base = desired.empty() ? std::string("光源") : desired;
    size_t digitStart = base.size();
    while (digitStart > 0 && std::isdigit(static_cast<unsigned char>(base[digitStart - 1])))
    {
        --digitStart;
    }
    if (digitStart > 0 && digitStart < base.size())
    {
        base = base.substr(0, digitStart);
    }

    const std::vector<PointLightGroup::Entry> &points = pointLights_.GetEntries();
    const std::vector<SpotLightGroup::Entry> &spots = spotLights_.GetEntries();

    auto IsTaken = [&](const std::string &candidate) {
        for (int i = 0; i < static_cast<int>(points.size()); ++i)
        {
            if (i != ignorePoint && points[i].name == candidate)
                return true;
        }
        for (int i = 0; i < static_cast<int>(spots.size()); ++i)
        {
            if (i != ignoreSpot && spots[i].name == candidate)
                return true;
        }
        return false;
    };

    if (!IsTaken(desired) && !desired.empty())
    {
        return desired;
    }
    for (int suffix = 1; suffix < 100000; ++suffix)
    {
        std::string candidate = std::format("{}{}", base, suffix);
        if (!IsTaken(candidate))
        {
            return candidate;
        }
    }
    return base;
}

void LightGroup::EnsureUniqueNames()
{
    // 読み込み直後は同名が並びうるので、前から順に一意化していく
    std::vector<PointLightGroup::Entry> &points = pointLights_.GetEntries();
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        points[i].name = MakeUniqueLightName(points[i].name, i, -1);
    }
    std::vector<SpotLightGroup::Entry> &spots = spotLights_.GetEntries();
    for (int i = 0; i < static_cast<int>(spots.size()); ++i)
    {
        spots[i].name = MakeUniqueLightName(spots[i].name, -1, i);
    }
}

// ===================================================
// ギズモ連携
// ===================================================

std::string LightGroup::PointGizmoName(int index) const
{
    if (!pointLights_.IsValidIndex(index))
        return {};
    return kGizmoPrefix + pointLights_.GetEntries()[index].name;
}

std::string LightGroup::SpotGizmoName(int index) const
{
    if (!spotLights_.IsValidIndex(index))
        return {};
    return kGizmoPrefix + spotLights_.GetEntries()[index].name;
}

std::string LightGroup::SpotAimGizmoName(int index) const
{
    if (!spotLights_.IsValidIndex(index))
        return {};
    return kGizmoPrefix + spotLights_.GetEntries()[index].name + kGizmoAimSuffix;
}

void LightGroup::UnregisterGizmoTargets()
{
#ifdef USE_IMGUI
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    for (const std::string &name : gizmoNames_)
    {
        gizmo->RemoveTarget(name);
    }
#endif
    gizmoNames_.clear();
}

void LightGroup::SyncGizmoTargets()
{
    // 親子付けの登録も同じタイミングで作り直す（こちらは Release でも動かす）
    SyncAttachTargets();

#ifdef USE_IMGUI
    // std::vector の再確保でポインタが変わるため、登録は毎回作り直す
    UnregisterGizmoTargets();

    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();

    std::vector<PointLightGroup::Entry> &points = pointLights_.GetEntries();
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const std::string name = PointGizmoName(i);
        gizmo->AddTarget(name, &points[i].gpu.position, nullptr, nullptr, true,
                         [this, i]() { pointLights_.DrawGizmoInspector(i); });
        gizmo->SetCategory(name, GizmoCategory::Light);
        gizmoNames_.push_back(name);
    }

    std::vector<SpotLightGroup::Entry> &spots = spotLights_.GetEntries();
    for (int i = 0; i < static_cast<int>(spots.size()); ++i)
    {
        const std::string name = SpotGizmoName(i);
        gizmo->AddTarget(name, &spots[i].gpu.position, nullptr, nullptr, true,
                         [this, i]() { spotLights_.DrawGizmoInspector(i); });
        gizmo->SetCategory(name, GizmoCategory::Light);
        gizmoNames_.push_back(name);

        // 向きは回転ギズモではなく「照射先の点」を掴んで決める。
        // ImGuizmoManager は平行移動しか各ターゲットへ反映しないため、この形が確実。
        const std::string aimName = SpotAimGizmoName(i);
        gizmo->AddTarget(aimName, &spots[i].aimPoint, nullptr, nullptr, true,
                         [this, i]() { spotLights_.DrawGizmoInspector(i); });
        gizmo->SetCategory(aimName, GizmoCategory::Light);
        gizmoNames_.push_back(aimName);
    }
#endif
}

void LightGroup::SyncAttachTargets()
{
    // 種類をまたいだ親子付け（光源をプレイヤーに付ける等）の対象として登録し直す。
    // std::vector の再確保でポインタが変わるため、ギズモと同じくライトの増減・改名のたびに呼ぶ。
    // ギズモと違い Release でも要る処理なので USE_IMGUI では囲まない。
    AttachmentManager *attachment = AttachmentManager::GetInstance();
    attachment->UnregisterKind(AttachKind::Light);

    std::vector<PointLightGroup::Entry> &points = pointLights_.GetEntries();
    for (PointLightGroup::Entry &entry : points)
    {
        AttachTarget target;
        target.kind = AttachKind::Light;
        target.name = AttachName(entry.name);
        target.position = &entry.gpu.position;
        attachment->Register(target);
    }

    std::vector<SpotLightGroup::Entry> &spots = spotLights_.GetEntries();
    for (SpotLightGroup::Entry &entry : spots)
    {
        AttachTarget target;
        target.kind = AttachKind::Light;
        target.name = AttachName(entry.name);
        target.position = &entry.gpu.position;
        // スポットは向きも親の回転に追従させたいので渡しておく
        target.direction = &entry.gpu.direction;
        attachment->Register(target);
    }
}

std::string LightGroup::AttachName(const std::string &lightName)
{
    // 3Dオブジェクトと名前がぶつからないよう、光源であることが分かる接頭辞を付ける
    return std::string(kAttachPrefix) + lightName;
}

void LightGroup::SyncSelectionToGizmo()
{
#ifdef USE_IMGUI
    if (!syncGizmoSelection_)
        return;

    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    switch (selectedKind_)
    {
    case SelectionKind::Point:
        gizmo->SelectOnly(PointGizmoName(selectedIndex_));
        break;
    case SelectionKind::Spot:
        gizmo->SelectOnly(SpotGizmoName(selectedIndex_));
        break;
    default:
        // 平行光源・未選択のときはライトのギズモを外す（空文字は未登録なので選択解除になる）
        gizmo->SelectOnly("");
        break;
    }
#endif
}

void LightGroup::SyncSelectionFromGizmo()
{
#ifdef USE_IMGUI
    if (!syncGizmoSelection_)
        return;

    // 毎フレーム全走査すると重いので「今の選択がまだ生きているか」を先に見て早期に抜ける
    ImGuizmoManager *gizmo = ImGuizmoManager::GetInstance();
    const std::string current = (selectedKind_ == SelectionKind::Point)  ? PointGizmoName(selectedIndex_)
                                : (selectedKind_ == SelectionKind::Spot) ? SpotGizmoName(selectedIndex_)
                                                                         : std::string();
    const bool currentStillSelected =
        !current.empty() && (gizmo->IsSelected(current) ||
                             (selectedKind_ == SelectionKind::Spot && gizmo->IsSelected(SpotAimGizmoName(selectedIndex_))));
    if (currentStillSelected)
    {
        return;
    }

    for (int i = 0; i < static_cast<int>(pointLights_.GetCount()); ++i)
    {
        if (gizmo->IsSelected(PointGizmoName(i)))
        {
            selectedKind_ = SelectionKind::Point;
            selectedIndex_ = i;
            return;
        }
    }
    for (int i = 0; i < static_cast<int>(spotLights_.GetCount()); ++i)
    {
        if (gizmo->IsSelected(SpotGizmoName(i)) || gizmo->IsSelected(SpotAimGizmoName(i)))
        {
            selectedKind_ = SelectionKind::Spot;
            selectedIndex_ = i;
            return;
        }
    }
#endif
}

// ===================================================
// ImGui
// ===================================================

void LightGroup::DrawImGui()
{
#ifdef USE_IMGUI
    // シーン上でライトのギズモを掴んだら、一覧の選択もそちらへ合わせる
    SyncSelectionFromGizmo();

    DrawStatusHeader();

    ImGui::Spacing();
    SectionHeader("[ デバッグ描画 ]", DebugTheme::kAccentCyan);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, DebugTheme::kAccentCyan);
    ImGui::Checkbox("光源を可視化##lightvis", &showLightVisualization_);
    ImGui::SetItemTooltip("光源の位置・向き・届く範囲を線で表示します");
    ImGui::SameLine();
    ImGui::BeginDisabled(!showLightVisualization_);
    ImGui::Checkbox("選択中だけ詳細##lightvisSel", &visualizeSelectedOnly_);
    ImGui::SetItemTooltip("選択していない光源は小さな十字だけにします。\n大量に配置したときの線描画の負荷対策です");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("ギズモと選択を同期##lightvisGizmo", &syncGizmoSelection_);
    ImGui::SetItemTooltip("一覧で選んだ光源をシーン上のギズモでも選択します。\nギズモ側で掴んだときは一覧の選択が追従します");
    ImGui::PopStyleColor();

    if (syncGizmoSelection_ && !ImGuizmoManager::GetInstance()->IsCategoryEnabled(GizmoCategory::Light))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("※ ギズモの「操作対象フィルタ」で【ライト】がOFFのため、シーン上では掴めません");
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();

    // 一覧とプロパティを左右に並べる。セーブ/ロードぶんの高さを残しておく
    const float paneHeight = (std::max)(240.0f, ImGui::GetContentRegionAvail().y - 130.0f);
    DrawLightListPanel(paneHeight);
    ImGui::SameLine();
    DrawPropertyPanel(paneHeight);

    ImGui::Spacing();
    // トゥーンは「光の当たり方」そのものを変える設定なので、光源UIと同じ画面に置く
    ToonSettings::GetInstance()->DrawImGui();

    ImGui::Spacing();
    DrawSaveLoadSection();
#endif // USE_IMGUI
}

void LightGroup::DrawStatusHeader()
{
#ifdef USE_IMGUI
    const bool deferred = DeferredRenderer::GetInstance()->IsEnabled();

    const int activePoint = pointLights_.GetActiveCount();
    const int activeSpot = spotLights_.GetActiveCount();

    StatusBadge(deferred ? "ディファード ON : 点光源の個数制限なし" : "前方描画 : 点光源は16個まで",
                deferred ? DebugTheme::kAccentGreen : DebugTheme::kAccentOrange);

    const uint32_t gpuTotal = pointLights_.GetGpuTotalCount();

    if (ImGui::BeginTable("##LightStats", 5, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
        auto Cell = [](const char *label, const std::string &value, ImVec4 color) {
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kTextDim);
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextUnformatted(value.c_str());
            ImGui::PopStyleColor();
        };

        ImGui::TableNextRow();
        Cell("点光源", std::format("{} / {}", activePoint, pointLights_.GetCount()), DebugTheme::kAccentYellow);
        Cell("スポット", std::format("{} / {}", activeSpot, MAX_SPOT_LIGHTS), DebugTheme::kAccentBlue);
        Cell("動的（粒子等）", std::format("{}", pointLights_.GetDynamicCount()), DebugTheme::kAccentPurple);
        Cell("GPU転送数", std::format("{} / {}", pointLights_.GetBufferCount(), kMaxBufferedPointLights),
             DebugTheme::kAccentCyan);
        Cell("粒子光源(GPU生成)", std::format("{}", pointLights_.GetParticleLightCount()),
             gpuTotal > kMaxBufferedPointLights ? DebugTheme::kAccentOrange : DebugTheme::kAccentGreen);
        ImGui::EndTable();
    }

    if (gpuTotal > kMaxBufferedPointLights)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("光源の合計が %u 個で上限 %u を超えています。溢れた粒子光源は捨てられます。\n"
                           "パーティクル側の「間引き」を大きくするか「光源の上限」を下げてください。",
                           gpuTotal, kMaxBufferedPointLights);
        ImGui::PopStyleColor();
    }

    if (!deferred && activePoint > MAX_POINT_LIGHTS)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentOrange);
        ImGui::TextWrapped("前方描画では点光源は %d 個までしか反映されません（明るさ×半径÷カメラ距離が大きい順に採用）。\n"
                           "「描画システム」でディファードをONにすると全部反映されます。",
                           MAX_POINT_LIGHTS);
        ImGui::PopStyleColor();
    }
#endif // USE_IMGUI
}

void LightGroup::DrawLightListPanel(float height)
{
#ifdef USE_IMGUI
    ImGui::BeginChild("##LightList", ImVec2(250.0f, height),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

    SectionHeader("[ 光源一覧 ]", DebugTheme::kAccentPurple);

    // ---- 追加ボタン ----
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, DebugTheme::kBgGreen);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f, 0.68f, 0.52f, 0.40f));
    if (ImGui::Button("＋ 点光源", ImVec2(buttonWidth, 0.0f)))
    {
        const int added = pointLights_.Add(MakeUniqueLightName("点光源1"));
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Point;
            selectedIndex_ = added;
            SyncGizmoTargets();
            SyncSelectionToGizmo();
        }
    }
    ImGui::SetItemTooltip("周囲を等方向に照らす光源を追加します");
    ImGui::SameLine();
    ImGui::BeginDisabled(!spotLights_.CanAdd());
    if (ImGui::Button("＋ スポット", ImVec2(buttonWidth, 0.0f)))
    {
        const int added = spotLights_.Add(MakeUniqueLightName("スポット1"));
        if (added >= 0)
        {
            selectedKind_ = SelectionKind::Spot;
            selectedIndex_ = added;
            SyncGizmoTargets();
            SyncSelectionToGizmo();
        }
    }
    ImGui::SetItemTooltip("円錐状に照らす光源を追加します（上限 32個）");
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    // ---- 絞り込み ----
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##LightFilter", "名前で絞り込み...", listFilter_, sizeof(listFilter_));

    ImGui::Separator();

    // ---- 平行光源（常に先頭）----
    {
        const bool selected = (selectedKind_ == SelectionKind::Directional);
        ImGui::PushID("dirLight");
        bool active = directionalLight_.IsEnabled();
        if (ThemedToggle("##on", &active, DebugTheme::kAccentYellow))
        {
            directionalLight_.SetEnabled(active);
        }
        ImGui::SameLine(0.0f, 4.0f);
        LightUI::ColorSwatch(directionalLight_.GetColor());
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable("平行光源", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight())))
        {
            selectedKind_ = SelectionKind::Directional;
            selectedIndex_ = -1;
            SyncSelectionToGizmo();
        }
        ImGui::PopID();
    }

    // 走査中に配列を増減させると参照が壊れるので、構造変化はループ後にまとめて行う
    const int pointSelected = (selectedKind_ == SelectionKind::Point) ? selectedIndex_ : -1;
    const LightListResult pointResult = pointLights_.DrawListRows(listFilter_, pointSelected);

    const int spotSelected = (selectedKind_ == SelectionKind::Spot) ? selectedIndex_ : -1;
    const LightListResult spotResult = spotLights_.DrawListRows(listFilter_, spotSelected);

    // 走査が終わってから選択と構造を変える
    if (pointResult.clickedIndex >= 0)
    {
        selectedKind_ = SelectionKind::Point;
        selectedIndex_ = pointResult.clickedIndex;
        SyncSelectionToGizmo();
    }
    if (spotResult.clickedIndex >= 0)
    {
        selectedKind_ = SelectionKind::Spot;
        selectedIndex_ = spotResult.clickedIndex;
        SyncSelectionToGizmo();
    }
    if (pointResult.duplicateIndex >= 0)
    {
        ApplyEditRequest({LightEditRequest::Kind::Duplicate, {}}, false, pointResult.duplicateIndex);
    }
    if (spotResult.duplicateIndex >= 0)
    {
        ApplyEditRequest({LightEditRequest::Kind::Duplicate, {}}, true, spotResult.duplicateIndex);
    }
    if (pointResult.removeIndex >= 0)
    {
        ApplyEditRequest({LightEditRequest::Kind::Remove, {}}, false, pointResult.removeIndex);
    }
    if (spotResult.removeIndex >= 0)
    {
        ApplyEditRequest({LightEditRequest::Kind::Remove, {}}, true, spotResult.removeIndex);
    }

    if (pointLights_.GetCount() == 0 && spotLights_.GetCount() == 0)
    {
        ImGui::Spacing();
        ImGui::TextDisabled("光源がありません。上のボタンで追加できます");
    }

    ImGui::EndChild();
#else
    (void)height;
#endif // USE_IMGUI
}

void LightGroup::SyncNameEditBuffer(const std::string &owner, const std::string &currentName)
{
    if (nameEditOwner_ != owner)
    {
        nameEditBuffer_ = currentName;
        nameEditOwner_ = owner;
    }
}

void LightGroup::DrawPropertyPanel(float height)
{
#ifdef USE_IMGUI
    ImGui::BeginChild("##LightProps", ImVec2(0.0f, height), ImGuiChildFlags_Borders);

    switch (selectedKind_)
    {
    case SelectionKind::Directional:
        directionalLight_.DrawProperties();
        break;
    case SelectionKind::Point:
        if (pointLights_.IsValidIndex(selectedIndex_))
        {
            SyncNameEditBuffer(std::format("P{}", selectedIndex_),
                               pointLights_.GetEntries()[selectedIndex_].name);
            const LightEditRequest request = pointLights_.DrawProperties(selectedIndex_, nameEditBuffer_);
            ApplyEditRequest(request, false, selectedIndex_);
        }
        else
        {
            ImGui::TextDisabled("光源が選択されていません");
        }
        break;
    case SelectionKind::Spot:
        if (spotLights_.IsValidIndex(selectedIndex_))
        {
            SyncNameEditBuffer(std::format("S{}", selectedIndex_),
                               spotLights_.GetEntries()[selectedIndex_].name);
            const LightEditRequest request =
                spotLights_.DrawProperties(selectedIndex_, nameEditBuffer_, kGizmoAimSuffix);
            ApplyEditRequest(request, true, selectedIndex_);
        }
        else
        {
            ImGui::TextDisabled("光源が選択されていません");
        }
        break;
    default:
        ImGui::TextDisabled("左の一覧から光源を選んでください");
        break;
    }

    ImGui::EndChild();
#else
    (void)height;
#endif // USE_IMGUI
}

void LightGroup::ApplyEditRequest(const LightEditRequest &request, bool isSpot, int index)
{
    switch (request.kind)
    {
    case LightEditRequest::Kind::Rename:
    {
        // 名前は点光源とスポットをまたいで一意にする（ギズモの登録名がぶつかるため）
        const std::string unique =
            MakeUniqueLightName(request.newName, isSpot ? -1 : index, isSpot ? index : -1);
        if (isSpot)
        {
            spotLights_.GetEntries()[index].name = unique;
        }
        else
        {
            pointLights_.GetEntries()[index].name = unique;
        }
        nameEditBuffer_ = unique;
        SyncGizmoTargets(); // ギズモの登録名も変わるので付け直す
        SyncSelectionToGizmo();
        break;
    }
    case LightEditRequest::Kind::Duplicate:
    {
        int added = -1;
        if (isSpot)
        {
            added = spotLights_.Duplicate(index, MakeUniqueLightName(spotLights_.GetEntries()[index].name));
            if (added >= 0)
            {
                selectedKind_ = SelectionKind::Spot;
            }
        }
        else
        {
            added = pointLights_.Duplicate(index, MakeUniqueLightName(pointLights_.GetEntries()[index].name));
            if (added >= 0)
            {
                selectedKind_ = SelectionKind::Point;
            }
        }
        if (added >= 0)
        {
            selectedIndex_ = added;
            nameEditOwner_.clear(); // 名前欄を新しい対象で作り直させる
            SyncGizmoTargets();
            SyncSelectionToGizmo();
        }
        break;
    }
    case LightEditRequest::Kind::Remove:
    {
        const bool removed = isSpot ? spotLights_.Remove(index) : pointLights_.Remove(index);
        if (removed)
        {
            selectedKind_ = SelectionKind::Directional;
            selectedIndex_ = -1;
            nameEditOwner_.clear();
            SyncGizmoTargets();
            SyncSelectionToGizmo();
        }
        break;
    }
    case LightEditRequest::Kind::None:
    default:
        break;
    }
}

void LightGroup::DrawSaveLoadSection()
{
#ifdef USE_IMGUI
    SectionHeader("[ セーブ / ロード ]", DebugTheme::kAccentPurple);

    static char saveFileName[256] = "DefaultLightSetting";
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lightfile", "ファイル名", saveFileName, sizeof(saveFileName));
    ImGui::Spacing();

    // 保存・読込（通知は SaveLightData / LoadLightData 側で投稿する）
    const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.58f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.52f, 0.70f, 0.95f));
    if (ImGui::Button("セーブ", ImVec2(buttonWidth, 0.0f)))
    {
        SaveLightData(std::string(saveFileName));
    }
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.48f, 0.40f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.60f, 0.50f, 0.95f));
    if (ImGui::Button("ロード", ImVec2(buttonWidth, 0.0f)))
    {
        LoadLightData(std::string(saveFileName));
    }
    ImGui::PopStyleColor(2);
#endif // USE_IMGUI
}

// ===================================================
// セーブ / ロード
// ===================================================

void LightGroup::SaveLightData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("LightGroup", fileName);

    directionalLight_.Save(dataHandler.get());
    pointLights_.Save(dataHandler.get());
    spotLights_.Save(dataHandler.get());

    dataHandler->Flush();
    ImGuiNotification::Post("ライトデータを保存しました: " + fileName, {0.2f, 0.8f, 0.2f, 1.0f});
}

void LightGroup::LoadLightData(const std::string &fileName)
{
    auto dataHandler = std::make_unique<DataHandler>("LightGroup", fileName);

    directionalLight_.Load(dataHandler.get());
    pointLights_.Load(dataHandler.get());
    spotLights_.Load(dataHandler.get());

    // 名前は点光源とスポットで共有の名前空間なので、読み込み後にまとめて一意化する
    EnsureUniqueNames();

    // 一覧の選択とギズモ登録を作り直す
    selectedKind_ = SelectionKind::Directional;
    selectedIndex_ = -1;
    nameEditOwner_.clear();
    SyncGizmoTargets();
    SyncSelectionToGizmo();

    ImGuiNotification::Post("ライトデータを読み込みました: " + fileName, {0.2f, 0.8f, 0.8f, 1.0f});
}

// ===================================================
// デバッグ描画
// ===================================================

void LightGroup::DrawLightVisualization()
{
    if (!showLightVisualization_)
        return;

    LineRenderer *drawLine = LineRenderer::GetInstance();

    // 平行光源は選択中のときだけ（線が多く画面を埋めるため）
    if (!visualizeSelectedOnly_ || selectedKind_ == SelectionKind::Directional)
    {
        directionalLight_.DrawVisualization(drawLine);
    }

    const int pointSelected = (selectedKind_ == SelectionKind::Point) ? selectedIndex_ : -1;
    pointLights_.DrawVisualization(drawLine, pointSelected, visualizeSelectedOnly_);

    const int spotSelected = (selectedKind_ == SelectionKind::Spot) ? selectedIndex_ : -1;
    spotLights_.DrawVisualization(drawLine, spotSelected, visualizeSelectedOnly_);
}
} // namespace Hagine
