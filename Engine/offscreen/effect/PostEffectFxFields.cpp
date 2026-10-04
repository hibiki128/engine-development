#include "PostEffectFxFields.h"
#include <type/Vector2.h>
#include <type/Vector3.h>
#include <type/Vector4.h>
#include <cstring>
#ifdef USE_IMGUI
#include "imgui.h"
#include "utility/debug/imgui/DebugUIHelper.h"
#include <icon/IconsFontAwesome5.h>
#endif

namespace Hagine {
namespace FxFields {
namespace {

/// <summary>項目が持つ値の大きさ（バイト）。値を持たない項目は 0</summary>
size_t ValueSize(FxFieldType type)
{
    switch (type)
    {
    case FxFieldType::Float:
    case FxFieldType::Int:
    case FxFieldType::Toggle:
    case FxFieldType::Combo:
        return 4;
    case FxFieldType::Float2:
        return 8;
    case FxFieldType::Float3:
    case FxFieldType::Color3:
        return 12;
    case FxFieldType::Color4:
        return 16;
    default:
        return 0;
    }
}

template <typename T>
T &At(void *data, size_t offset)
{
    return *reinterpret_cast<T *>(static_cast<char *>(data) + offset);
}

template <typename T>
const T &At(const void *data, size_t offset)
{
    return *reinterpret_cast<const T *>(static_cast<const char *>(data) + offset);
}

} // namespace

bool DrawUI(std::span<const FxField> fields, void *data, const void *defaults)
{
    bool changed = false;
#ifdef USE_IMGUI
    for (const FxField &field : fields)
    {
        ImGui::PushID(field.label ? field.label : "");
        switch (field.type)
        {
        case FxFieldType::Section:
            ImGui::SeparatorText(field.label);
            break;
        case FxFieldType::Note:
            DimText(field.label);
            break;
        case FxFieldType::Float:
            changed |= ImGui::DragFloat(field.label, &At<float>(data, field.offset), field.speed, field.minValue, field.maxValue,
                                        field.speed < 0.001f ? "%.4f" : "%.3f");
            break;
        case FxFieldType::Int:
            changed |= ImGui::SliderInt(field.label, &At<int>(data, field.offset), static_cast<int>(field.minValue),
                                        static_cast<int>(field.maxValue));
            break;
        case FxFieldType::Toggle:
        {
            bool value = At<int>(data, field.offset) != 0;
            if (ImGui::Checkbox(field.label, &value))
            {
                At<int>(data, field.offset) = value ? 1 : 0;
                changed = true;
            }
            break;
        }
        case FxFieldType::Combo:
            changed |= ImGui::Combo(field.label, &At<int>(data, field.offset), field.items);
            break;
        case FxFieldType::Float2:
            changed |= ImGui::DragFloat2(field.label, &At<float>(data, field.offset), field.speed, field.minValue, field.maxValue);
            break;
        case FxFieldType::Float3:
            changed |= ImGui::DragFloat3(field.label, &At<float>(data, field.offset), field.speed, field.minValue, field.maxValue);
            break;
        case FxFieldType::Color3:
            changed |= ImGui::ColorEdit3(field.label, &At<float>(data, field.offset), ImGuiColorEditFlags_Float);
            break;
        case FxFieldType::Color4:
            changed |= ImGui::ColorEdit4(field.label, &At<float>(data, field.offset), ImGuiColorEditFlags_Float | ImGuiColorEditFlags_AlphaBar);
            break;
        }
        if (field.tooltip && ValueSize(field.type) > 0)
        {
            ImGui::SetItemTooltip("%s", field.tooltip);
        }
        // 右クリックでその項目だけ既定値へ戻す
        if (ValueSize(field.type) > 0 && ImGui::BeginPopupContextItem("##fxFieldReset"))
        {
            if (ImGui::MenuItem(ICON_FA_UNDO " 既定値に戻す"))
            {
                std::memcpy(static_cast<char *>(data) + field.offset, static_cast<const char *>(defaults) + field.offset,
                            ValueSize(field.type));
                changed = true;
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    if (NeutralButton(ICON_FA_UNDO " 既定に戻す"))
    {
        ResetToDefaults(fields, data, defaults);
        changed = true;
    }
    ImGui::SetItemTooltip("このエフェクトの値をすべて最初の値へ戻します（項目を右クリックすると1つだけ戻せます）");
#else
    (void)fields;
    (void)data;
    (void)defaults;
#endif
    return changed;
}

void Save(std::span<const FxField> fields, const void *data, DataHandler *handler, const std::string &prefix)
{
    for (const FxField &field : fields)
    {
        if (!field.key)
        {
            continue;
        }
        const std::string key = prefix + field.key;
        switch (field.type)
        {
        case FxFieldType::Float:
            handler->Save<float>(key, At<float>(data, field.offset));
            break;
        case FxFieldType::Int:
        case FxFieldType::Toggle:
        case FxFieldType::Combo:
            handler->Save<int>(key, At<int>(data, field.offset));
            break;
        case FxFieldType::Float2:
            handler->Save<Vector2>(key, At<Vector2>(data, field.offset));
            break;
        case FxFieldType::Float3:
        case FxFieldType::Color3:
            handler->Save<Vector3>(key, At<Vector3>(data, field.offset));
            break;
        case FxFieldType::Color4:
            handler->Save<Vector4>(key, At<Vector4>(data, field.offset));
            break;
        default:
            break;
        }
    }
}

void Load(std::span<const FxField> fields, void *data, const void *defaults, DataHandler *handler, const std::string &prefix)
{
    for (const FxField &field : fields)
    {
        if (!field.key)
        {
            continue;
        }
        const std::string key = prefix + field.key;
        switch (field.type)
        {
        case FxFieldType::Float:
            At<float>(data, field.offset) = handler->Load<float>(key, At<float>(defaults, field.offset));
            break;
        case FxFieldType::Int:
        case FxFieldType::Toggle:
        case FxFieldType::Combo:
            At<int>(data, field.offset) = handler->Load<int>(key, At<int>(defaults, field.offset));
            break;
        case FxFieldType::Float2:
            At<Vector2>(data, field.offset) = handler->Load<Vector2>(key, At<Vector2>(defaults, field.offset));
            break;
        case FxFieldType::Float3:
        case FxFieldType::Color3:
            At<Vector3>(data, field.offset) = handler->Load<Vector3>(key, At<Vector3>(defaults, field.offset));
            break;
        case FxFieldType::Color4:
            At<Vector4>(data, field.offset) = handler->Load<Vector4>(key, At<Vector4>(defaults, field.offset));
            break;
        default:
            break;
        }
    }
}

void ResetToDefaults(std::span<const FxField> fields, void *data, const void *defaults)
{
    for (const FxField &field : fields)
    {
        const size_t size = ValueSize(field.type);
        if (size == 0)
        {
            continue;
        }
        std::memcpy(static_cast<char *>(data) + field.offset, static_cast<const char *>(defaults) + field.offset, size);
    }
}

} // namespace FxFields
} // namespace Hagine
