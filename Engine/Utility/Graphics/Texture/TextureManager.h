#pragma once
#include <imgui/imstb_truetype.h>
#include "DirectXCommon.h"
#include "d3d12.h"
#include "DirectXTex/DirectXTex.h"
#include "string"
#include "unordered_map"
#include "wrl.h"
#include <Graphics/Srv/SrvManager.h>
#include <array>
#include <memory>

namespace Hagine {
class TextureManager {
  private:
    TextureManager() = default;
    ~TextureManager() = default;
    TextureManager(TextureManager &) = delete;
    TextureManager &operator=(TextureManager &) = delete;

    // -----------------------------------------------------------------------
    // 公開型定義
    // GetFontData() の戻り値として呼び出し側が参照するため public に定義する
    // C++ では非末尾の戻り値型はクラス外スコープで解決されるため、
    // メソッド宣言より前に定義しておく必要がある
    // -----------------------------------------------------------------------
  public:
    // フォントアトラス1つ分のデータ
    // ASCII 32('space') 〜 127 の96文字をベイク対象とする
    struct FontData {
        std::array<stbtt_bakedchar, 96> charData; // 各グリフのUV・オフセット情報
        int atlasWidth;
        int atlasHeight;
        uint32_t srvIndex;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
        // CPU側でテキストテクスチャを合成するために保持するグレースケールアトラス
        std::vector<uint8_t> atlasPixels;
        std::shared_ptr<std::vector<uint8_t>> ttfBuffer; // TTFファイルの生データ
        float fontSize;                                  // フォントサイズ
    };

    // -----------------------------------------------------------------------
    // 公開メソッド
    // -----------------------------------------------------------------------
  public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(SrvManager *srvManager);

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static TextureManager *GetInstance() {
        static TextureManager instance;
        return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// テクスチャファイルの読み込み
    /// </summary>
    void LoadTexture(const std::string &filePath);

    /// <summary>
    /// resources/images/ 配下のテクスチャを再帰的に全て読み込む
    /// </summary>
    void LoadAllTextures();

    /// <summary>
    /// TTFフォントファイルからグリフアトラステクスチャを生成してSRVに登録する
    /// fontFilePath : resources/fonts/ からの相対パス
    /// fontSize     : ベイクするフォントサイズ（ピクセル単位）
    /// atlasWidth   : アトラステクスチャの幅（2のべき乗推奨）
    /// atlasHeight  : アトラステクスチャの高さ（2のべき乗推奨）
    /// </summary>
    void LoadFontTexture(const std::string &fontFilePath, float fontSize, int atlasWidth = 512, int atlasHeight = 512);

    /// <summary>
    /// ファイルパスとフォントサイズからSRVインデックスを取得する
    /// </summary>
    uint32_t GetTextureIndexByFilePath(const std::string &filePath);

    /// <summary>
    /// ファイルパスからGPUデスクリプタハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string &filePath);

    /// <summary>
    /// ファイルパスからテクスチャのメタデータを取得する
    /// </summary>
    const DirectX::TexMetadata &GetMetaData(const std::string &filePath);

    /// <summary>
    /// フォントキーからフォントアトラスデータを取得する
    /// フォントキーは MakeFontKey() で生成したものを使用すること
    /// 見つからない場合は nullptr を返す
    /// </summary>
    const FontData *GetFontData(const std::string &fontKey) const;

    /// <summary>
    /// フォントキーを生成する（マップ検索用）
    /// fontFilePath と fontSize の組み合わせで一意なキーを作る
    /// </summary>
    static std::string MakeFontKey(const std::string &fontFilePath, float fontSize);

    /// <summary>
    /// ロード済みフォントのキー一覧を返す（TextRendererのUI用）
    /// </summary>
    std::vector<std::string> GetAllFontKeys() const;

    bool GetDDS() { return isDDS_; }

    SrvManager *GetSrvManager() { return srvManager_; }

    // -----------------------------------------------------------------------
    // 非公開型・メンバ
    // -----------------------------------------------------------------------
  private:
    // テクスチャ1枚分のデータ
    struct TextureData {
        DirectX::TexMetadata metadata;                   // 画像の幅や高さなどの情報
        Microsoft::WRL::ComPtr<ID3D12Resource> resource; // テクスチャリソース
        Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
        uint32_t srvIndex;
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU; // SRV作成時に必要なCPUハンドル
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU; // 描画コマンドに必要なGPUハンドル
    };

    // ファイルパスをキーとするテクスチャデータのマップ
    std::unordered_map<std::string, TextureData> textureDatas_;

    // MakeFontKey() で生成したキーをキーとするフォントデータのマップ
    std::unordered_map<std::string, FontData> fontDatas_;

    DirectXCommon *dxCommon_ = nullptr;
    SrvManager *srvManager_ = nullptr;

    bool isDDS_ = false;

    // ImGuiが0番を使用するため1番から使用するSRVインデックスの開始番号
    static uint32_t kSRVIndexTop;
};
} // namespace Hagine
