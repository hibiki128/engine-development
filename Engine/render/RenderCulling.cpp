#include "RenderCulling.h"
#include <algorithm>
#include <cstring>
#include <line/LineRenderer.h>
#include <render/RenderView.h>
#include <shadow/ShadowMap.h>
#include <vector>
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#endif

namespace Hagine {
namespace {

/// カリングの状態。1フレーム内で何百回も引かれるので、視錐台は行列が変わったときだけ作り直す
struct CullingState
{
    bool enabled = true;      // カリングを行うか
    float margin = 0.5f;      // 境界ボックスを膨らませる量（ワールド単位）
    int testedCount = 0;      // このフレームに判定した数
    int culledCount = 0;      // このフレームに省いた数
    int lastTestedCount = 0;  // 表示用（フレーム途中で0に見えないよう前フレーム値を残す）
    int lastCulledCount = 0;  // 表示用

    Frustum frustum;             // 現在の視錐台
    Matrix4x4 cachedMatrix{};    // frustum を作ったビュー射影行列
    bool hasCachedMatrix = false; // 一度でも作ったか

    // ---- デバッグカメラから確かめる ----
    bool inspectWithMainCamera = false; // デバッグカメラ中はメインカメラで判定する
    bool showFrustum = true;            // メインカメラの視錐台を線で描く
    bool showCulledBoxes = true;        // 省いた物を赤い箱で描く
    bool showVisibleBoxes = false;      // 描いた物を緑の箱で描く
    float frustumLength = 60.0f;        // 視錐台の線を奥へ伸ばす長さ（遠クリップまで描くと長すぎる）
    bool hasInspection = false;         // メインカメラの行列を受け取っているか
    Matrix4x4 inspectionMatrix{};       // メインカメラのビュー射影行列

    /// <summary>判定した物の箱（線で描く用）</summary>
    struct DebugBox
    {
        AABB bounds;
        bool visible;
    };
    static constexpr size_t kMaxDebugBoxes = 4096;
    std::vector<DebugBox> currentBoxes; // このフレームに判定した物
    std::vector<DebugBox> lastBoxes;    // 前のフレームに判定した物（線はこちらを描く）
};

CullingState &State()
{
    static CullingState state;
    return state;
}

bool IsInspectingState(const CullingState &state)
{
    return state.inspectWithMainCamera && state.hasInspection;
}

/// ビュー射影行列が前回と同じなら、キャッシュした視錐台をそのまま使う
const Frustum &ResolveFrustum(const ViewProjection &viewProjection)
{
    CullingState &state = State();
    // デバッグカメラから確かめているときは、描画中のカメラではなくメインカメラで判定する
    const Matrix4x4 viewProjectionMatrix = IsInspectingState(state)
                                               ? state.inspectionMatrix
                                               : viewProjection.matView_ * viewProjection.matProjection_;

    if (!state.hasCachedMatrix ||
        std::memcmp(&state.cachedMatrix, &viewProjectionMatrix, sizeof(Matrix4x4)) != 0)
    {
        state.frustum.ExtractFromViewProjection(viewProjectionMatrix);
        state.cachedMatrix = viewProjectionMatrix;
        state.hasCachedMatrix = true;
    }
    return state.frustum;
}
} // namespace

void RenderCulling::SetEnabled(bool enabled)
{
    State().enabled = enabled;
}

bool RenderCulling::IsEnabled()
{
    return State().enabled;
}

void RenderCulling::BeginFrame()
{
    CullingState &state = State();
    state.lastTestedCount = state.testedCount;
    state.lastCulledCount = state.culledCount;
    state.testedCount = 0;
    state.culledCount = 0;
    state.lastBoxes.swap(state.currentBoxes);
    state.currentBoxes.clear();
}

void RenderCulling::SetInspectionCamera(const Matrix4x4 *pMainViewProjection)
{
    CullingState &state = State();
    state.hasInspection = (pMainViewProjection != nullptr);
    if (pMainViewProjection)
    {
        state.inspectionMatrix = *pMainViewProjection;
    }
}

bool RenderCulling::IsInspecting()
{
    return IsInspectingState(State());
}

void RenderCulling::SetInspectWithMainCamera(bool enabled)
{
    State().inspectWithMainCamera = enabled;
}

bool RenderCulling::IsInspectWithMainCamera()
{
    return State().inspectWithMainCamera;
}

void RenderCulling::SubmitDebugLines()
{
    CullingState &state = State();
    if (!IsInspectingState(state))
    {
        state.lastBoxes.clear();
        return;
    }
    LineRenderer *pLines = LineRenderer::GetInstance();

    // ---- メインカメラの視錐台（近い面の4隅から遠い面の4隅へ。奥は frustumLength で打ち切る）----
    if (state.showFrustum)
    {
        const Matrix4x4 inverse = Inverse(state.inspectionMatrix);
        const float xs[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const float ys[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        Vector3 nearCorners[4];
        Vector3 farCorners[4];
        for (int i = 0; i < 4; ++i)
        {
            nearCorners[i] = Transformation(Vector3{xs[i], ys[i], 0.0f}, inverse);
            const Vector3 farPoint = Transformation(Vector3{xs[i], ys[i], 1.0f}, inverse);
            Vector3 direction = farPoint - nearCorners[i];
            const float length = direction.Length();
            farCorners[i] = (length > state.frustumLength && length > 0.0f)
                                ? nearCorners[i] + direction * (state.frustumLength / length)
                                : farPoint;
        }
        const Vector4 color = {1.0f, 0.85f, 0.2f, 1.0f};
        for (int i = 0; i < 4; ++i)
        {
            const int next = (i + 1) % 4;
            pLines->AddLine(nearCorners[i], nearCorners[next], color);
            pLines->AddLine(farCorners[i], farCorners[next], color);
            pLines->AddLine(nearCorners[i], farCorners[i], color);
        }
    }

    // ---- 前のフレームに判定した物の箱 ----
    for (const CullingState::DebugBox &box : state.lastBoxes)
    {
        if (box.visible ? !state.showVisibleBoxes : !state.showCulledBoxes)
        {
            continue;
        }
        const Vector4 color = box.visible ? Vector4{0.35f, 0.9f, 0.45f, 1.0f} : Vector4{1.0f, 0.3f, 0.3f, 1.0f};
        pLines->AddBox(box.bounds.min, box.bounds.max, color);
    }
}

bool RenderCulling::IsVisible(const ViewProjection &viewProjection,
                              const AABB &localBounds,
                              const Matrix4x4 &worldMatrix)
{
    CullingState &state = State();
    if (!state.enabled)
    {
        return true;
    }

    // 影を落とす物はカメラの外にいても描かないといけない。
    // ここをカメラの視錐台で切ると、画面内に落ちるはずの影が消える
    if (ShadowMap::GetInstance()->IsShadowPassActive())
    {
        return true;
    }

    AABB worldBounds = Frustum::TransformAabb(localBounds, worldMatrix);

    // アニメーションで手足がバインドポーズのAABBからはみ出すことがあるので、少し余裕を持たせる
    worldBounds.min = {worldBounds.min.x - state.margin, worldBounds.min.y - state.margin, worldBounds.min.z - state.margin};
    worldBounds.max = {worldBounds.max.x + state.margin, worldBounds.max.y + state.margin, worldBounds.max.z + state.margin};

    const bool visible = ResolveFrustum(viewProjection).IsAabbVisible(worldBounds);
    if (RenderView::IsExtra())
    {
        return visible; // カメラビュー窓の判定は統計・線に入れない
    }
    ++state.testedCount;
    if (!visible)
    {
        ++state.culledCount;
    }
    // デバッグカメラから確かめているときは、線で描くために箱を控えておく
    if (IsInspectingState(state) && state.currentBoxes.size() < CullingState::kMaxDebugBoxes)
    {
        state.currentBoxes.push_back({worldBounds, visible});
    }
    return visible;
}

int RenderCulling::GetTestedCount()
{
    return State().lastTestedCount;
}

int RenderCulling::GetCulledCount()
{
    return State().lastCulledCount;
}

void RenderCulling::DrawImGui()
{
#ifdef USE_IMGUI
    CullingState &state = State();

    ImGui::Checkbox("有効##culling", &state.enabled);
    ImGui::SetItemTooltip("カメラの視界に入らないオブジェクトの描画を省きます。\n"
                          "影は画面外の物からも落ちるので、シャドウパスでは常に描きます");

    ImGui::DragFloat("余裕(ワールド単位)##culling", &state.margin, 0.05f, 0.0f, 10.0f);
    ImGui::SetItemTooltip("境界ボックスをこのぶん膨らませてから判定します。\n"
                          "アニメーションで手足がはみ出す物が画面端で消えるときは大きくします");

    const int tested = state.lastTestedCount;
    const int culled = state.lastCulledCount;
    const float ratio = (tested > 0) ? (static_cast<float>(culled) / static_cast<float>(tested) * 100.0f) : 0.0f;

    ImGui::Text("判定 %d 件 / 省いた %d 件 (%.0f%%)", tested, culled, ratio);

    // ---- デバッグカメラから確かめる ----
    ImGui::Spacing();
    SectionHeader("[ デバッグカメラから確かめる ]", DebugTheme::kAccentYellow);
    ImGui::Checkbox("デバッグカメラ中はメインカメラで判定する##culling", &state.inspectWithMainCamera);
    ImGui::SetItemTooltip("デバッグカメラを使っている間も、カリングの判定だけは\n"
                          "デバッグカメラを使う前のカメラ（メイン）の視界で行います。\n"
                          "メインから見えない物が消えるので、外から回り込んで確かめられます");
    if (state.inspectWithMainCamera)
    {
        if (state.hasInspection)
        {
            StatusBadge("メインカメラで判定中", DebugTheme::kAccentYellow);
        }
        else
        {
            StatusBadge("デバッグカメラを使うと有効になります", DebugTheme::kTextDim);
        }
    }
    ImGui::BeginDisabled(!state.inspectWithMainCamera);
    ImGui::Checkbox("メインカメラの視錐台を線で描く（黄）##culling", &state.showFrustum);
    ImGui::Checkbox("省いた物を箱で描く（赤）##culling", &state.showCulledBoxes);
    ImGui::Checkbox("描いた物を箱で描く（緑）##culling", &state.showVisibleBoxes);
    ImGui::DragFloat("視錐台の線の長さ##culling", &state.frustumLength, 1.0f, 1.0f, 2000.0f, "%.0f");
    ImGui::SetItemTooltip("遠くの面まで描くと長すぎて見えないので、このぶんで打ち切ります");
    ImGui::EndDisabled();
#endif
}
} // namespace Hagine
