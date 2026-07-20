// stb_truetypeの実装をこのファイルでのみ行う
#define STB_TRUETYPE_IMPLEMENTATION
#include "TextureManager.h"
#include "DirectXCommon.h"
#include <asset/AssetPath.h>
#include "utility/debug/imgui/ImGuiNotification.h"
#include <debug/log/Logger.h>
#include <string/StringUtility.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

// SRVインデックスのオフセット（=1）。エンジン共通の規約に合わせる。
// SrvManager::Allocate() で予約した r に対し、実際の書き込みは r+1（先頭1枠を空ける）。
// この +1 規約はエンジン全体で使われており（Sprite/Skin/Particle 各種/RendererBuffer 等）、
// ここだけ変えると他の +1 利用箇所と衝突するため 1 のまま維持すること。
namespace Hagine {
uint32_t TextureManager::kSRVIndexTop = 1;

void TextureManager::LoadTexture(const std::string &filePath)
{
    // 相対パスから実パス(＝マップキー)を作る。debug/配下はエンジン、それ以外はアプリのルート。
    std::string newFilePath = AssetPath::Image(filePath);

    // 読み込み済みテクスチャを検索
    if (textureDatas_.contains(newFilePath))
    {
        return;
    }

    // テクスチャ枚数上限をチェック
    assert(pSrvManager_->CanAllocate());

    // テクスチャファイルを読んでプログラムで扱えるようにする
    DirectX::ScratchImage image{};
    std::wstring filePathW = StringUtility::ConvertString(newFilePath);
    HRESULT hr;
    if (filePathW.ends_with(L".dds"))
    {
        isDDS_ = true;
        hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    }
    else
    {
        isDDS_ = false;
        hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    }
    if (FAILED(hr))
    {
        char hrText[16] = {};
        snprintf(hrText, sizeof(hrText), "0x%08X", static_cast<unsigned int>(hr));
        Logger::Error("Failed to load texture: \"" + newFilePath + "\" (HRESULT=" + hrText + "). The file may be missing or its format unsupported.");
        assert(SUCCEEDED(hr));
        return;
    }

    DirectX::ScratchImage *imageToUse = &image; // 初期値はオリジナルのイメージ

    // ミニマップの作成
    DirectX::ScratchImage mipImages{};
    if (DirectX::IsCompressed(image.GetMetadata().format))
    {
        mipImages = std::move(image);
    }
    else
    {
        hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 4, mipImages);
    }

    if (SUCCEEDED(hr))
    {
        imageToUse = &mipImages; // ミップマップが生成された場合はこれを使用
    }

    // テクスチャデータを追加して書き込む
    TextureData &textureData = textureDatas_[newFilePath];

    textureData.metadata = imageToUse->GetMetadata();
    textureData.resource = pDxCommon_->CreateTextureResource(textureData.metadata);
    textureData.intermediateResource = pDxCommon_->UploadTextureData(textureData.resource, *imageToUse);

    textureData.srvIndex = pSrvManager_->Allocate() + kSRVIndexTop;
    textureData.srvHandleCPU = pSrvManager_->GetCPUDescriptorHandle(textureData.srvIndex);
    textureData.srvHandleGPU = pSrvManager_->GetGPUDescriptorHandle(textureData.srvIndex);

    pSrvManager_->CreateSRVforTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata, UINT(textureData.metadata.mipLevels));
    ImGuiNotification::Post("テクスチャを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
}

void TextureManager::LoadFontTexture(const std::string &fontFilePath, float fontSize, int atlasWidth, int atlasHeight)
{
    // フォントキーで重複ロードを防ぐ
    const std::string fontKey = MakeFontKey(fontFilePath, fontSize);
    if (fontDatas_.contains(fontKey))
    {
        return;
    }

    assert(pSrvManager_->CanAllocate());

    // TTFファイルをバイナリとして丸ごと読み込む
    const std::string fullPath = AssetPath::Font(fontFilePath);
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
    {
        Logger::Error("Failed to open font file: \"" + fullPath + "\". The file was not found.");
        assert(file.is_open());
        return;
    }
    const std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> ttfBuffer(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char *>(ttfBuffer.data()), fileSize);

    // stb_truetypeでグレースケールのグリフアトラスをベイクする
    // ベイク対象: ASCII 32('space') 〜 127 の96文字
    FontData fontData{};
    fontData.atlasWidth = atlasWidth;
    fontData.atlasHeight = atlasHeight;

    std::vector<uint8_t> grayscaleBitmap(static_cast<size_t>(atlasWidth * atlasHeight));
    constexpr int kFirstChar = 32; // ' '(スペース)
    constexpr int kCharCount = 96; // ASCII 127まで

    const int bakeResult = stbtt_BakeFontBitmap(
        ttfBuffer.data(), 0,
        fontSize,
        grayscaleBitmap.data(), atlasWidth, atlasHeight,
        kFirstChar, kCharCount,
        fontData.charData.data());

    // bakeResultが負の場合はアトラスサイズが不足している
    if (bakeResult <= 0)
    {
        Logger::Error("Failed to bake font atlas: \"" + fontFilePath + "\". The atlas size (" +
                      std::to_string(atlasWidth) + "x" + std::to_string(atlasHeight) + ") is too small for the requested font size.");
        assert(bakeResult > 0);
        return;
    }

    // アトラスのグレースケールピクセルをCPU側でも保持する（テキストテクスチャ合成用）
    fontData.atlasPixels = grayscaleBitmap;

    // グレースケール(R8)をRGBA(R8G8B8A8)に展開する
    // RGB=白(255)固定、A=グリフのカバレッジ とすることでシェーダー側でαテストが容易になる
    DirectX::ScratchImage scratchImage{};
    HRESULT hr = scratchImage.Initialize2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        static_cast<size_t>(atlasWidth),
        static_cast<size_t>(atlasHeight),
        1, 1);
    assert(SUCCEEDED(hr));

    const DirectX::Image *img = scratchImage.GetImages();
    uint8_t *dest = img->pixels;
    const size_t rowPitch = img->rowPitch; // D3D12のアライメントを考慮したピッチ

    for (int y = 0; y < atlasHeight; ++y)
    {
        for (int x = 0; x < atlasWidth; ++x)
        {
            const uint8_t alpha = grayscaleBitmap[y * atlasWidth + x];
            uint8_t *pixel = dest + y * rowPitch + x * 4;
            pixel[0] = 255;   // R
            pixel[1] = 255;   // G
            pixel[2] = 255;   // B
            pixel[3] = alpha; // A: グリフのカバレッジ
        }
    }

    // メタデータを手動で組み立ててGPUリソースを作成する
    DirectX::TexMetadata metadata{};
    metadata.width = static_cast<size_t>(atlasWidth);
    metadata.height = static_cast<size_t>(atlasHeight);
    metadata.depth = 1;
    metadata.arraySize = 1;
    metadata.mipLevels = 1;
    metadata.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

    fontData.resource = pDxCommon_->CreateTextureResource(metadata);
    fontData.intermediateResource = pDxCommon_->UploadTextureData(fontData.resource, scratchImage);
    fontData.ttfBuffer = std::make_shared<std::vector<uint8_t>>(ttfBuffer);
    fontData.fontSize = fontSize;

    // SRVを割り当てて登録する
    fontData.srvIndex = pSrvManager_->Allocate() + kSRVIndexTop;
    fontData.srvHandleCPU = pSrvManager_->GetCPUDescriptorHandle(fontData.srvIndex);
    fontData.srvHandleGPU = pSrvManager_->GetGPUDescriptorHandle(fontData.srvIndex);

    pSrvManager_->CreateSRVforTexture2D(fontData.srvIndex, fontData.resource.Get(), metadata, 1);

    fontDatas_[fontKey] = std::move(fontData);
    ImGuiNotification::Post("フォントテクスチャを読み込みました: " + fontFilePath, {0.2f, 0.8f, 0.8f, 1.0f});
}

void TextureManager::Initialize(SrvManager *srvManager)
{
    pDxCommon_ = DirectXCommon::GetInstance();
    pSrvManager_ = srvManager;
    // SRVの数と同数
    textureDatas_.reserve(SrvManager::kMaxSRVCount);
}

void TextureManager::Finalize()
{
    // 通常テクスチャのSRVインデックスを解放する
    for (auto &pair : textureDatas_)
    {
        pSrvManager_->Free(pair.second.srvIndex - kSRVIndexTop);
    }
    textureDatas_.clear();

    // フォントアトラスのSRVインデックスを解放する
    for (auto &pair : fontDatas_)
    {
        pSrvManager_->Free(pair.second.srvIndex - kSRVIndexTop);
    }
    fontDatas_.clear();
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string &filePath)
{
    // 相対パスから実パス(＝マップキー)を作る。
    std::string newFilePath = AssetPath::Image(filePath);

    auto it = textureDatas_.find(newFilePath);
    if (it != textureDatas_.end())
    {
        return it->second.srvIndex;
    }

    // 見つからない場合はassertでエラーにする
    Logger::Error("Texture index not found: \"" + newFilePath + "\". The texture was never loaded (the path may be wrong).");
    assert(0);
    return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string &filePath)
{
    // 指定されたファイルパスが存在するかチェック
    if (textureDatas_.find(filePath) == textureDatas_.end())
    {
        Logger::Error("Texture handle not found: \"" + filePath + "\". The texture was never loaded (the path may be wrong).");
        assert(textureDatas_.find(filePath) != textureDatas_.end());
    }

    TextureData &textureData = textureDatas_[filePath];
    return textureData.srvHandleGPU;
}

const DirectX::TexMetadata &TextureManager::GetMetaData(const std::string &filePath)
{
    std::string fullPath = AssetPath::Image(filePath);
    // 指定されたファイルパスが存在するかチェック
    if (textureDatas_.find(fullPath) == textureDatas_.end())
    {
        Logger::Error("Texture metadata not found: \"" + fullPath + "\". The texture was never loaded (the path may be wrong).");
        assert(textureDatas_.find(fullPath) != textureDatas_.end());
    }

    TextureData &textureData = textureDatas_[fullPath];
    return textureData.metadata;
}

const TextureManager::FontData *TextureManager::GetFontData(const std::string &fontKey) const
{
    auto it = fontDatas_.find(fontKey);
    if (it != fontDatas_.end())
    {
        return &it->second;
    }
    return nullptr;
}

std::string TextureManager::MakeFontKey(const std::string &fontFilePath, float fontSize)
{
    // フォントパスとサイズを組み合わせて一意なキーを生成する
    // floatをそのまま文字列化すると精度問題が出るためintに丸める
    return fontFilePath + "_" + std::to_string(static_cast<int>(fontSize));
}

std::vector<std::string> TextureManager::GetAllFontKeys() const
{
    std::vector<std::string> keys;
    keys.reserve(fontDatas_.size());
    for (const auto &pair : fontDatas_)
    {
        keys.push_back(pair.first);
    }
    return keys;
}

void TextureManager::LoadAllTextures()
{
    // images はエンジン(debug)とアプリの 2 ルートに分割されているため両方を走査する。
    // 相対パスは各ルート基点で作るので AssetPath::Image() が正しいルートへ振り分ける。
    for (const std::string &baseDir : AssetPath::ImageScanRoots())
    {

        // ディレクトリが存在しない場合はスキップ
        if (!std::filesystem::exists(baseDir))
        {
            continue;
        }

        // 再帰的探索
        for (auto &entry : std::filesystem::recursive_directory_iterator(baseDir))
        {

            // ファイルでなければスキップ
            if (!entry.is_regular_file())
                continue;

            // 拡張子を取得
            std::string ext = entry.path().extension().string();

            // png・jpg 以外は無視
            if (ext != ".png" && ext != ".jpg")
                continue;

            // baseDir からの相対パスを作成
            std::filesystem::path relative = entry.path().lexically_relative(baseDir);

            // Windows だとパス区切りが \ なので / に統一する
            std::string file = relative.string();
            std::replace(file.begin(), file.end(), '\\', '/');

            // 既にロード済みならスキップ
            if (textureDatas_.contains(AssetPath::Image(file)))
            {
                continue;
            }

            // テクスチャを読み込む
            LoadTexture(file);
        }
    }
}
} // namespace Hagine
