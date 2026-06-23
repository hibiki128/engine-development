// stb_truetypeの実装をこのファイルでのみ行う
#define STB_TRUETYPE_IMPLEMENTATION
#include "TextureManager.h"
#include "DirectXCommon.h"
#include "Utility/Debug/ImGui/ImGuiNotification.h"
#include <String/StringUtility.h>
#include <filesystem>
#include <fstream>
#include <vector>

// ImGuiで0番を使用するため、1番から使用
namespace Hagine {
uint32_t TextureManager::kSRVIndexTop = 1;

void TextureManager::LoadTexture(const std::string &filePath) {
    // ファイル名を取り出して、resources/images/を付ける
    std::string newFilePath = "resources/images/" + filePath;

    // 読み込み済みテクスチャを検索
    if (textureDatas_.contains(newFilePath)) {
        return;
    }

    // テクスチャ枚数上限をチェック
    assert(srvManager_->CanAllocate());

    // テクスチャファイルを読んでプログラムで扱えるようにする
    DirectX::ScratchImage image{};
    std::wstring filePathW = StringUtility::ConvertString(newFilePath);
    HRESULT hr;
    if (filePathW.ends_with(L".dds")) {
        isDDS_ = true;
        hr = DirectX::LoadFromDDSFile(filePathW.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    } else {
        isDDS_ = false;
        hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
    }
    assert(SUCCEEDED(hr));

    DirectX::ScratchImage *imageToUse = &image; // 初期値はオリジナルのイメージ

    // ミニマップの作成
    DirectX::ScratchImage mipImages{};
    if (DirectX::IsCompressed(image.GetMetadata().format)) {
        mipImages = std::move(image);
    } else {
        hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 4, mipImages);
    }

    if (SUCCEEDED(hr)) {
        imageToUse = &mipImages; // ミップマップが生成された場合はこれを使用
    }

    // テクスチャデータを追加して書き込む
    TextureData &textureData = textureDatas_[newFilePath];

    textureData.metadata = imageToUse->GetMetadata();
    textureData.resource = dxCommon_->CreateTextureResource(textureData.metadata);
    textureData.intermediateResource = dxCommon_->UploadTextureData(textureData.resource, *imageToUse);

    textureData.srvIndex = srvManager_->Allocate() + kSRVIndexTop;
    textureData.srvHandleCPU = srvManager_->GetCPUDescriptorHandle(textureData.srvIndex);
    textureData.srvHandleGPU = srvManager_->GetGPUDescriptorHandle(textureData.srvIndex);

    srvManager_->CreateSRVforTexture2D(textureData.srvIndex, textureData.resource.Get(), textureData.metadata, UINT(textureData.metadata.mipLevels));
    ImGuiNotification::Post("テクスチャを読み込みました: " + filePath, {0.2f, 0.8f, 0.8f, 1.0f});
}

void TextureManager::LoadFontTexture(const std::string &fontFilePath, float fontSize, int atlasWidth, int atlasHeight) {
    // フォントキーで重複ロードを防ぐ
    const std::string fontKey = MakeFontKey(fontFilePath, fontSize);
    if (fontDatas_.contains(fontKey)) {
        return;
    }

    assert(srvManager_->CanAllocate());

    // TTFファイルをバイナリとして丸ごと読み込む
    const std::string fullPath = "resources/fonts/" + fontFilePath;
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    assert(file.is_open());
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
    assert(bakeResult > 0);

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

    for (int y = 0; y < atlasHeight; ++y) {
        for (int x = 0; x < atlasWidth; ++x) {
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

    fontData.resource = dxCommon_->CreateTextureResource(metadata);
    fontData.intermediateResource = dxCommon_->UploadTextureData(fontData.resource, scratchImage);
    fontData.ttfBuffer = std::make_shared<std::vector<uint8_t>>(ttfBuffer);
    fontData.fontSize = fontSize;

    // SRVを割り当てて登録する
    fontData.srvIndex = srvManager_->Allocate() + kSRVIndexTop;
    fontData.srvHandleCPU = srvManager_->GetCPUDescriptorHandle(fontData.srvIndex);
    fontData.srvHandleGPU = srvManager_->GetGPUDescriptorHandle(fontData.srvIndex);

    srvManager_->CreateSRVforTexture2D(fontData.srvIndex, fontData.resource.Get(), metadata, 1);

    fontDatas_[fontKey] = std::move(fontData);
    ImGuiNotification::Post("フォントテクスチャを読み込みました: " + fontFilePath, {0.2f, 0.8f, 0.8f, 1.0f});
}

void TextureManager::Initialize(SrvManager *srvManager) {
    dxCommon_ = DirectXCommon::GetInstance();
    srvManager_ = srvManager;
    // SRVの数と同数
    textureDatas_.reserve(SrvManager::kMaxSRVCount);
}

void TextureManager::Finalize() {
    // 通常テクスチャのSRVインデックスを解放する
    for (auto &pair : textureDatas_) {
        srvManager_->Free(pair.second.srvIndex - kSRVIndexTop);
    }
    textureDatas_.clear();

    // フォントアトラスのSRVインデックスを解放する
    for (auto &pair : fontDatas_) {
        srvManager_->Free(pair.second.srvIndex - kSRVIndexTop);
    }
    fontDatas_.clear();
}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string &filePath) {
    // ファイル名を取り出して、resources/images/を付ける
    std::string newFilePath = "resources/images/" + filePath;

    auto it = textureDatas_.find(newFilePath);
    if (it != textureDatas_.end()) {
        return it->second.srvIndex;
    }

    // 見つからない場合はassertでエラーにする
    assert(0);
    return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string &filePath) {
    // 指定されたファイルパスが存在するかチェック
    assert(textureDatas_.find(filePath) != textureDatas_.end());

    TextureData &textureData = textureDatas_[filePath];
    return textureData.srvHandleGPU;
}

const DirectX::TexMetadata &TextureManager::GetMetaData(const std::string &filePath) {
    std::string fullPath = ("resources/images/" + filePath);
    // 指定されたファイルパスが存在するかチェック
    assert(textureDatas_.find(fullPath) != textureDatas_.end());

    TextureData &textureData = textureDatas_[fullPath];
    return textureData.metadata;
}

const TextureManager::FontData *TextureManager::GetFontData(const std::string &fontKey) const {
    auto it = fontDatas_.find(fontKey);
    if (it != fontDatas_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string TextureManager::MakeFontKey(const std::string &fontFilePath, float fontSize) {
    // フォントパスとサイズを組み合わせて一意なキーを生成する
    // floatをそのまま文字列化すると精度問題が出るためintに丸める
    return fontFilePath + "_" + std::to_string(static_cast<int>(fontSize));
}

std::vector<std::string> TextureManager::GetAllFontKeys() const {
    std::vector<std::string> keys;
    keys.reserve(fontDatas_.size());
    for (const auto &pair : fontDatas_) {
        keys.push_back(pair.first);
    }
    return keys;
}

void TextureManager::LoadAllTextures() {
    // 読み込み開始ディレクトリ
    std::filesystem::path baseDir = "resources/images";

    // ディレクトリが存在しない場合は早期リターン
    if (!std::filesystem::exists(baseDir)) {
        return;
    }

    // 再帰的探索
    for (auto &entry : std::filesystem::recursive_directory_iterator(baseDir)) {

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
        if (textureDatas_.contains("resources/images/" + file)) {
            continue;
        }

        // テクスチャを読み込む
        LoadTexture(file);
    }
}
} // namespace Hagine
