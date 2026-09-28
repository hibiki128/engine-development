#define NOMINMAX
#include "CameraFade.h"
#include <algorithm>
#include <cmath>
#include <data/DataHandler.h>
#include <shadow/ShadowMap.h>
#ifdef USE_IMGUI
#include <imgui.h>
// DebugUIHelper.h は ImGui:: を使うので imgui.h の後に include する
#include "utility/debug/imgui/DebugUIHelper.h"
#include "utility/debug/imgui/ImGuiNotification.h"
#endif

namespace Hagine {
namespace {
CameraFade::Settings g_settings;
bool g_loaded = false;

Vector3 TransformPoint(const Vector3 &p, const Matrix4x4 &m)
{
    return {
        p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
        p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
        p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2],
    };
}
} // namespace

CameraFade::Settings &CameraFade::GetSettings()
{
    if (!g_loaded)
    {
        Load();
    }
    return g_settings;
}

float CameraFade::Compute(const ViewProjection &viewProjection, const AABB &localBounds, const Matrix4x4 &worldMatrix)
{
    const Settings &settings = GetSettings();
    // 影は消さない（物が見えなくなっても、影が残っていたほうが自然）
    if (!settings.enabled || ShadowMap::GetInstance()->IsShadowPassActive())
    {
        return 1.0f;
    }

    // 大きい物（地形・床）は対象外。ワールドでの箱の対角線で測る
    const Vector3 half = (localBounds.max - localBounds.min) * 0.5f;
    const Vector3 axisX = {worldMatrix.m[0][0], worldMatrix.m[0][1], worldMatrix.m[0][2]};
    const Vector3 axisY = {worldMatrix.m[1][0], worldMatrix.m[1][1], worldMatrix.m[1][2]};
    const Vector3 axisZ = {worldMatrix.m[2][0], worldMatrix.m[2][1], worldMatrix.m[2][2]};
    const Vector3 worldHalf = {half.x * axisX.Length(), half.y * axisY.Length(), half.z * axisZ.Length()};
    if (worldHalf.Length() * 2.0f > settings.maxObjectSize)
    {
        return 1.0f;
    }

    // カメラの位置をローカルへ戻し、箱に押し込んだ点が「箱の上でカメラに一番近い点」
    const Matrix4x4 &cameraWorld = viewProjection.matWorld_;
    const Vector3 cameraPosition = {cameraWorld.m[3][0], cameraWorld.m[3][1], cameraWorld.m[3][2]};
    const Vector3 localCamera = TransformPoint(cameraPosition, Inverse(worldMatrix));
    const Vector3 localClosest = {
        std::clamp(localCamera.x, localBounds.min.x, localBounds.max.x),
        std::clamp(localCamera.y, localBounds.min.y, localBounds.max.y),
        std::clamp(localCamera.z, localBounds.min.z, localBounds.max.z),
    };
    const float distance = (TransformPoint(localClosest, worldMatrix) - cameraPosition).Length();

    const float range = (std::max)(settings.fadeStartDistance - settings.fadeEndDistance, 1e-3f);
    return std::clamp((distance - settings.fadeEndDistance) / range, 0.0f, 1.0f);
}

void CameraFade::Load()
{
    g_loaded = true;
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("DrawSystem", "CameraFade");
    g_settings.enabled = data->Load("enabled", g_settings.enabled);
    g_settings.fadeStartDistance = data->Load("fadeStartDistance", g_settings.fadeStartDistance);
    g_settings.fadeEndDistance = data->Load("fadeEndDistance", g_settings.fadeEndDistance);
    g_settings.maxObjectSize = data->Load("maxObjectSize", g_settings.maxObjectSize);
}

void CameraFade::Save()
{
    std::unique_ptr<DataHandler> data = std::make_unique<DataHandler>("DrawSystem", "CameraFade");
    data->Save("enabled", g_settings.enabled);
    data->Save("fadeStartDistance", g_settings.fadeStartDistance);
    data->Save("fadeEndDistance", g_settings.fadeEndDistance);
    data->Save("maxObjectSize", g_settings.maxObjectSize);
}

void CameraFade::DrawImGui()
{
#ifdef USE_IMGUI
    Settings &settings = GetSettings();
    SectionHeader("[ カメラに近い物を透けさせる ]", DebugTheme::kAccentCyan);
    AccentCheckbox("有効##cameraFade", &settings.enabled, DebugTheme::kAccentCyan);
    ImGui::SetItemTooltip("カメラが物に近づくと、網目状に間引いてだんだん透けさせ、最後は見えなくします。\n"
                          "影は残ります。オブジェクトごとに外すこともできます（インスペクタの「表示」）");
    ImGui::BeginDisabled(!settings.enabled);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("透け始める距離##cameraFade", &settings.fadeStartDistance, 0.05f, 0.0f, 50.0f, "%.2f");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("完全に消える距離##cameraFade", &settings.fadeEndDistance, 0.05f, 0.0f, 50.0f, "%.2f");
    settings.fadeEndDistance = (std::min)(settings.fadeEndDistance, settings.fadeStartDistance);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloat("対象にする大きさの上限##cameraFade", &settings.maxObjectSize, 0.5f, 0.0f, 1000.0f, "%.1f");
    ImGui::SetItemTooltip("箱の対角線がこれより大きい物（地形・床など）は透けさせません");
    ImGui::EndDisabled();
    if (NeutralButton("保存##cameraFade"))
    {
        Save();
        ImGuiNotification::Post("カメラの近接フェードの設定を保存しました", {0.2f, 0.8f, 0.2f, 1.0f});
    }
#endif // USE_IMGUI
}

} // namespace Hagine
