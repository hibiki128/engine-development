#include "EditorCommandPalette.h"
#ifdef USE_IMGUI
#include <algorithm>
#include <icon/IconsFontAwesome5.h>
#include <imgui.h>

namespace Hagine {

namespace {

/// <summary>種類ごとの表示名と色（一覧の左の帯・アイコン・右端の札に使う）</summary>
struct KindStyle
{
    const char *name;
    ImVec4 color;
};

KindStyle GetKindStyle(EditorCommand::Kind kind)
{
    switch (kind)
    {
    case EditorCommand::Kind::Action:
        return {"操作", ImVec4(0.53f, 0.68f, 0.85f, 1.0f)};
    case EditorCommand::Kind::Window:
        return {"ウィンドウ", ImVec4(0.48f, 0.78f, 0.58f, 1.0f)};
    case EditorCommand::Kind::Workspace:
        return {"ワークスペース", ImVec4(0.72f, 0.58f, 0.90f, 1.0f)};
    case EditorCommand::Kind::Object:
        return {"オブジェクト", ImVec4(0.90f, 0.66f, 0.40f, 1.0f)};
    case EditorCommand::Kind::Scene:
        return {"シーン", ImVec4(0.40f, 0.80f, 0.80f, 1.0f)};
    case EditorCommand::Kind::Setting:
        return {"外観", ImVec4(0.88f, 0.55f, 0.70f, 1.0f)};
    case EditorCommand::Kind::Asset:
        return {"アセット", ImVec4(0.80f, 0.72f, 0.42f, 1.0f)};
    }
    return {"", ImVec4(1.0f, 1.0f, 1.0f, 1.0f)};
}

/// <summary>
/// UTF-8 を1文字ずつ読む。読んだコードポイントを返し、p を次の文字へ進める
/// </summary>
uint32_t DecodeUtf8(const char *&p, const char *end)
{
    const unsigned char c = static_cast<unsigned char>(*p);
    uint32_t cp = 0;
    int extra = 0;
    if (c < 0x80)
    {
        cp = c;
    }
    else if ((c >> 5) == 0x6)
    {
        cp = c & 0x1F;
        extra = 1;
    }
    else if ((c >> 4) == 0xE)
    {
        cp = c & 0x0F;
        extra = 2;
    }
    else if ((c >> 3) == 0x1E)
    {
        cp = c & 0x07;
        extra = 3;
    }
    ++p;
    for (int i = 0; i < extra && p < end; ++i, ++p)
    {
        cp = (cp << 6) | (static_cast<unsigned char>(*p) & 0x3F);
    }
    return cp;
}

/// <summary>比較用に文字をそろえる（英字は小文字、ひらがなはカタカナへ）</summary>
uint32_t NormalizeCodepoint(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z')
    {
        return cp - 'A' + 'a';
    }
    // 全角英数も半角と同じに扱う（Ａ→a）
    if (cp >= 0xFF21 && cp <= 0xFF3A)
    {
        return cp - 0xFF21 + 'a';
    }
    if (cp >= 0xFF41 && cp <= 0xFF5A)
    {
        return cp - 0xFF41 + 'a';
    }
    // 「ぱーてぃくる」で「パーティクル」に当たるように、ひらがなはカタカナへ寄せる
    if (cp >= 0x3041 && cp <= 0x3096)
    {
        return cp + 0x60;
    }
    return cp;
}

/// <summary>単語の頭か（直前が区切り文字、または英字とそれ以外の境目）</summary>
bool IsWordStart(const std::vector<uint32_t> &text, size_t index)
{
    if (index == 0)
    {
        return true;
    }
    const uint32_t prev = text[index - 1];
    if (prev == ' ' || prev == '_' || prev == '-' || prev == '/' || prev == '(' || prev == 0x3000 ||
        prev == 0xFF08 || prev == 0x30FB)
    {
        return true;
    }
    const bool prevAscii = prev < 0x80;
    const bool curAscii = text[index] < 0x80;
    return prevAscii != curAscii;
}

} // namespace

void EditorCommandPalette::Open()
{
    openRequested_ = true;
}

void EditorCommandPalette::Toggle()
{
    if (isOpen_)
    {
        // 閉じるのは次の Draw の中で行う（ImGui のポップアップは CloseCurrentPopup で閉じないと残る）
        closeRequested_ = true;
        openRequested_ = false;
        return;
    }
    Open();
}

std::vector<uint32_t> EditorCommandPalette::ToLowerCodepoints(const std::string &text)
{
    std::vector<uint32_t> result;
    result.reserve(text.size());
    const char *p = text.data();
    const char *end = p + text.size();
    while (p < end)
    {
        result.push_back(NormalizeCodepoint(DecodeUtf8(p, end)));
    }
    return result;
}

std::string EditorCommandPalette::MakeKey(const EditorCommand &command)
{
    return std::to_string(static_cast<int>(command.kind)) + ":" + command.label;
}

int EditorCommandPalette::FuzzyScore(const std::vector<uint32_t> &query, const std::string &target, std::vector<int> *outMatched)
{
    if (query.empty())
    {
        return 0;
    }
    const std::vector<uint32_t> text = ToLowerCodepoints(target);
    if (text.size() < query.size())
    {
        return -1;
    }

    // ---- まず「ひと続きで含まれるか」。含まれるなら一番強い一致として扱う ----
    auto it = std::search(text.begin(), text.end(), query.begin(), query.end());
    if (it != text.end())
    {
        const int pos = static_cast<int>(it - text.begin());
        if (outMatched)
        {
            outMatched->clear();
            for (int i = 0; i < static_cast<int>(query.size()); ++i)
            {
                outMatched->push_back(pos + i);
            }
        }
        int score = 1000 - pos * 2 - static_cast<int>(text.size() - query.size());
        if (pos == 0)
        {
            score += 300; // 先頭一致
        }
        else if (IsWordStart(text, static_cast<size_t>(pos)))
        {
            score += 150; // 単語の頭から一致
        }
        return score;
    }

    // ---- 飛び飛びでも順番どおりに現れれば一致（「pts」で「パーティクル設定」…のような使い方）----
    std::vector<int> matched;
    matched.reserve(query.size());
    int score = 0;
    size_t qi = 0;
    int lastMatch = -2;
    for (size_t ti = 0; ti < text.size() && qi < query.size(); ++ti)
    {
        if (text[ti] != query[qi])
        {
            continue;
        }
        score += 10;
        if (static_cast<int>(ti) == lastMatch + 1)
        {
            score += 15; // 続けて一致
        }
        if (IsWordStart(text, ti))
        {
            score += 20; // 単語の頭
        }
        matched.push_back(static_cast<int>(ti));
        lastMatch = static_cast<int>(ti);
        ++qi;
    }
    if (qi < query.size())
    {
        return -1;
    }
    // 広く散らばった一致ほど弱くする
    score -= (matched.back() - matched.front()) - static_cast<int>(query.size());
    if (outMatched)
    {
        *outMatched = std::move(matched);
    }
    return score;
}

void EditorCommandPalette::Filter(const std::vector<EditorCommand> &commands)
{
    matches_.clear();

    // 先頭の記号で種類を絞る（> 操作 / # ウィンドウ / @ オブジェクト / ! シーン / $ アセット）
    std::string text = query_;
    bool assetPrefix = false;
    auto kindAllowed = [](EditorCommand::Kind) { return true; };
    std::function<bool(EditorCommand::Kind)> allowed = kindAllowed;
    if (!text.empty())
    {
        switch (text[0])
        {
        case '>':
            allowed = [](EditorCommand::Kind k) {
                return k == EditorCommand::Kind::Action || k == EditorCommand::Kind::Setting;
            };
            text.erase(0, 1);
            break;
        case '#':
            allowed = [](EditorCommand::Kind k) {
                return k == EditorCommand::Kind::Window || k == EditorCommand::Kind::Workspace;
            };
            text.erase(0, 1);
            break;
        case '@':
            allowed = [](EditorCommand::Kind k) { return k == EditorCommand::Kind::Object; };
            text.erase(0, 1);
            break;
        case '!':
            allowed = [](EditorCommand::Kind k) { return k == EditorCommand::Kind::Scene; };
            text.erase(0, 1);
            break;
        case '$':
            allowed = [](EditorCommand::Kind k) { return k == EditorCommand::Kind::Asset; };
            text.erase(0, 1);
            assetPrefix = true;
            break;
        default:
            break;
        }
    }
    // 前後の空白は無視する
    while (!text.empty() && text.front() == ' ')
    {
        text.erase(text.begin());
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.pop_back();
    }
    const std::vector<uint32_t> query = ToLowerCodepoints(text);

    // アセットは数百あるので、何も打っていない間は一覧に出さない（$ で明示したときは出す）
    if (text.empty() && !assetPrefix)
    {
        allowed = [previous = allowed](EditorCommand::Kind k) { return k != EditorCommand::Kind::Asset && previous(k); };
    }

    auto recentRank = [&](const EditorCommand &command) -> int {
        const std::string key = MakeKey(command);
        for (size_t i = 0; i < recent_.size(); ++i)
        {
            if (recent_[i] == key)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    };

    for (int i = 0; i < static_cast<int>(commands.size()); ++i)
    {
        const EditorCommand &command = commands[i];
        if (!allowed(command.kind))
        {
            continue;
        }

        Match match;
        match.index = i;
        const int rank = recentRank(command);

        if (query.empty())
        {
            // 何も打っていないときは最近使った順、その後ろに渡された順
            match.score = (rank >= 0) ? 100000 - rank : -i;
        }
        else
        {
            int score = FuzzyScore(query, command.label, &match.matched);
            if (score < 0)
            {
                // 名前で当たらなければ補足・種類名でも探す（強調はしない）
                const int sub = FuzzyScore(query, command.hint + " " + GetKindStyle(command.kind).name, nullptr);
                if (sub < 0)
                {
                    continue;
                }
                score = sub / 3;
                match.matched.clear();
            }
            if (rank >= 0)
            {
                score += 60 - rank * 5;
            }
            match.score = score;
        }
        matches_.push_back(std::move(match));
    }

    std::stable_sort(matches_.begin(), matches_.end(), [](const Match &a, const Match &b) { return a.score > b.score; });

    if (matches_.empty())
    {
        selected_ = 0;
    }
    else
    {
        selected_ = std::clamp(selected_, 0, static_cast<int>(matches_.size()) - 1);
    }
}

bool EditorCommandPalette::DrawRow(const EditorCommand &command, const Match &match, bool selected)
{
    const float rowHeight = ImGui::GetFrameHeight() + 6.0f;
    const bool clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight));

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 rectMin = ImGui::GetItemRectMin();
    const ImVec2 rectMax = ImGui::GetItemRectMax();
    const float textY = rectMin.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
    const KindStyle kindStyle = GetKindStyle(command.kind);

    // 左端の色帯（種類が一目で分かる）
    drawList->AddRectFilled(ImVec2(rectMin.x, rectMin.y + 4.0f), ImVec2(rectMin.x + 3.0f, rectMax.y - 4.0f),
                            ImGui::GetColorU32(kindStyle.color), 1.5f);

    float x = rectMin.x + 12.0f;
    // アイコン
    const float iconWidth = ImGui::GetFontSize() * 1.5f;
    drawList->AddText(ImVec2(x, textY), ImGui::GetColorU32(kindStyle.color), command.icon.c_str());
    x += iconWidth;

    // 名前。一致した文字だけアクセント色にする
    const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 hitColor = ImGui::GetColorU32(ImVec4(0.98f, 0.82f, 0.45f, 1.0f));
    const char *p = command.label.data();
    const char *end = p + command.label.size();
    int cpIndex = 0;
    size_t matchCursor = 0;
    while (p < end)
    {
        const char *start = p;
        DecodeUtf8(p, end);
        bool isHit = false;
        while (matchCursor < match.matched.size() && match.matched[matchCursor] < cpIndex)
        {
            ++matchCursor;
        }
        if (matchCursor < match.matched.size() && match.matched[matchCursor] == cpIndex)
        {
            isHit = true;
        }
        drawList->AddText(ImVec2(x, textY), isHit ? hitColor : textColor, start, p);
        x += ImGui::CalcTextSize(start, p).x;
        ++cpIndex;
    }

    // 右端: チェック・ショートカット・種類の札（右から詰めて置く）
    float right = rectMax.x - 10.0f;
    const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float kindWidth = ImGui::CalcTextSize(kindStyle.name).x;
    right -= kindWidth;
    drawList->AddText(ImVec2(right, textY), ImGui::GetColorU32(ImVec4(kindStyle.color.x, kindStyle.color.y, kindStyle.color.z, 0.75f)),
                      kindStyle.name);
    right -= 14.0f;
    if (!command.shortcut.empty())
    {
        const ImVec2 size = ImGui::CalcTextSize(command.shortcut.c_str());
        right -= size.x + 10.0f;
        // キーは枠で囲んでキーキャップ風に
        drawList->AddRect(ImVec2(right - 4.0f, textY - 2.0f), ImVec2(right + size.x + 4.0f, textY + size.y + 2.0f), dimColor, 3.0f);
        drawList->AddText(ImVec2(right, textY), dimColor, command.shortcut.c_str());
        right -= 12.0f;
    }
    if (command.checked)
    {
        const ImVec2 size = ImGui::CalcTextSize(ICON_FA_CHECK);
        right -= size.x;
        drawList->AddText(ImVec2(right, textY), ImGui::GetColorU32(kindStyle.color), ICON_FA_CHECK);
        right -= 10.0f;
    }

    // 補足（入る幅だけ薄く）
    if (!command.hint.empty())
    {
        x += 12.0f;
        const float available = right - x - 8.0f;
        if (available > 30.0f)
        {
            ImGui::PushClipRect(ImVec2(x, rectMin.y), ImVec2(x + available, rectMax.y), true);
            drawList->AddText(ImVec2(x, textY), dimColor, command.hint.c_str());
            ImGui::PopClipRect();
        }
    }
    return clicked;
}

void EditorCommandPalette::Execute(const EditorCommand &command)
{
    // 最近使った項目の先頭へ
    const std::string key = MakeKey(command);
    recent_.erase(std::remove(recent_.begin(), recent_.end(), key), recent_.end());
    recent_.push_front(key);
    while (recent_.size() > kMaxRecent)
    {
        recent_.pop_back();
    }
}

void EditorCommandPalette::Draw(const std::vector<EditorCommand> &commands)
{
    constexpr const char *kPopupId = "##EditorCommandPalette";
    if (openRequested_)
    {
        ImGui::OpenPopup(kPopupId);
        openRequested_ = false;
        isOpen_ = true;
        focusInput_ = true;
        query_[0] = '\0';
        selected_ = 0;
    }
    if (!isOpen_)
    {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float width = std::min(720.0f, viewport->WorkSize.x * 0.9f);
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.12f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    // マルチビューポート時に別のOSウィンドウとして切り離されないよう、常にメイン画面の上に出す
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));

    std::function<void()> pending;
    if (ImGui::BeginPopup(kPopupId, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        // ---- 検索欄 ----
        ImGui::TextColored(ImVec4(0.60f, 0.70f, 0.85f, 1.0f), ICON_FA_SEARCH);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (focusInput_)
        {
            ImGui::SetKeyboardFocusHere();
            focusInput_ = false;
        }
        const bool entered = ImGui::InputTextWithHint("##query", "ウィンドウ・操作・オブジェクト・シーンを検索…", query_, sizeof(query_),
                                                      ImGuiInputTextFlags_EnterReturnsTrue);
        if (ImGui::IsItemEdited())
        {
            selected_ = 0;
        }

        Filter(commands);

        // ---- キー操作 ----
        const int count = static_cast<int>(matches_.size());
        if (count > 0)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                selected_ = (selected_ + 1) % count;
                scrollToSelection_ = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                selected_ = (selected_ + count - 1) % count;
                scrollToSelection_ = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown))
            {
                selected_ = std::min(selected_ + kVisibleRows, count - 1);
                scrollToSelection_ = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp))
            {
                selected_ = std::max(selected_ - kVisibleRows, 0);
                scrollToSelection_ = true;
            }
            if (entered)
            {
                const EditorCommand &command = commands[matches_[selected_].index];
                Execute(command);
                pending = command.action;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || closeRequested_)
        {
            ImGui::CloseCurrentPopup();
            isOpen_ = false;
            closeRequested_ = false;
        }

        ImGui::Spacing();

        // ---- 一覧 ----
        const float rowHeight = ImGui::GetFrameHeight() + 6.0f + ImGui::GetStyle().ItemSpacing.y;
        if (count == 0)
        {
            ImGui::TextDisabled("  一致する項目がありません");
        }
        else
        {
            const int rows = std::min(count, kVisibleRows);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
            if (ImGui::BeginChild("##list", ImVec2(0.0f, rowHeight * static_cast<float>(rows) + 4.0f), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoNav))
            {
                ImGuiListClipper clipper;
                clipper.Begin(count, rowHeight);
                if (scrollToSelection_)
                {
                    clipper.IncludeItemByIndex(selected_);
                }
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const Match &match = matches_[row];
                        const EditorCommand &command = commands[match.index];
                        ImGui::PushID(row);
                        if (DrawRow(command, match, row == selected_))
                        {
                            Execute(command);
                            pending = command.action;
                        }
                        // マウスを動かしたときだけホバーで選択を移す（キー操作中に勝手に戻らないように）
                        if (ImGui::IsItemHovered() && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f))
                        {
                            selected_ = row;
                        }
                        if (row == selected_ && scrollToSelection_)
                        {
                            ImGui::SetScrollHereY(0.5f);
                        }
                        ImGui::PopID();
                    }
                }
                scrollToSelection_ = false;
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
        }

        // ---- 使い方 ----
        ImGui::Separator();
        ImGui::TextDisabled(ICON_FA_KEYBOARD "  ↑↓ 移動   Enter 実行   Esc 閉じる      先頭に  > 操作   # ウィンドウ   @ オブジェクト   ! シーン   $ アセット");

        if (pending)
        {
            ImGui::CloseCurrentPopup();
            isOpen_ = false;
        }
        ImGui::EndPopup();
    }
    else
    {
        // 外側をクリックして閉じた
        isOpen_ = false;
    }
    ImGui::PopStyleVar(3);

    // 実行はポップアップを閉じた後に行う（実行先がモーダルを開くことがあるため）
    if (pending)
    {
        pending();
    }
}

} // namespace Hagine
#endif // USE_IMGUI
