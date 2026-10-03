#include "InputActions.h"
#include "Input.h"
#include <algorithm>
#include <asset/AssetPath.h>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#ifdef USE_IMGUI
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>
#include <map>
#include <utility/debug/imgui/ImGuiNotification.h>
// DebugUIHelper.h は ImVec4 / ImGui:: を使うので imgui.h の後に include する
#include <utility/debug/imgui/DebugUIHelper.h>
#endif // USE_IMGUI

namespace Hagine {
namespace {
/// <summary>軸の割り当てをボタンとして見たときに「押している」とみなす大きさ</summary>
constexpr float kAxisPressThreshold = 0.5f;

/// <summary>パッドのボタン（XINPUT_GAMEPAD_）と表示名</summary>
struct PadButtonName
{
    WORD bit;
    const char *name;
};
constexpr PadButtonName kPadButtons[] = {
    {XINPUT_GAMEPAD_A, "A"},
    {XINPUT_GAMEPAD_B, "B"},
    {XINPUT_GAMEPAD_X, "X"},
    {XINPUT_GAMEPAD_Y, "Y"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
    {XINPUT_GAMEPAD_BACK, "Back"},
    {XINPUT_GAMEPAD_START, "Start"},
    {XINPUT_GAMEPAD_LEFT_THUMB, "左スティック押し込み"},
    {XINPUT_GAMEPAD_RIGHT_THUMB, "右スティック押し込み"},
    {XINPUT_GAMEPAD_DPAD_UP, "十字キー ↑"},
    {XINPUT_GAMEPAD_DPAD_DOWN, "十字キー ↓"},
    {XINPUT_GAMEPAD_DPAD_LEFT, "十字キー ←"},
    {XINPUT_GAMEPAD_DPAD_RIGHT, "十字キー →"},
};

/// <summary>パッドの軸の値（-1〜1。トリガーは 0〜1）</summary>
float PadAxisValue(InputPadAxis axis)
{
    const GamePad *pad = Input::GetInstance()->GetGamePad();
    if (!pad)
    {
        return 0.0f;
    }
    switch (axis)
    {
    case InputPadAxis::LeftX:
        return pad->GetLeftStickX();
    case InputPadAxis::LeftY:
        return pad->GetLeftStickY();
    case InputPadAxis::RightX:
        return pad->GetRightStickX();
    case InputPadAxis::RightY:
        return pad->GetRightStickY();
    case InputPadAxis::LeftTrigger:
        return pad->GetLeftTrigger();
    case InputPadAxis::RightTrigger:
        return pad->GetRightTrigger();
    default:
        return 0.0f;
    }
}

/// <summary>DIK（スキャンコード）からキーの名前を引く（Windows が配列に合わせた名前を返す）</summary>
std::string KeyName(uint16_t dik)
{
    // 名前が分かりにくい・取れないキーだけ先に決めておく
    switch (dik)
    {
    case DIK_UP:
        return "↑";
    case DIK_DOWN:
        return "↓";
    case DIK_LEFT:
        return "←";
    case DIK_RIGHT:
        return "→";
    case DIK_SPACE:
        return "Space";
    case DIK_RETURN:
        return "Enter";
    case DIK_ESCAPE:
        return "Esc";
    case DIK_LSHIFT:
        return "左Shift";
    case DIK_RSHIFT:
        return "右Shift";
    case DIK_LCONTROL:
        return "左Ctrl";
    case DIK_RCONTROL:
        return "右Ctrl";
    case DIK_TAB:
        return "Tab";
    case DIK_BACK:
        return "BackSpace";
    default:
        break;
    }
    // DIK の値はスキャンコード。0x80 以上は拡張キー（右側の Ctrl・矢印など）
    LONG lParam = static_cast<LONG>(dik & 0x7F) << 16;
    if (dik & 0x80)
    {
        lParam |= 1 << 24;
    }
    wchar_t buffer[64] = {};
    const int length = GetKeyNameTextW(lParam, buffer, 64);
    if (length <= 0)
    {
        return std::format("キー {}", dik);
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, buffer, length, nullptr, 0, nullptr, nullptr);
    std::string name(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buffer, length, name.data(), size, nullptr, nullptr);
    return name;
}

nlohmann::json BindingToJson(const InputBinding &binding)
{
    return {{"source", static_cast<int>(binding.source)}, {"code", binding.code}, {"scale", binding.scale}};
}

bool BindingFromJson(const nlohmann::json &json, InputBinding &out)
{
    if (!json.is_object() || !json.contains("source") || !json.contains("code"))
    {
        return false;
    }
    const int source = json.value("source", 0);
    if (source < 0 || source > static_cast<int>(InputSource::Mouse))
    {
        return false;
    }
    out.source = static_cast<InputSource>(source);
    out.code = static_cast<uint16_t>(json.value("code", 0));
    out.scale = json.value("scale", 1.0f);
    return true;
}
} // namespace

InputActions *InputActions::GetInstance()
{
    static InputActions instance;
    return &instance;
}

std::string InputActions::SavePath() const
{
    return AssetPath::Json("Settings") + "/InputBindings.json";
}

void InputActions::Register(const InputActionDesc &desc)
{
    if (!loaded_)
    {
        Load();
    }
    if (actions_.find(desc.name) == actions_.end())
    {
        order_.push_back(desc.name);
    }
    Action &action = actions_[desc.name];
    action.desc = desc;
    auto saved = saved_.find(desc.name);
    action.bindings = (saved != saved_.end()) ? saved->second : desc.defaults;
}

void InputActions::Unregister(const std::string &name)
{
    actions_.erase(name);
    order_.erase(std::remove(order_.begin(), order_.end(), name), order_.end());
}

float InputActions::BindingValue(const InputBinding &binding)
{
    Input *input = Input::GetInstance();
    switch (binding.source)
    {
    case InputSource::Key:
        return (binding.code < 256 && input->PushKey(static_cast<BYTE>(binding.code))) ? binding.scale : 0.0f;
    case InputSource::PadButton: {
        const GamePad *pad = input->GetGamePad();
        return (pad && pad->IsPress(static_cast<WORD>(binding.code))) ? binding.scale : 0.0f;
    }
    case InputSource::PadAxis:
        if (binding.code >= static_cast<uint16_t>(InputPadAxis::Count))
        {
            return 0.0f;
        }
        return PadAxisValue(static_cast<InputPadAxis>(binding.code)) * binding.scale;
    case InputSource::Mouse:
        return Input::IsPressMouse(static_cast<int32_t>(binding.code)) ? binding.scale : 0.0f;
    default:
        return 0.0f;
    }
}

void InputActions::Update()
{
    for (auto &[name, action] : actions_)
    {
        action.pressedPrevious = action.pressed;
        float value = 0.0f;
        bool pressed = false;
        for (const InputBinding &binding : action.bindings)
        {
            const float v = BindingValue(binding);
            value += v;
            // ボタンの行動では、向きの合った軸を半分以上倒したら押したとみなす（トリガーをボタンにするなど）
            pressed |= action.desc.isAxis ? false : (v >= kAxisPressThreshold);
        }
        if (action.desc.isAxis)
        {
            action.value = std::clamp(value, -1.0f, 1.0f);
            action.pressed = std::abs(action.value) >= kAxisPressThreshold;
        }
        else
        {
            action.value = pressed ? 1.0f : 0.0f;
            action.pressed = pressed;
        }
    }
}

const InputActions::Action *InputActions::Find(const std::string &name) const
{
    auto it = actions_.find(name);
    return it == actions_.end() ? nullptr : &it->second;
}

bool InputActions::IsPressed(const std::string &name) const
{
    const Action *action = Find(name);
    return action && action->pressed;
}

bool InputActions::IsTriggered(const std::string &name) const
{
    const Action *action = Find(name);
    return action && action->pressed && !action->pressedPrevious;
}

bool InputActions::IsReleased(const std::string &name) const
{
    const Action *action = Find(name);
    return action && !action->pressed && action->pressedPrevious;
}

float InputActions::GetValue(const std::string &name) const
{
    const Action *action = Find(name);
    return action ? action->value : 0.0f;
}

const std::vector<InputBinding> *InputActions::GetBindings(const std::string &name) const
{
    const Action *action = Find(name);
    return action ? &action->bindings : nullptr;
}

void InputActions::SetBindings(const std::string &name, const std::vector<InputBinding> &bindings)
{
    auto it = actions_.find(name);
    if (it != actions_.end())
    {
        it->second.bindings = bindings;
    }
}

void InputActions::ResetToDefault(const std::string &name)
{
    auto it = actions_.find(name);
    if (it != actions_.end())
    {
        it->second.bindings = it->second.desc.defaults;
    }
}

void InputActions::ResetAllToDefault()
{
    for (auto &[name, action] : actions_)
    {
        action.bindings = action.desc.defaults;
    }
}

void InputActions::Save()
{
    // 今登録されている行動に加えて、読み込んだが今は登録されていない行動の割り当ても残す
    // （シーンごとに登録する行動が違っても、別のシーンの設定を消さないため）
    nlohmann::json root = nlohmann::json::object();
    for (const auto &[name, bindings] : saved_)
    {
        if (actions_.find(name) == actions_.end())
        {
            nlohmann::json list = nlohmann::json::array();
            for (const InputBinding &binding : bindings)
                list.push_back(BindingToJson(binding));
            root[name] = list;
        }
    }
    for (const auto &[name, action] : actions_)
    {
        nlohmann::json list = nlohmann::json::array();
        for (const InputBinding &binding : action.bindings)
            list.push_back(BindingToJson(binding));
        root[name] = list;
        saved_[name] = action.bindings;
    }

    const std::filesystem::path path = SavePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path);
    if (file)
    {
        file << root.dump(4);
    }
}

void InputActions::Load()
{
    loaded_ = true;
    saved_.clear();
    std::ifstream file(SavePath());
    if (!file)
    {
        return;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (!root.is_object())
    {
        return;
    }
    for (const auto &[name, list] : root.items())
    {
        if (!list.is_array())
            continue;
        std::vector<InputBinding> bindings;
        for (const nlohmann::json &item : list)
        {
            InputBinding binding;
            if (BindingFromJson(item, binding))
                bindings.push_back(binding);
        }
        saved_[name] = std::move(bindings);
    }
    // 登録済みの行動にも反映する（「読み直す」ボタン用）
    for (auto &[name, action] : actions_)
    {
        auto saved = saved_.find(name);
        if (saved != saved_.end())
            action.bindings = saved->second;
    }
}

std::string InputActions::BindingName(const InputBinding &binding)
{
    switch (binding.source)
    {
    case InputSource::Key:
        return KeyName(binding.code);
    case InputSource::PadButton:
        for (const PadButtonName &button : kPadButtons)
        {
            if (button.bit == binding.code)
                return std::string("パッド ") + button.name;
        }
        return std::format("パッド {:#x}", binding.code);
    case InputSource::PadAxis: {
        const bool negative = binding.scale < 0.0f;
        switch (static_cast<InputPadAxis>(binding.code))
        {
        case InputPadAxis::LeftX:
            return negative ? "左スティック ←" : "左スティック →";
        case InputPadAxis::LeftY:
            return negative ? "左スティック ↓" : "左スティック ↑";
        case InputPadAxis::RightX:
            return negative ? "右スティック ←" : "右スティック →";
        case InputPadAxis::RightY:
            return negative ? "右スティック ↓" : "右スティック ↑";
        case InputPadAxis::LeftTrigger:
            return "LT";
        case InputPadAxis::RightTrigger:
            return "RT";
        default:
            return "パッドの軸";
        }
    }
    case InputSource::Mouse: {
        static const char *kMouseNames[] = {"マウス左", "マウス右", "マウス中"};
        return binding.code < 3 ? kMouseNames[binding.code] : std::format("マウス {}", binding.code);
    }
    default:
        return "?";
    }
}

#ifdef USE_IMGUI
bool InputActions::CaptureNextInput(InputBinding &outBinding)
{
    Input *input = Input::GetInstance();
    // キー（Esc は取り消しに使うので割り当てない）
    for (int key = 1; key < 256; ++key)
    {
        if (key != DIK_ESCAPE && input->TriggerKey(static_cast<BYTE>(key)))
        {
            outBinding = {InputSource::Key, static_cast<uint16_t>(key), listeningScale_};
            return true;
        }
    }
    // パッドのボタン
    if (const GamePad *pad = input->GetGamePad())
    {
        for (const PadButtonName &button : kPadButtons)
        {
            if (pad->IsTrigger(button.bit))
            {
                outBinding = {InputSource::PadButton, button.bit, listeningScale_};
                return true;
            }
        }
    }
    // パッドの軸（大きく倒した向き）
    for (int axis = 0; axis < static_cast<int>(InputPadAxis::Count); ++axis)
    {
        const float value = PadAxisValue(static_cast<InputPadAxis>(axis));
        if (std::abs(value) >= 0.7f)
        {
            outBinding = {InputSource::PadAxis, static_cast<uint16_t>(axis), value < 0.0f ? -1.0f : 1.0f};
            return true;
        }
    }
    // マウス（右・中だけ。左は窓のボタンを押すのに使うので拾わない）
    for (int button = 1; button < 3; ++button)
    {
        if (Input::IsTriggerMouse(button))
        {
            outBinding = {InputSource::Mouse, static_cast<uint16_t>(button), listeningScale_};
            return true;
        }
    }
    return false;
}

void InputActions::DrawImGui()
{
    // ---- 全体の操作 ----
    if (ConfirmButton(ICON_FA_SAVE " 保存"))
    {
        Save();
        dirty_ = false;
        ImGuiNotification::Post("入力の割り当てを保存しました", {0.45f, 0.68f, 0.52f, 1.0f});
    }
    ImGui::SetItemTooltip("jsons/Settings/InputBindings.json に保存します（次の起動から既定より優先されます）");
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_UPLOAD " 読み直す"))
    {
        Load();
        dirty_ = false;
    }
    ImGui::SameLine();
    if (NeutralButton(ICON_FA_UNDO " 全部既定に戻す"))
    {
        ResetAllToDefault();
        dirty_ = true;
    }
    if (dirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(DebugTheme::kAccentYellow, "保存していない変更あり");
    }
    DimText("割り当てを押すと付け替え（次に押したキー・ボタン・スティック。Esc で取り消し）/ 右クリックで外す / ＋で足す");

    if (order_.empty())
    {
        ImGui::Spacing();
        DimText("登録された行動がありません。ゲーム側で InputActions::Register を呼ぶとここに並びます");
        return;
    }

    // ---- 付け替えの待ち受け ----
    if (!listeningAction_.empty())
    {
        ++listeningFrames_;
        InputBinding captured;
        if (Input::GetInstance()->TriggerKey(DIK_ESCAPE))
        {
            listeningAction_.clear();
        }
        else if (listeningFrames_ > 1 && CaptureNextInput(captured))
        {
            auto it = actions_.find(listeningAction_);
            if (it != actions_.end())
            {
                std::vector<InputBinding> &bindings = it->second.bindings;
                if (listeningSlot_ >= 0 && listeningSlot_ < static_cast<int>(bindings.size()))
                    bindings[listeningSlot_] = captured;
                else if (std::find(bindings.begin(), bindings.end(), captured) == bindings.end())
                    bindings.push_back(captured);
                dirty_ = true;
            }
            listeningAction_.clear();
        }
    }

    // 同じ見出しの中で、同じ入力が2つ以上の行動に付いていたら知らせる
    std::map<std::string, std::map<std::string, int>> usage; // 見出し → 割り当ての名前 → 使っている数
    for (const std::string &name : order_)
    {
        const Action &action = actions_[name];
        for (const InputBinding &binding : action.bindings)
            ++usage[action.desc.category][BindingName(binding)];
    }

    // 見出しごとにまとめる（登録した順を保つ）
    std::vector<std::string> categories;
    for (const std::string &name : order_)
    {
        const std::string &category = actions_[name].desc.category;
        if (std::find(categories.begin(), categories.end(), category) == categories.end())
            categories.push_back(category);
    }

    for (const std::string &category : categories)
    {
        ImGui::SeparatorText(category.c_str());
        if (!ImGui::BeginTable(("##inputActions" + category).c_str(), 3,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
            continue;
        ImGui::TableSetupColumn("行動", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("割り当て", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        for (const std::string &name : order_)
        {
            Action &action = actions_[name];
            if (action.desc.category != category)
                continue;
            ImGui::PushID(name.c_str());
            ImGui::TableNextRow();

            // ---- 行動の名前と今の状態（押している間は光る。軸は棒で値を出す）----
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const ImVec4 lit = action.pressed ? DebugTheme::kAccentGreen : DebugTheme::kTextDim;
            ImGui::TextColored(lit, "%s", action.pressed ? ICON_FA_CIRCLE : ICON_FA_DOT_CIRCLE);
            ImGui::SameLine();
            ImGui::TextUnformatted(action.desc.label.empty() ? name.c_str() : action.desc.label.c_str());
            ImGui::SetItemTooltip("コードでの名前: %s%s", name.c_str(), action.desc.isAxis ? "（軸: -1〜1）" : "");
            if (action.desc.isAxis)
            {
                // 真ん中から左右へ伸びる棒（-1 で左端、+1 で右端）
                const ImVec2 barMin = ImGui::GetCursorScreenPos();
                const float barWidth = ImGui::GetContentRegionAvail().x;
                const ImVec2 barMax = ImVec2(barMin.x + barWidth, barMin.y + 4.0f);
                const float center = barMin.x + barWidth * 0.5f;
                ImDrawList *drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(barMin, barMax, ImGui::GetColorU32(ImGuiCol_FrameBg), 2.0f);
                const float end = center + barWidth * 0.5f * action.value;
                drawList->AddRectFilled(ImVec2((std::min)(center, end), barMin.y), ImVec2((std::max)(center, end), barMax.y),
                                        ImGui::ColorConvertFloat4ToU32(DebugTheme::kAccentGreen), 2.0f);
                drawList->AddLine(ImVec2(center, barMin.y - 1.0f), ImVec2(center, barMax.y + 1.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::Dummy(ImVec2(barWidth, 4.0f));
            }

            // ---- 割り当て（押すと付け替え・右クリックで外す）----
            ImGui::TableNextColumn();
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(action.bindings.size()); ++i)
            {
                const InputBinding &binding = action.bindings[i];
                ImGui::PushID(i);
                const bool listening = (listeningAction_ == name && listeningSlot_ == i);
                std::string label = listening ? std::string("… 押してください") : BindingName(binding);
                if (action.desc.isAxis && binding.source != InputSource::PadAxis)
                    label += binding.scale < 0.0f ? " (-)" : " (+)";
                const bool conflict = !listening && usage[category][BindingName(binding)] > 1;
                {
                    ScopedButtonColors colors(listening ? DebugTheme::kButtonPrimary : (conflict ? DebugTheme::kButtonDanger : DebugTheme::kButtonNeutral),
                                              listening ? DebugTheme::kButtonPrimaryHover : (conflict ? DebugTheme::kButtonDangerHover : DebugTheme::kButtonNeutralHover));
                    if (ImGui::Button(label.c_str()))
                    {
                        listeningAction_ = name;
                        listeningSlot_ = i;
                        listeningScale_ = binding.scale;
                        listeningFrames_ = 0;
                    }
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    removeIndex = i;
                if (conflict)
                    ImGui::SetItemTooltip("同じ見出しの別の行動にも付いています");
                ImGui::SameLine();
                ImGui::PopID();
            }
            if (removeIndex >= 0)
            {
                action.bindings.erase(action.bindings.begin() + removeIndex);
                dirty_ = true;
            }

            // 足す（軸の行動は向きを選んでから押す）
            const bool listeningNew = (listeningAction_ == name && listeningSlot_ >= static_cast<int>(action.bindings.size()));
            if (listeningNew)
            {
                ImGui::TextColored(DebugTheme::kAccentBlue, "… 押してください");
            }
            else if (action.desc.isAxis)
            {
                if (ImGui::SmallButton("＋(+)"))
                {
                    listeningAction_ = name;
                    listeningSlot_ = static_cast<int>(action.bindings.size());
                    listeningScale_ = 1.0f;
                    listeningFrames_ = 0;
                }
                ImGui::SetItemTooltip("押すと値を増やす入力を足す（スティックはそのまま向きごと拾う）");
                ImGui::SameLine();
                if (ImGui::SmallButton("＋(-)"))
                {
                    listeningAction_ = name;
                    listeningSlot_ = static_cast<int>(action.bindings.size());
                    listeningScale_ = -1.0f;
                    listeningFrames_ = 0;
                }
                ImGui::SetItemTooltip("押すと値を減らす入力を足す");
            }
            else if (ImGui::SmallButton("＋"))
            {
                listeningAction_ = name;
                listeningSlot_ = static_cast<int>(action.bindings.size());
                listeningScale_ = 1.0f;
                listeningFrames_ = 0;
            }

            // ---- 既定に戻す ----
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(action.bindings == action.desc.defaults);
            {
                ScopedButtonColors colors(DebugTheme::kButtonGhost, DebugTheme::kButtonGhostHover);
                if (ImGui::Button(ICON_FA_UNDO "##reset", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                {
                    ResetToDefault(name);
                    dirty_ = true;
                }
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip("この行動を既定の割り当てに戻す");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
#endif // USE_IMGUI

} // namespace Hagine
