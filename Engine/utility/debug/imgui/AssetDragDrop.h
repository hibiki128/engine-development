#pragma once
// =============================================================
// アセット（テクスチャ）のドラッグ&ドロップ共通ヘルパー。
//   アセットブラウザ等の「ドラッグ元」と、各種テクスチャ設定UIの「ドロップ先」を
//   1つのペイロード種別で繋ぐ。プロジェクト全体でテクスチャをD&D設定できるようにする。
//   ・ペイロード = テクスチャの相対パス文字列（"debug/circle2.png" 等。images ルート基準）
//   ・USE_IMGUI のときのみ有効（リリースビルドでは空実装）。
// =============================================================
#ifdef USE_IMGUI
#include "imgui.h"
#include <string>

namespace Hagine {
namespace AssetDragDrop {

// ペイロード種別ID（ImGui の制約: 32文字以内）。
inline constexpr const char *kTexturePayloadId = "ASSET_TEX_PATH";
inline constexpr const char *kModelPayloadId = "ASSET_MDL_PATH";

/// <summary>
/// 直前に描いたアイテム（Selectable/Image 等）をテクスチャパスのドラッグ元にする。
/// </summary>
/// <param name="relPath">ドラッグするテクスチャの相対パス（images ルート基準）</param>
/// <param name="previewTexId">ドラッグ中に表示するサムネ（0 なら名前のみ表示）</param>
inline void TextureSource(const std::string &relPath, ImTextureID previewTexId = 0)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        // 末尾 NUL を含めて送る（受け側で C 文字列として読む）。
        ImGui::SetDragDropPayload(kTexturePayloadId, relPath.c_str(),
                                  relPath.size() + 1);
        // ドラッグ中のプレビュー（サムネ + パス）。
        if (previewTexId != 0)
            ImGui::Image(previewTexId, ImVec2(48.0f, 48.0f));
        ImGui::TextUnformatted(relPath.c_str());
        ImGui::EndDragDropSource();
    }
}

/// <summary>
/// 直前に描いたアイテムをテクスチャのドロップ先にする。
/// ドロップされたら outRelPath を受け取ったパスで更新して true を返す。
/// </summary>
inline bool TextureTarget(std::string &outRelPath)
{
    bool received = false;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kTexturePayloadId))
        {
            if (payload->Data && payload->DataSize > 0)
            {
                outRelPath.assign(static_cast<const char *>(payload->Data));
                received = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return received;
}

/// <summary>
/// 直前に描いたアイテムをモデルパスのドラッグ元にする。
/// シーンウィンドウへドロップするとそのモデルのオブジェクトが生成される。
/// </summary>
/// <param name="relPath">ドラッグするモデルの相対パス（models ルート基準）</param>
inline void ModelSource(const std::string &relPath)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        // 末尾 NUL を含めて送る（受け側で C 文字列として読む）。
        ImGui::SetDragDropPayload(kModelPayloadId, relPath.c_str(), relPath.size() + 1);
        ImGui::TextUnformatted("配置: ");
        ImGui::SameLine();
        ImGui::TextUnformatted(relPath.c_str());
        ImGui::EndDragDropSource();
    }
}

/// <summary>
/// 直前に描いたアイテム（シーンウィンドウの画像など）をモデルのドロップ先にする。
/// ドロップされたら outRelPath を受け取ったパスで更新して true を返す。
/// </summary>
inline bool ModelTarget(std::string &outRelPath)
{
    bool received = false;
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kModelPayloadId))
        {
            if (payload->Data && payload->DataSize > 0)
            {
                outRelPath.assign(static_cast<const char *>(payload->Data));
                received = true;
            }
        }
        ImGui::EndDragDropTarget();
    }
    return received;
}

} // namespace AssetDragDrop
} // namespace Hagine
#endif // USE_IMGUI
