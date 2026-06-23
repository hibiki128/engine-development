#define NOMINMAX
#include "TextRenderer.h"
#include "../SpriteCommon.h"
#include "../SpriteManager.h"
#include <Graphics/Texture/TextureManager.h>
#include <String/StringUtility.h>
#include <DirectXTex/DirectXTex.h>
#include <imgui/imstb_truetype.h>
#ifdef _DEBUG
#include <imgui.h>
#endif // _DEBUG
#include <String/StringUtility.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <vector>
#include <wincodec.h>

namespace Hagine {
const std::string TextRenderer::kSaveFolderRelative = "Text";
const std::string TextRenderer::kSaveFolder = "resources/images/Text";

// --------------------------------------------------------------------------
// ローカルヘルパー
// --------------------------------------------------------------------------

/// <summary>
/// コードポイントをUTF-8文字列に変換する。
/// サロゲートペアを構成する必要があるBMP外文字にも対応する。
/// </summary>
static std::string CodepointToUtf8(uint32_t codepoint) {
    wchar_t wBuf[3] = {};
    int wLen = 0;
    if (codepoint < 0x10000) {
        wBuf[0] = static_cast<wchar_t>(codepoint);
        wLen = 1;
    } else {
        // BMP外文字はUTF-16サロゲートペアとしてエンコードする
        const uint32_t cp = codepoint - 0x10000;
        wBuf[0] = static_cast<wchar_t>(0xD800 + (cp >> 10));
        wBuf[1] = static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        wLen = 2;
    }
    char buf[8] = {};
    WideCharToMultiByte(CP_UTF8, 0, wBuf, wLen, buf, sizeof(buf), nullptr, nullptr);
    return std::string(buf);
}

/// <summary>
/// UTF-8文字列をコードポイントの配列に変換する。
/// サロゲートペア（BMP外文字）にも対応する。
/// </summary>
static std::vector<uint32_t> Utf8ToCodepoints(const std::string &utf8) {
    const std::wstring wide = StringUtility::ConvertString(utf8);
    std::vector<uint32_t> codepoints;
    for (size_t i = 0; i < wide.length(); ++i) {
        uint32_t cp = static_cast<uint32_t>(wide[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wide.length()) {
            const uint32_t low = static_cast<uint32_t>(wide[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        codepoints.push_back(cp);
    }
    return codepoints;
}

// --------------------------------------------------------------------------
// 公開メソッド
// --------------------------------------------------------------------------

void TextRenderer::CreateTextSprite(
    const std::string &spriteName,
    const std::string &text,
    const std::string &fontKey,
    Vector2 position,
    Vector4 color,
    bool outlineEnabled,
    float outlineThickness,
    Vector4 outlineColor) {
    // 同名スプライトが存在する場合は再生成のために先に削除する
    if (SpriteManager::GetInstance()->GetSprite(spriteName) != nullptr) {
        SpriteManager::GetInstance()->UnregisterSprite(spriteName);
    }

    // テキストをPNGとして保存し、TextureManagerへロードして相対パスを受け取る
    const std::string relPath = RenderTextToFile(spriteName, text, fontKey, outlineEnabled, outlineThickness, outlineColor);

    // SpriteManagerにスプライトとして登録する
    SpriteTransform transform;
    transform.position = position;
    transform.color = color;
    SpriteManager::GetInstance()->RegisterSprite(spriteName, relPath, transform);
}

void TextRenderer::CreateCharacterAtlasSprite(
    const std::string &spriteName,
    const std::string &chars,
    const std::string &fontKey,
    int cellWidth,
    int cellHeight,
    Vector2 position,
    Vector4 color,
    bool outlineEnabled,
    float outlineThickness,
    Vector4 outlineColor) {
    // 同名スプライトが存在する場合は再生成のために先に削除する
    if (SpriteManager::GetInstance()->GetSprite(spriteName) != nullptr) {
        SpriteManager::GetInstance()->UnregisterSprite(spriteName);
    }

    // 入力文字列をコードポイント列に変換する
    const std::vector<uint32_t> codepoints = Utf8ToCodepoints(chars);
    if (codepoints.empty()) {
        return;
    }

    // アトラステクスチャをPNGとして保存し、TextureManagerへロードして相対パスを受け取る
    const std::string relPath = RenderCharacterAtlasToFile(
        spriteName, codepoints, fontKey,
        cellWidth, cellHeight,
        outlineEnabled, outlineThickness, outlineColor);

    // SpriteManagerにスプライトとして登録する
    SpriteTransform transform;
    transform.position = position;
    transform.color = color;
    SpriteManager::GetInstance()->RegisterSprite(spriteName, relPath, transform);
}

void TextRenderer::UpdateImGui() {
#ifdef _DEBUG
    if (!ImGui::Begin("テキストレンダラー (TextRenderer)")) {
        ImGui::End();
        return;
    }

    // 作成モードをRadioButtonで切り替える
    ImGui::RadioButton("テキストスプライト", &imguiMode_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("アトラススプライト", &imguiMode_, 1);
    ImGui::Separator();

    std::vector<std::string> fontKeys = TextureManager::GetInstance()->GetAllFontKeys();

    // ================================================================
    // テキストスプライト作成
    // ================================================================
    if (imguiMode_ == 0) {
        ImGui::SeparatorText("テキストスプライト作成");

        ImGui::InputText("スプライト名", imguiSpriteName_, sizeof(imguiSpriteName_));
        ImGui::InputText("テキスト", imguiText_, sizeof(imguiText_));

        if (!fontKeys.empty()) {
            imguiFontIndex_ = std::clamp(imguiFontIndex_, 0, static_cast<int>(fontKeys.size()) - 1);
            const char *current = fontKeys[imguiFontIndex_].c_str();
            if (ImGui::BeginCombo("フォント", current)) {
                for (int i = 0; i < static_cast<int>(fontKeys.size()); ++i) {
                    const bool selected = (i == imguiFontIndex_);
                    if (ImGui::Selectable(fontKeys[i].c_str(), selected)) {
                        imguiFontIndex_ = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("フォントがありません。TextureManager::LoadFontTexture()を呼んでください。");
        }

        ImGui::DragFloat2("座標", imguiPosition_, 1.0f);
        ImGui::ColorEdit4("文字色", imguiColor_);

        // アウトライン設定
        ImGui::SeparatorText("アウトライン設定");
        ImGui::Checkbox("アウトラインを有効にする", &imguiOutlineEnabled_);

        if (imguiOutlineEnabled_) {
            ImGui::DragFloat("太さ (px)", &imguiOutlineThickness_, 0.5f, 0.5f, 20.0f, "%.1f");
            ImGui::ColorEdit4("アウトライン色", imguiOutlineColor_);
        } else {
            ImGui::BeginDisabled();
            ImGui::DragFloat("太さ (px)", &imguiOutlineThickness_, 0.5f, 0.5f, 20.0f, "%.1f");
            ImGui::ColorEdit4("アウトライン色", imguiOutlineColor_);
            ImGui::EndDisabled();
        }

        const bool canCreate = (imguiSpriteName_[0] != '\0') &&
                               (imguiText_[0] != '\0') &&
                               !fontKeys.empty();

        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("テキストスプライトを作成")) {
            CreateTextSprite(
                std::string(imguiSpriteName_),
                std::string(imguiText_),
                fontKeys[imguiFontIndex_],
                {imguiPosition_[0], imguiPosition_[1]},
                {imguiColor_[0], imguiColor_[1], imguiColor_[2], imguiColor_[3]},
                imguiOutlineEnabled_,
                imguiOutlineThickness_,
                {imguiOutlineColor_[0], imguiOutlineColor_[1], imguiOutlineColor_[2], imguiOutlineColor_[3]});
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }
    }

    // ================================================================
    // アトラススプライト作成
    // 全文字を1枚のテクスチャに固定セルで横並びに配置する
    // UV操作による文字の切り出しは呼び出し側で行う
    // ================================================================
    if (imguiMode_ == 1) {
        ImGui::SeparatorText("アトラススプライト作成");
        ImGui::TextDisabled("全文字を1枚のテクスチャに配置します。UV操作で各文字を切り出して使用してください。");

        ImGui::InputText("スプライト名", imguiAtlasSpriteName_, sizeof(imguiAtlasSpriteName_));
        ImGui::InputText("文字列 (例: ABCDE)", imguiAtlasChars_, sizeof(imguiAtlasChars_));

        if (!fontKeys.empty()) {
            imguiAtlasFontIndex_ = std::clamp(imguiAtlasFontIndex_, 0, static_cast<int>(fontKeys.size()) - 1);
            const char *atlasCurrent = fontKeys[imguiAtlasFontIndex_].c_str();
            if (ImGui::BeginCombo("フォント##atlas", atlasCurrent)) {
                for (int i = 0; i < static_cast<int>(fontKeys.size()); ++i) {
                    const bool selected = (i == imguiAtlasFontIndex_);
                    if (ImGui::Selectable(fontKeys[i].c_str(), selected)) {
                        imguiAtlasFontIndex_ = i;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextDisabled("フォントがありません。TextureManager::LoadFontTexture()を呼んでください。");
        }

        // セルサイズ設定
        ImGui::SeparatorText("セルサイズ");
        ImGui::DragInt("セル幅 (px)", &imguiAtlasCellWidth_, 1.0f, 8, 512);
        ImGui::DragInt("セル高さ (px)", &imguiAtlasCellHeight_, 1.0f, 8, 512);

        // 生成されるテクスチャサイズをプレビュー表示する
        const int charCount = static_cast<int>(strlen(imguiAtlasChars_));
        ImGui::TextDisabled(
            "生成テクスチャサイズ: %d x %d px (%d 文字)",
            imguiAtlasCellWidth_ * charCount,
            imguiAtlasCellHeight_,
            charCount);

        ImGui::DragFloat2("座標##atlas", imguiAtlasPosition_, 1.0f);
        ImGui::ColorEdit4("文字色##atlas", imguiAtlasColor_);

        // アウトライン設定
        ImGui::SeparatorText("アウトライン設定##atlas");
        ImGui::Checkbox("アウトラインを有効にする##atlas", &imguiAtlasOutlineEnabled_);

        if (imguiAtlasOutlineEnabled_) {
            ImGui::DragFloat("太さ (px)##atlas", &imguiAtlasOutlineThickness_, 0.5f, 0.5f, 20.0f, "%.1f");
            ImGui::ColorEdit4("アウトライン色##atlas", imguiAtlasOutlineColor_);
        } else {
            ImGui::BeginDisabled();
            ImGui::DragFloat("太さ (px)##atlas", &imguiAtlasOutlineThickness_, 0.5f, 0.5f, 20.0f, "%.1f");
            ImGui::ColorEdit4("アウトライン色##atlas", imguiAtlasOutlineColor_);
            ImGui::EndDisabled();
        }

        const bool canCreate = (imguiAtlasSpriteName_[0] != '\0') &&
                               (imguiAtlasChars_[0] != '\0') &&
                               !fontKeys.empty();

        if (!canCreate) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("アトラススプライトを作成")) {
            CreateCharacterAtlasSprite(
                std::string(imguiAtlasSpriteName_),
                std::string(imguiAtlasChars_),
                fontKeys[imguiAtlasFontIndex_],
                imguiAtlasCellWidth_,
                imguiAtlasCellHeight_,
                {imguiAtlasPosition_[0], imguiAtlasPosition_[1]},
                {imguiAtlasColor_[0], imguiAtlasColor_[1], imguiAtlasColor_[2], imguiAtlasColor_[3]},
                imguiAtlasOutlineEnabled_,
                imguiAtlasOutlineThickness_,
                {imguiAtlasOutlineColor_[0], imguiAtlasOutlineColor_[1], imguiAtlasOutlineColor_[2], imguiAtlasOutlineColor_[3]});
        }
        if (!canCreate) {
            ImGui::EndDisabled();
        }
    }

    ImGui::End();
#endif // _DEBUG
}

// --------------------------------------------------------------------------
// 非公開メソッド
// --------------------------------------------------------------------------

std::string TextRenderer::RenderTextToFile(
    const std::string &spriteName,
    const std::string &text,
    const std::string &fontKey,
    bool outlineEnabled,
    float outlineThickness,
    Vector4 outlineColor) {
    const TextureManager::FontData *fontData = TextureManager::GetInstance()->GetFontData(fontKey);
    assert(fontData != nullptr);
    assert(fontData->ttfBuffer != nullptr);

    // stb_truetypeの初期化
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData->ttfBuffer->data(), 0)) {
        assert(0 && "Failed to init font");
    }

    float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontData->fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    int maxAscent = static_cast<int>(std::round(ascent * scale));
    int maxDescent = static_cast<int>(std::round(-descent * scale));

    // StringUtilityを使ってUTF-8(std::string)からUTF-16(std::wstring)へ変換
    std::wstring wText = StringUtility::ConvertString(text);

    // std::wstring からコードポイントの配列を抽出（サロゲートペア対応）
    std::vector<uint32_t> codepoints;
    for (size_t i = 0; i < wText.length(); ++i) {
        uint32_t cp = static_cast<uint32_t>(wText[i]);
        // 絵文字や一部の難しい漢字など（サロゲートペア）の処理
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < wText.length()) {
            uint32_t low = static_cast<uint32_t>(wText[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        codepoints.push_back(cp);
    }

    // テクスチャの全体横幅を計算する
    float totalWidth = 0.0f;
    for (size_t i = 0; i < codepoints.size(); ++i) {
        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&fontInfo, codepoints[i], &advanceWidth, &leftSideBearing);
        totalWidth += advanceWidth * scale;

        if (i + 1 < codepoints.size()) {
            totalWidth += stbtt_GetCodepointKernAdvance(&fontInfo, codepoints[i], codepoints[i + 1]) * scale;
        }
    }

    // アウトライン有効時はその太さ分のパディングを上下左右に付加してクリッピングを防ぐ
    const int outlinePad = outlineEnabled ? static_cast<int>(std::ceil(outlineThickness)) : 0;

    const int texWidth = std::max(1, static_cast<int>(std::ceil(totalWidth)) + outlinePad * 2);
    const int texHeight = std::max(1, maxAscent + maxDescent + 2 + outlinePad * 2);

    std::vector<uint8_t> pixels(static_cast<size_t>(texWidth * texHeight * 4), 0);

    // 各文字をビットマップ化してピクセルバッファに書き込む
    float cursorX = static_cast<float>(outlinePad);
    for (size_t i = 0; i < codepoints.size(); ++i) {
        uint32_t cp = codepoints[i];

        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&fontInfo, cp, &advanceWidth, &leftSideBearing);

        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&fontInfo, cp, scale, scale, &x0, &y0, &x1, &y1);

        int glyphW = x1 - x0;
        int glyphH = y1 - y0;

        // パディングを考慮したグリフの描画先座標を計算する
        int dstX = static_cast<int>(std::round(cursorX)) + x0;
        int dstY = maxAscent + y0 + outlinePad;

        if (glyphW > 0 && glyphH > 0) {
            std::vector<uint8_t> glyphBitmap(glyphW * glyphH);
            stbtt_MakeCodepointBitmap(&fontInfo, glyphBitmap.data(), glyphW, glyphH, glyphW, scale, scale, cp);

            for (int gy = 0; gy < glyphH; ++gy) {
                for (int gx = 0; gx < glyphW; ++gx) {
                    int px = dstX + gx;
                    int py = dstY + gy;
                    if (px < 0 || px >= texWidth || py < 0 || py >= texHeight)
                        continue;

                    uint8_t alpha = glyphBitmap[gy * glyphW + gx];
                    if (alpha > 0) {
                        int dstIdx = (py * texWidth + px) * 4;
                        pixels[dstIdx + 0] = 255;
                        pixels[dstIdx + 1] = 255;
                        pixels[dstIdx + 2] = 255;
                        pixels[dstIdx + 3] = std::max(pixels[dstIdx + 3], alpha);
                    }
                }
            }
        }

        cursorX += advanceWidth * scale;
        if (i + 1 < codepoints.size()) {
            cursorX += stbtt_GetCodepointKernAdvance(&fontInfo, cp, codepoints[i + 1]) * scale;
        }
    }

    // アウトライン処理:
    // グリフが描画済みのピクセルバッファに対して膨張処理（ダイレーション）を行い、
    // グリフの外周に指定した太さ・色のアウトラインを合成する
    if (outlineEnabled && outlineThickness > 0.0f) {
        const int radius = static_cast<int>(std::ceil(outlineThickness));

        // アウトラインピクセルを格納するバッファ（alpha のみ判定に使用）
        std::vector<uint8_t> outlineMask(static_cast<size_t>(texWidth * texHeight), 0);

        // グリフが存在しないピクセルに対して近傍探索を行い、
        // 半径 outlineThickness 以内にグリフピクセルがあればアウトライン候補とする
        for (int y = 0; y < texHeight; ++y) {
            for (int x = 0; x < texWidth; ++x) {
                const int selfIdx = (y * texWidth + x) * 4;

                // グリフが描画済みの箇所はアウトライン不要
                if (pixels[selfIdx + 3] > 0)
                    continue;

                // 近傍ピクセルをサーチして半径内にグリフが存在するか確認
                bool hasNeighbor = false;
                for (int dy = -radius; dy <= radius && !hasNeighbor; ++dy) {
                    for (int dx = -radius; dx <= radius && !hasNeighbor; ++dx) {
                        // 円形マスクにするため距離チェック
                        const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                        if (dist > outlineThickness)
                            continue;

                        const int nx = x + dx;
                        const int ny = y + dy;
                        if (nx < 0 || nx >= texWidth || ny < 0 || ny >= texHeight)
                            continue;

                        const int nIdx = (ny * texWidth + nx) * 4;
                        if (pixels[nIdx + 3] > 0)
                            hasNeighbor = true;
                    }
                }

                if (hasNeighbor) {
                    outlineMask[y * texWidth + x] = 255;
                }
            }
        }

        // アウトライン色をベースピクセルバッファに書き込む
        // グリフが存在しないピクセルにのみ上書きすることで、文字の上にはみ出さないようにする
        const uint8_t outR = static_cast<uint8_t>(std::clamp(outlineColor.x, 0.0f, 1.0f) * 255.0f);
        const uint8_t outG = static_cast<uint8_t>(std::clamp(outlineColor.y, 0.0f, 1.0f) * 255.0f);
        const uint8_t outB = static_cast<uint8_t>(std::clamp(outlineColor.z, 0.0f, 1.0f) * 255.0f);
        const uint8_t outA = static_cast<uint8_t>(std::clamp(outlineColor.w, 0.0f, 1.0f) * 255.0f);

        for (int y = 0; y < texHeight; ++y) {
            for (int x = 0; x < texWidth; ++x) {
                if (outlineMask[y * texWidth + x] == 0)
                    continue;

                const int idx = (y * texWidth + x) * 4;
                // グリフピクセルは上書きしない
                if (pixels[idx + 3] > 0)
                    continue;

                pixels[idx + 0] = outR;
                pixels[idx + 1] = outG;
                pixels[idx + 2] = outB;
                pixels[idx + 3] = outA;
            }
        }
    }

    // DirectXTex による PNG 保存処理
    EnsureOutputDirectory();

    DirectX::ScratchImage scratchImage;
    HRESULT hr = scratchImage.Initialize2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        static_cast<size_t>(texWidth),
        static_cast<size_t>(texHeight),
        1, 1);
    assert(SUCCEEDED(hr));

    const DirectX::Image *img = scratchImage.GetImages();
    for (int y = 0; y < texHeight; ++y) {
        std::memcpy(
            img->pixels + y * img->rowPitch,
            pixels.data() + y * texWidth * 4,
            static_cast<size_t>(texWidth * 4));
    }

    const std::string fileName = spriteName + ".png";
    const std::string filePath = kSaveFolder + "/" + fileName;
    const std::string loadPath = kSaveFolderRelative + "/" + fileName;

    const std::wstring wFilePath = StringUtility::ConvertString(filePath);
    hr = DirectX::SaveToWICFile(*img, DirectX::WIC_FLAGS_NONE,
                                GUID_ContainerFormatPng, wFilePath.c_str());
    assert(SUCCEEDED(hr));

    TextureManager::GetInstance()->LoadTexture(loadPath);

    return loadPath;
}

std::string TextRenderer::RenderCharacterAtlasToFile(
    const std::string &spriteName,
    const std::vector<uint32_t> &codepoints,
    const std::string &fontKey,
    int cellWidth,
    int cellHeight,
    bool outlineEnabled,
    float outlineThickness,
    Vector4 outlineColor) {
    const TextureManager::FontData *fontData = TextureManager::GetInstance()->GetFontData(fontKey);
    assert(fontData != nullptr);
    assert(fontData->ttfBuffer != nullptr);

    // stb_truetypeの初期化
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData->ttfBuffer->data(), 0)) {
        assert(0 && "Failed to init font");
    }

    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, fontData->fontSize);
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    const int scaledAscent = static_cast<int>(std::round(ascent * scale));
    const int scaledDescent = static_cast<int>(std::round(-descent * scale));
    const int textBlockHeight = scaledAscent + scaledDescent;

    // アウトライン半径分をセル内側に余白として確保する
    // これによりグリフ端のアウトラインがセル境界でクリップされることを防ぐ
    const int outlineRadius = outlineEnabled ? static_cast<int>(std::ceil(outlineThickness)) : 0;
    const int effectiveCellWidth = cellWidth - outlineRadius * 2;
    const int effectiveCellHeight = cellHeight - outlineRadius * 2;

    // セル内でのベースラインY座標（垂直中央揃え）
    // アウトライン余白を起点にして、有効描画領域内でフォントブロックを中央配置する
    const int verticalOffset = outlineRadius + std::max(0, (effectiveCellHeight - textBlockHeight) / 2);
    const int baselineY = verticalOffset + scaledAscent;

    const int numChars = static_cast<int>(codepoints.size());
    const int atlasWidth = cellWidth * numChars;
    const int atlasHeight = cellHeight;

    // アトラス全体のピクセルバッファ（RGBA, 全て透明で初期化）
    std::vector<uint8_t> atlasPixels(static_cast<size_t>(atlasWidth * atlasHeight * 4), 0);

    // アウトライン色を事前にバイト値に変換しておく
    const uint8_t outR = static_cast<uint8_t>(std::clamp(outlineColor.x, 0.0f, 1.0f) * 255.0f);
    const uint8_t outG = static_cast<uint8_t>(std::clamp(outlineColor.y, 0.0f, 1.0f) * 255.0f);
    const uint8_t outB = static_cast<uint8_t>(std::clamp(outlineColor.z, 0.0f, 1.0f) * 255.0f);
    const uint8_t outA = static_cast<uint8_t>(std::clamp(outlineColor.w, 0.0f, 1.0f) * 255.0f);

    for (int ci = 0; ci < numChars; ++ci) {
        const uint32_t cp = codepoints[ci];

        int advanceWidth, leftSideBearing;
        stbtt_GetCodepointHMetrics(&fontInfo, cp, &advanceWidth, &leftSideBearing);
        const float glyphAdvance = advanceWidth * scale;

        // グリフのバウンディングボックスをスケール済みで取得する
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&fontInfo, cp, scale, scale, &x0, &y0, &x1, &y1);
        const int glyphW = x1 - x0;
        const int glyphH = y1 - y0;

        // セル内水平中央揃え:
        // アウトライン余白を起点にして有効描画領域内でアドバンス幅を中央配置し、グリフオフセットを加える
        const int cellOffsetX = outlineRadius +
                                static_cast<int>(std::round((effectiveCellWidth - glyphAdvance) / 2.0f)) + x0;
        // セル内垂直: ベースライン基準でグリフオフセットを加える
        const int cellOffsetY = baselineY + y0;

        // セル単位のピクセルバッファ（RGBA）
        std::vector<uint8_t> cellPixels(static_cast<size_t>(cellWidth * cellHeight * 4), 0);

        // グリフビットマップをセルバッファに書き込む
        if (glyphW > 0 && glyphH > 0) {
            std::vector<uint8_t> glyphBitmap(static_cast<size_t>(glyphW * glyphH));
            stbtt_MakeCodepointBitmap(&fontInfo, glyphBitmap.data(), glyphW, glyphH, glyphW, scale, scale, cp);

            for (int gy = 0; gy < glyphH; ++gy) {
                for (int gx = 0; gx < glyphW; ++gx) {
                    const int px = cellOffsetX + gx;
                    const int py = cellOffsetY + gy;
                    if (px < 0 || px >= cellWidth || py < 0 || py >= cellHeight)
                        continue;

                    const uint8_t alpha = glyphBitmap[gy * glyphW + gx];
                    if (alpha > 0) {
                        const int idx = (py * cellWidth + px) * 4;
                        cellPixels[idx + 0] = 255;
                        cellPixels[idx + 1] = 255;
                        cellPixels[idx + 2] = 255;
                        cellPixels[idx + 3] = std::max(cellPixels[idx + 3], alpha);
                    }
                }
            }
        }

        // アウトライン処理をセル単位で実行する
        // セル単位で処理することで隣接セルへのアウトラインのはみ出しを防ぐ
        if (outlineEnabled && outlineThickness > 0.0f) {
            std::vector<uint8_t> outlineMask(static_cast<size_t>(cellWidth * cellHeight), 0);

            // グリフピクセルの近傍を探索してアウトライン候補ピクセルを収集する
            for (int y = 0; y < cellHeight; ++y) {
                for (int x = 0; x < cellWidth; ++x) {
                    // グリフが描画済みの箇所はアウトライン不要
                    if (cellPixels[(y * cellWidth + x) * 4 + 3] > 0)
                        continue;

                    bool hasNeighbor = false;
                    for (int dy = -outlineRadius; dy <= outlineRadius && !hasNeighbor; ++dy) {
                        for (int dx = -outlineRadius; dx <= outlineRadius && !hasNeighbor; ++dx) {
                            // 円形マスクにするため距離チェック
                            const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
                            if (dist > outlineThickness)
                                continue;

                            const int nx = x + dx;
                            const int ny = y + dy;
                            if (nx < 0 || nx >= cellWidth || ny < 0 || ny >= cellHeight)
                                continue;

                            if (cellPixels[(ny * cellWidth + nx) * 4 + 3] > 0)
                                hasNeighbor = true;
                        }
                    }

                    if (hasNeighbor) {
                        outlineMask[y * cellWidth + x] = 255;
                    }
                }
            }

            // アウトライン色をセルバッファに書き込む（グリフピクセルは上書きしない）
            for (int y = 0; y < cellHeight; ++y) {
                for (int x = 0; x < cellWidth; ++x) {
                    if (outlineMask[y * cellWidth + x] == 0)
                        continue;

                    const int idx = (y * cellWidth + x) * 4;
                    if (cellPixels[idx + 3] > 0)
                        continue;

                    cellPixels[idx + 0] = outR;
                    cellPixels[idx + 1] = outG;
                    cellPixels[idx + 2] = outB;
                    cellPixels[idx + 3] = outA;
                }
            }
        }

        // セルのピクセルをアトラスバッファの対応する位置にコピーする
        const int cellStartX = ci * cellWidth;
        for (int y = 0; y < cellHeight; ++y) {
            const int srcRowStart = y * cellWidth;
            const int dstRowStart = y * atlasWidth + cellStartX;
            for (int x = 0; x < cellWidth; ++x) {
                const int srcIdx = (srcRowStart + x) * 4;
                const int dstIdx = (dstRowStart + x) * 4;
                atlasPixels[dstIdx + 0] = cellPixels[srcIdx + 0];
                atlasPixels[dstIdx + 1] = cellPixels[srcIdx + 1];
                atlasPixels[dstIdx + 2] = cellPixels[srcIdx + 2];
                atlasPixels[dstIdx + 3] = cellPixels[srcIdx + 3];
            }
        }
    }

    // DirectXTex による PNG 保存処理
    EnsureOutputDirectory();

    DirectX::ScratchImage scratchImage;
    HRESULT hr = scratchImage.Initialize2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        static_cast<size_t>(atlasWidth),
        static_cast<size_t>(atlasHeight),
        1, 1);
    assert(SUCCEEDED(hr));

    const DirectX::Image *img = scratchImage.GetImages();
    for (int y = 0; y < atlasHeight; ++y) {
        std::memcpy(
            img->pixels + y * img->rowPitch,
            atlasPixels.data() + y * atlasWidth * 4,
            static_cast<size_t>(atlasWidth * 4));
    }

    const std::string fileName = spriteName + ".png";
    const std::string filePath = kSaveFolder + "/" + fileName;
    const std::string loadPath = kSaveFolderRelative + "/" + fileName;

    const std::wstring wFilePath = StringUtility::ConvertString(filePath);
    hr = DirectX::SaveToWICFile(*img, DirectX::WIC_FLAGS_NONE,
                                GUID_ContainerFormatPng, wFilePath.c_str());
    assert(SUCCEEDED(hr));

    TextureManager::GetInstance()->LoadTexture(loadPath);

    return loadPath;
}

void TextRenderer::EnsureOutputDirectory() {
    const std::filesystem::path dir(kSaveFolder);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
    }
}
} // namespace Hagine
