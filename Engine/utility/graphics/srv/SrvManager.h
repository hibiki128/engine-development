#pragma once
#include <cstdint>
#include <d3d12.h>
#include <queue> // 空きインデックスの管理用
#include <vector>
#include <wrl.h>
#include "DirectXTex/DirectXTex.h"

namespace Hagine {
class DirectXCommon;

class SrvManager
{
  private:
    SrvManager() = default;
    ~SrvManager() = default;
    SrvManager(SrvManager &) = delete;
    SrvManager &operator=(SrvManager &) = delete;

  private:
    DirectXCommon *pDxCommon_ = nullptr;

    // SRV用のでスクリプタサイズ
    uint32_t descriptorSize_;
    // SRV用デスクリプタヒープ
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;
    // 次に使用するSRVインデックス
    uint32_t useIndex_ = 0;
    // 空きインデックスを管理するキュー
    std::queue<uint32_t> freeIndices_; // 解放されたSRVインデックスを保存

    /// <summary>解放待ちのインデックス（GPU が触り終わるまで抱えておく）</summary>
    struct PendingFree
    {
        uint32_t srvIndex = 0; //!< Allocate() が返した予約番号
        int framesLeft = 0;    //!< あと何フレーム待つか
    };
    // バックバッファは2枚で、PostDraw は「2フレーム前の完了」までしか保証しない。
    // それより1フレーム多く待ってから解放する（ModelManager と同じ考え方）
    static constexpr int kFreeDelayFrames = 3;
    std::vector<PendingFree> pendingFrees_;

  public:
    // 最大SRV数(最大テクスチャ枚数)
    static const uint32_t kMaxSRVCount;

    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static SrvManager *GetInstance()
    {
        static SrvManager instance;
        return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

  public:
    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize();

    /// <summary>
    /// 描画前処理
    /// </summary>
    void SetDescriptorHeap();

    /// <summary>
    /// SRV生成(テクスチャ用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="Format"></param>
    /// <param name="MipLevels"></param>
    void CreateSRVforTexture2D(uint32_t srvIndex, ID3D12Resource *pResource, DirectX::TexMetadata metaData, UINT MipLevels);

    /// <summary>
    /// SRV生成(Structured Buffer用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateSRVforStructuredBuffer(uint32_t srvIndex, ID3D12Resource *pResource, UINT numElements, UINT structureByteStride);

    /// <summary>
    /// SRV生成(RenderTexture用)
    /// </summary>
    /// <param name="srvIndex">SRV番号</param>
    /// <param name="pResource">対象リソース</param>
    /// <param name="format">SRVのフォーマット。UNKNOWNならリソース自身のフォーマットを使う</param>
    void CreateSRVforRenderTexture(uint32_t srvIndex, ID3D12Resource *pResource, DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN);

    /// <summary>
    /// SRV生成(Depth用)
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateSRVforDepth(uint32_t srvIndex, ID3D12Resource *pResource);

    /// <summary>
    /// SRV生成(シャドウマップ用)
    /// シャドウマップは D32_FLOAT なのでシーンの深度（D24_UNORM_S8_UINT）とフォーマットが違う。
    /// 同じ CreateSRVforDepth を使うと読めない値になるため別にしてある
    /// </summary>
    /// <param name="srvIndex">SRVインデックス</param>
    /// <param name="pResource">シャドウマップの深度リソース</param>
    void CreateSRVforShadowDepth(uint32_t srvIndex, ID3D12Resource *pResource);

    /// <summary>
    /// SRV生成(キューブマップ用)。
    /// 環境マップを別のテーブルへ差したいときに、同じリソースからSRVを作り直すために使う
    /// </summary>
    /// <param name="srvIndex">SRVインデックス</param>
    /// <param name="pResource">キューブマップのリソース</param>
    /// <param name="format">読み取りフォーマット</param>
    /// <param name="mipLevels">ミップ数</param>
    void CreateSRVforTextureCube(uint32_t srvIndex, ID3D12Resource *pResource,
                                 DXGI_FORMAT format, UINT mipLevels);

    /// <summary>
    /// SRV生成(レイトレーシングの加速構造用)。
    /// 加速構造のSRVは「リソースではなくGPUアドレス」を指すので、渡すのはアドレス
    /// </summary>
    /// <param name="srvIndex">SRVインデックス</param>
    /// <param name="address">TLASのGPUアドレス</param>
    void CreateSRVforTlas(uint32_t srvIndex, D3D12_GPU_VIRTUAL_ADDRESS address);

    /// <summary>
    /// UAV作成
    /// </summary>
    /// <param name="srvIndex"></param>
    /// <param name="pResource"></param>
    /// <param name="numElements"></param>
    /// <param name="structureByteStride"></param>
    void CreateUAVStructuredBuffer(uint32_t srvIndex, ID3D12Resource *pResource, UINT numElements, UINT structureByteStride);

    /// <summary>
    /// 2Dテクスチャ用のUAVを作る（コンピュートシェーダーの書き込み先）
    /// ※ sRGB フォーマットには UAV を作れないので、リソースは非sRGBで生成しておくこと
    /// </summary>
    /// <param name="srvIndex">デスクリプタの番号</param>
    /// <param name="pResource">対象リソース</param>
    /// <param name="format">ビューのフォーマット</param>
    void CreateUAVforTexture2D(uint32_t srvIndex, ID3D12Resource *pResource, DXGI_FORMAT format);

    /// <summary>
    /// インデックス割り当て
    /// </summary>
    /// <returns></returns>
    uint32_t Allocate();

    /// <summary>
    /// インデックス解放。
    /// **+1規約なので、渡すのは `Allocate()` が返した予約番号**（＝使っている番号 - 1）。
    /// 呼ぶ前に GPU の完了が保証できていること（できないなら FreeDeferred を使う）
    /// </summary>
    /// <param name="srvIndex">Allocate() が返した予約番号</param>
    void Free(uint32_t srvIndex);

    /// <summary>
    /// インデックスを数フレーム後に解放する。
    ///
    /// Free はディスクリプタを潰したうえで番号を空きリストへ戻すので、
    /// GPU がまだ前のフレームのコマンドでそのスロットを読んでいると壊れる。
    /// フレームの途中でオブジェクトが壊れる経路（キャラの破棄など）はこちらを使う。
    /// 実際に解放するのは Update() の仕事
    /// </summary>
    /// <param name="srvIndex">Allocate() が返した予約番号</param>
    void FreeDeferred(uint32_t srvIndex);

    /// <summary>
    /// 解放待ちのインデックスのうち、GPU が触り終わったものを実際に解放する。
    /// フレームの先頭で1回だけ呼ぶこと
    /// </summary>
    void Update();

    bool CanAllocate() const;
    void ClearDescriptor(uint32_t srvIndex);

    /// <summary>
    /// これまでに払い出した番号の最大値（＝使用中 ＋ 空きリストにある数）。
    /// 解放漏れを疑ったときに、増え続けていないかを見るための値
    /// </summary>
    uint32_t GetAllocatedCount() const { return useIndex_; }

    /// <summary>空きリストに戻っている番号の数</summary>
    size_t GetFreeCount() const { return freeIndices_.size(); }
    /// <summary>
    /// getter
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);
    ID3D12DescriptorHeap *GetDescriptorHeap() const
    {
        return descriptorHeap_.Get(); // 管理してるSRVヒープ
    }

    /// <summary>
    /// setter
    /// </summary>
    /// <param name="RootParameterIndex"></param>
    /// <param name="srvIndex"></param>
    void SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);
};
} // namespace Hagine
