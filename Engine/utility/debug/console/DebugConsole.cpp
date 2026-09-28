#include "DebugConsole.h"

#include <Audio.h>
#include <Frame.h>
#include <debug/capture/CaptureManager.h>
#include <debug/log/Logger.h>
#include <music/MusicEngine.h>
#include <scene/SceneManager.h>
#include <scene/SceneRegistry.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace Hagine {
namespace {

constexpr Vector4 kColorNormal = {0.85f, 0.87f, 0.90f, 1.0f};
constexpr Vector4 kColorEcho = {0.55f, 0.70f, 0.90f, 1.0f};
constexpr Vector4 kColorError = {0.90f, 0.45f, 0.45f, 1.0f};
constexpr Vector4 kColorOk = {0.50f, 0.80f, 0.55f, 1.0f};

/// 文字列を小文字にそろえる（コマンド名は大文字小文字を区別しない）
std::string ToLower(const std::string &text)
{
    std::string result = text;
    for (char &ch : result)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return result;
}

/// "true"/"1"/"on" を true として読む
bool ParseBool(const std::string &text)
{
    const std::string lower = ToLower(text);
    return lower == "1" || lower == "true" || lower == "on" || lower == "yes";
}

} // namespace

DebugConsole *DebugConsole::GetInstance()
{
    static DebugConsole instance;
    return &instance;
}

void DebugConsole::Initialize()
{
    if (initialized_)
        return;
    initialized_ = true;
    RegisterBuiltInCommands();
    Print("デバッグコンソール。help でコマンド一覧、vars で変数一覧が出ます。", kColorOk);
}

void DebugConsole::Finalize()
{
    commands_.clear();
    variables_.clear();
    lines_.clear();
    history_.clear();
    initialized_ = false;
}

void DebugConsole::RegisterCommand(const std::string &name, const std::string &help, CommandFunc func)
{
    commands_[ToLower(name)] = Command{help, std::move(func)};
}

void DebugConsole::RegisterFloat(const std::string &name, float *target, const std::string &help)
{
    variables_[ToLower(name)] = Variable{Variable::Type::Float, target, help};
}

void DebugConsole::RegisterInt(const std::string &name, int *target, const std::string &help)
{
    variables_[ToLower(name)] = Variable{Variable::Type::Int, target, help};
}

void DebugConsole::RegisterBool(const std::string &name, bool *target, const std::string &help)
{
    variables_[ToLower(name)] = Variable{Variable::Type::Bool, target, help};
}

void DebugConsole::UnregisterVariable(const std::string &name)
{
    variables_.erase(ToLower(name));
}

void DebugConsole::Print(const std::string &text, const Vector4 &color)
{
    lines_.push_back({text, color});
    // 無制限に溜めると重くなるので、古い行から捨てる
    if (lines_.size() > 500)
        lines_.erase(lines_.begin(), lines_.begin() + 100);
    scrollToBottom_ = true;
}

std::vector<std::string> DebugConsole::Tokenize(const std::string &line)
{
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token)
        tokens.push_back(token);
    return tokens;
}

std::string DebugConsole::FormatVariable(const Variable &variable) const
{
    if (variable.target == nullptr)
        return "(無効)";
    switch (variable.type)
    {
    case Variable::Type::Float:
        return std::to_string(*static_cast<float *>(variable.target));
    case Variable::Type::Int:
        return std::to_string(*static_cast<int *>(variable.target));
    case Variable::Type::Bool:
        return (*static_cast<bool *>(variable.target)) ? "true" : "false";
    }
    return "";
}

void DebugConsole::Execute(const std::string &line)
{
    if (line.empty())
        return;

    Print("> " + line, kColorEcho);
    history_.push_back(line);
    historyCursor_ = -1;

    const std::vector<std::string> args = Tokenize(line);
    if (args.empty())
        return;

    const auto it = commands_.find(ToLower(args[0]));
    if (it == commands_.end())
    {
        Print("知らないコマンドです: " + args[0] + "  （help で一覧）", kColorError);
        return;
    }
    it->second.func(args);
}

std::vector<std::string> DebugConsole::CollectCandidates(const std::string &prefix) const
{
    std::vector<std::string> candidates;
    const std::string lower = ToLower(prefix);
    for (const auto &[name, command] : commands_)
    {
        if (name.rfind(lower, 0) == 0)
            candidates.push_back(name);
    }
    return candidates;
}

void DebugConsole::RegisterBuiltInCommands()
{
    RegisterCommand("help", "コマンド一覧を出す。help <名前> で1つだけ詳しく出す",
                    [this](const std::vector<std::string> &args) {
                        if (args.size() >= 2)
                        {
                            const auto it = commands_.find(ToLower(args[1]));
                            if (it == commands_.end())
                            {
                                Print("知らないコマンドです: " + args[1], kColorError);
                                return;
                            }
                            Print(it->first + " : " + it->second.help);
                            return;
                        }
                        Print("--- コマンド一覧 ---", kColorOk);
                        for (const auto &[name, command] : commands_)
                            Print("  " + name + " : " + command.help);
                    });

    RegisterCommand("clear", "表示を消す", [this](const std::vector<std::string> &) { lines_.clear(); });

    RegisterCommand("vars", "登録されている変数の一覧と現在値を出す",
                    [this](const std::vector<std::string> &) {
                        if (variables_.empty())
                        {
                            Print("登録された変数はありません");
                            return;
                        }
                        Print("--- 変数一覧 ---", kColorOk);
                        for (const auto &[name, variable] : variables_)
                        {
                            std::string text = "  " + name + " = " + FormatVariable(variable);
                            if (!variable.help.empty())
                                text += "   (" + variable.help + ")";
                            Print(text);
                        }
                    });

    RegisterCommand("get", "変数の値を出す。 get <名前>", [this](const std::vector<std::string> &args) {
        if (args.size() < 2)
        {
            Print("使い方: get <名前>", kColorError);
            return;
        }
        const auto it = variables_.find(ToLower(args[1]));
        if (it == variables_.end())
        {
            Print("知らない変数です: " + args[1], kColorError);
            return;
        }
        Print(it->first + " = " + FormatVariable(it->second));
    });

    RegisterCommand("set", "変数を書き換える。 set <名前> <値>", [this](const std::vector<std::string> &args) {
        if (args.size() < 3)
        {
            Print("使い方: set <名前> <値>", kColorError);
            return;
        }
        const auto it = variables_.find(ToLower(args[1]));
        if (it == variables_.end())
        {
            Print("知らない変数です: " + args[1], kColorError);
            return;
        }
        Variable &variable = it->second;
        if (variable.target == nullptr)
        {
            Print("この変数は今は使えません", kColorError);
            return;
        }
        switch (variable.type)
        {
        case Variable::Type::Float:
            *static_cast<float *>(variable.target) = std::strtof(args[2].c_str(), nullptr);
            break;
        case Variable::Type::Int:
            *static_cast<int *>(variable.target) = std::atoi(args[2].c_str());
            break;
        case Variable::Type::Bool:
            *static_cast<bool *>(variable.target) = ParseBool(args[2]);
            break;
        }
        Print(it->first + " = " + FormatVariable(variable), kColorOk);
    });

    RegisterCommand("scenes", "登録されているシーン名の一覧を出す", [this](const std::vector<std::string> &) {
        const std::vector<std::string> names = SceneRegistry::GetInstance()->GetSceneNames();
        Print("現在: " + SceneManager::GetInstance()->GetCurrentSceneName(), kColorOk);
        for (const std::string &name : names)
            Print("  " + name);
    });

    RegisterCommand("scene", "シーンを切り替える。 scene <名前>", [this](const std::vector<std::string> &args) {
        if (args.size() < 2)
        {
            Print("使い方: scene <名前>   （scenes で一覧）", kColorError);
            return;
        }
        const std::vector<std::string> names = SceneRegistry::GetInstance()->GetSceneNames();
        // 大文字小文字の違いは吸収する。打ちやすさのほうが大事
        for (const std::string &name : names)
        {
            if (ToLower(name) == ToLower(args[1]))
            {
                SceneManager::GetInstance()->NextSceneReservation(name);
                Print("シーンを切り替えます: " + name, kColorOk);
                return;
            }
        }
        Print("知らないシーンです: " + args[1], kColorError);
    });

    RegisterCommand("fps", "フレームレートと経過時間を出す", [this](const std::vector<std::string> &) {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "FPS %.1f / 1フレーム %.2f ms / 起動から %.1f 秒",
                 Frame::GetFPS(), Frame::UnscaledDeltaTime() * 1000.0f, Frame::Time());
        Print(buffer);
    });

    RegisterCommand("volume", "音量を変える。 volume <master|bgm|se|voice> <0〜1>",
                    [this](const std::vector<std::string> &args) {
                        if (args.size() < 3)
                        {
                            Print("使い方: volume <master|bgm|se|voice> <0〜1>", kColorError);
                            return;
                        }
                        const float value = std::clamp(std::strtof(args[2].c_str(), nullptr), 0.0f, 1.0f);
                        const std::string target = ToLower(args[1]);
                        Audio *audio = Audio::GetInstance();
                        if (target == "master")
                            audio->SetMasterVolume(value);
                        else if (target == "bgm")
                            audio->SetBusVolume(SoundBus::BGM, value);
                        else if (target == "se")
                            audio->SetBusVolume(SoundBus::SE, value);
                        else if (target == "voice")
                            audio->SetBusVolume(SoundBus::Voice, value);
                        else
                        {
                            Print("対象は master / bgm / se / voice のどれかです", kColorError);
                            return;
                        }
                        Print(target + " の音量を " + args[2] + " にしました", kColorOk);
                    });

    RegisterCommand("play", "効果音を鳴らす。 play <sounds からの相対パス>",
                    [this](const std::vector<std::string> &args) {
                        if (args.size() < 2)
                        {
                            Print("使い方: play <player/playerJump.wav のようなパス>", kColorError);
                            return;
                        }
                        const SoundHandle handle = Audio::GetInstance()->PlayOneShot(args[1]);
                        if (handle.IsValid())
                            Print("鳴らしました: " + args[1], kColorOk);
                        else
                            Print("鳴らせませんでした: " + args[1], kColorError);
                    });

    RegisterCommand("bgm", "BGM を切り替える。 bgm <sounds からの相対パス>",
                    [this](const std::vector<std::string> &args) {
                        if (args.size() < 2)
                        {
                            Audio::GetInstance()->StopBus(SoundBus::BGM, 1.0f);
                            Print("BGM を止めました", kColorOk);
                            return;
                        }
                        Audio::GetInstance()->CrossFadeBgm(args[1]);
                        Print("BGM を切り替えました: " + args[1], kColorOk);
                    });

    RegisterCommand("reverb", "残響の入切とプリセット。 reverb <on|off> / reverb preset <番号>",
                    [this](const std::vector<std::string> &args) {
                        Audio *audio = Audio::GetInstance();
                        if (args.size() >= 3 && ToLower(args[1]) == "preset")
                        {
                            audio->SetReverbPreset(std::atoi(args[2].c_str()));
                            Print(std::string("残響: ") + Audio::GetReverbPresetName(audio->GetReverbPreset()), kColorOk);
                            return;
                        }
                        if (args.size() >= 2)
                        {
                            audio->SetReverbEnabled(ParseBool(args[1]));
                        }
                        Print(std::string("残響 ") + (audio->IsReverbEnabled() ? "ON" : "OFF") + " / " +
                                  Audio::GetReverbPresetName(audio->GetReverbPreset()),
                              kColorOk);
                    });

    RegisterCommand("music", "音楽エディタの曲を操作する。 music <play|stop>",
                    [this](const std::vector<std::string> &args) {
                        MusicEngine *engine = MusicEngine::GetInstance();
                        const std::string action = (args.size() >= 2) ? ToLower(args[1]) : "";
                        if (action == "play")
                        {
                            engine->Play();
                            Print("曲を再生しました", kColorOk);
                        }
                        else if (action == "stop")
                        {
                            engine->Stop();
                            Print("曲を止めました", kColorOk);
                        }
                        else
                        {
                            Print("使い方: music <play|stop>", kColorError);
                        }
                    });

    RegisterCommand("shot", "スクリーンショットを撮る", [this](const std::vector<std::string> &) {
        CaptureManager::GetInstance()->RequestScreenshot();
        Print("撮影しました: " + CaptureManager::GetInstance()->GetOutputDirectory(), kColorOk);
    });

    RegisterCommand("rec", "連番録画の開始・停止。 rec <start|stop>",
                    [this](const std::vector<std::string> &args) {
                        CaptureManager *capture = CaptureManager::GetInstance();
                        const std::string action = (args.size() >= 2) ? ToLower(args[1]) : "";
                        if (action == "start")
                        {
                            capture->StartSequence();
                            Print("録画を始めました", kColorOk);
                        }
                        else if (action == "stop")
                        {
                            capture->StopSequence();
                            Print("録画を止めました", kColorOk);
                        }
                        else
                        {
                            Print(std::string("録画中: ") + (capture->IsRecording() ? "はい" : "いいえ") +
                                  "  使い方: rec <start|stop>");
                        }
                    });

    RegisterCommand("echo", "そのまま出力する（動作確認用）", [this](const std::vector<std::string> &args) {
        std::string text;
        for (size_t i = 1; i < args.size(); ++i)
            text += args[i] + " ";
        Print(text);
    });
}

void DebugConsole::DrawImGui(bool *open)
{
#ifdef USE_IMGUI
    if (open && !*open)
        return;

    ImGui::SetNextWindowSize(ImVec2(620.0f, 380.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_TERMINAL " コンソール", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    // 出力欄。入力欄のぶんだけ高さを残す
    const float inputHeight = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##consoleLog", ImVec2(0.0f, -inputHeight), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const Line &line : lines_)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(line.color.x, line.color.y, line.color.z, line.color.w));
        ImGui::TextUnformatted(line.text.c_str());
        ImGui::PopStyleColor();
    }
    if (scrollToBottom_)
    {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom_ = false;
    }
    ImGui::EndChild();

    // 入力欄。履歴の呼び出しと補完はコールバックで処理する
    ImGui::SetNextItemWidth(-1.0f);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                      ImGuiInputTextFlags_CallbackHistory |
                                      ImGuiInputTextFlags_CallbackCompletion;
    auto callback = [](ImGuiInputTextCallbackData *data) -> int {
        DebugConsole *console = static_cast<DebugConsole *>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            if (console->history_.empty())
                return 0;
            const int last = static_cast<int>(console->history_.size()) - 1;
            if (data->EventKey == ImGuiKey_UpArrow)
                console->historyCursor_ = (console->historyCursor_ < 0) ? last
                                                                       : std::max(0, console->historyCursor_ - 1);
            else if (data->EventKey == ImGuiKey_DownArrow && console->historyCursor_ >= 0)
                console->historyCursor_ = (console->historyCursor_ >= last) ? -1 : console->historyCursor_ + 1;

            const std::string text = (console->historyCursor_ >= 0) ? console->history_[console->historyCursor_] : "";
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, text.c_str());
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        {
            // 先頭の単語だけを補う。候補が1つなら確定、複数なら一覧を出す
            std::string current(data->Buf, data->BufTextLen);
            if (current.find(' ') != std::string::npos)
                return 0;
            const std::vector<std::string> candidates = console->CollectCandidates(current);
            if (candidates.empty())
                return 0;
            if (candidates.size() == 1)
            {
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, (candidates[0] + " ").c_str());
            }
            else
            {
                std::string text = "候補:";
                for (const std::string &candidate : candidates)
                    text += " " + candidate;
                console->Print(text);
            }
        }
        return 0;
    };

    if (ImGui::InputTextWithHint("##consoleInput", "コマンドを入力 (Tab で補完 / ↑↓ で履歴)", &inputBuffer_,
                                 flags, callback, this))
    {
        Execute(inputBuffer_);
        inputBuffer_.clear();
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::End();
#else
    (void)open;
#endif // USE_IMGUI
}

} // namespace Hagine
