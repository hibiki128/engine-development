#pragma once
// 部品クラス（DXDevice など）のヘッダーはここでは読まない（前方宣言だけにする）。
// このヘッダーは 200 本以上の .cpp が読むので、部品のヘッダーを1つ触っただけで
// ほぼ全体が再コンパイルになっていた。部品を直接使う処理は DirectXCommon.cpp に置く。
#include "DirectXTex/DirectXTex.h"
#include "WinApp.h"
#include "d3d12.h"
#include "dxcapi.h"
#include "dxgi1_6.h"
#include "string"
#include "wrl.h"
#include <memory>
#include <type/Vector4.h>

// シェーダーのリフレクション（d3d12shader.h）は使う .cpp だけが読む
struct ID3D12ShaderReflection;

namespace Hagine {
class DXDevice;
class ResourceFactory;
class DXCommandQueue;
class DXCommandList;
class DXSwapChain;
class RtvManager;
class DsvManager;
class ShaderCompiler;
class FrameRateLimiter;

/// <summary>
/// シーンを描き込む先（オフスクリーン／ポストエフェクトのチェーン／最終結果テクスチャ）の
/// フォーマット。リニア空間のFP16＝HDR。
///
/// 8bit sRGB では明るさが 1.0 で頭打ちになり、「光っているものの芯だけがとても明るい」
/// という情報が捨てられてしまう。そうなるとブルームは白くつぶれるだけになり、
/// 露出やトーンマップも効かせようがない。
/// 1.0 を超える値のまま持ち回り、ポストエフェクトのチェーン出口でトーンマップして
/// 0〜1 に収める（shaders/OffScreen/ToneMap.PS.hlsl）。
/// UI はトーンマップ後の最終結果テクスチャへ描くので、UIの色は影響を受けない。
///
/// ここを変えるときは、オフスクリーンへ描くすべてのPSOの RTVFormats も一緒に変えること。
/// </summary>
inline constexpr DXGI_FORMAT kSceneColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

/// <summary>
/// DirectX基盤の統括クラス（ファサード）
/// デバイス・コマンド・スワップチェーン・RTV/DSV・シェーダーコンパイラ等の
/// 各コンポーネントを所有し、フレーム進行（PreDraw/PostDraw）とリソースバリアを統括する
/// </summary>
class DirectXCommon
{
  private:
    // 部品を unique_ptr で持つので、生成・破棄は部品の定義が見える .cpp で行う
    DirectXCommon();
    ~DirectXCommon();
    DirectXCommon(DirectXCommon &) = delete;
    DirectXCommon &operator=(DirectXCommon &) = delete;

  public: // メンバ関数
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
    static DirectXCommon *GetInstance();

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(WinApp *winApp);

    /// <summary>
    /// オフスクリーンのSRV作成
    /// </summary>
    void CreateOffscreenSRV();

    /// <summary>
    /// depthのSRV作成
    /// </summary>
    void CreateDepthSRV();

    /// <summary>
    /// スワップチェーンを実ウィンドウサイズへリサイズする
    /// 内部レンダリング解像度（オフスクリーン等）は変えず、
    /// バックバッファと最終合成用ビューポート（レターボックス）のみ更新する
    /// </summary>
    /// <param name="width">新しいクライアント幅</param>
    /// <param name="height">新しいクライアント高さ</param>
    void ResizeSwapChain(uint32_t width, uint32_t height);

    /// <summary>
    /// 描画前処理(RenderTexture)
    /// </summary>
    void PreRenderTexture();

    /// <summary>
    /// 描画前処理
    /// </summary>
    void PreDraw();

    /// <summary>
    /// 深度のバリア
    /// </summary>
    void TransitionDepthBarrier();

    /// <summary>
    /// マルチステージ用描画前処理（バックバッファ遷移なし）
    /// </summary>
    void PreDrawForEffects();

    /// <summary>
    /// 描画後処理
    /// </summary>
    void PostDraw();

    void TransitionUAVBarrier(ID3D12Resource *pResource);
    void TransitionSRVBarrier();

    /// <summary>
    /// 深度バッファをシェーダーから読むときの状態。
    /// ピクセルシェーダーとコンピュートシェーダーの両方から読むため、読み取り状態を両方立てている
    /// （NON_PIXEL を落とすと、CS版ポストエフェクトが深度を読んだ時点で状態違反になる）。
    /// </summary>
    static constexpr D3D12_RESOURCE_STATES kDepthReadState =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    /// <summary>
    /// シェーダーをコンパイルする
    /// </summary>
    /// <param name="filePath">CompilerするShaderファイルへのパス</param>
    /// <param name="profile">Compilerに使用するProfile</param>
    IDxcBlob *CompileShader(const std::wstring &filePath, const wchar_t *profile);

    /// <summary>
    /// シェーダーをコンパイルし、リフレクション情報も取得する
    /// ルートシグネチャをシェーダーの宣言から自動生成したい場合に使う
    /// </summary>
    /// <param name="filePath">CompilerするShaderファイルへのパス</param>
    /// <param name="profile">Compilerに使用するProfile</param>
    /// <param name="ppReflection">リフレクションの受け取り先（呼び出し側が Release すること）</param>
    IDxcBlob *CompileShaderWithReflection(const std::wstring &filePath, const wchar_t *profile,
                                          ID3D12ShaderReflection **ppReflection);

    /// <summary>
    /// コンパイルが通るかだけを確かめる（失敗しても止まらない）
    /// シェーダーのホットリロードで、作り直す前に安全かどうかを見るのに使う
    /// </summary>
    /// <param name="filePath">CompilerするShaderファイルへのパス</param>
    /// <param name="profile">Compilerに使用するProfile</param>
    /// <param name="outError">失敗したときのDXCのメッセージ（省略可）</param>
    /// <returns>bool: コンパイルが通れば true</returns>
    bool TryCompileShader(const std::wstring &filePath, const wchar_t *profile,
                          std::string *outError = nullptr);

    // Resourceの作成
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes, bool isUAV = false);

    // DirectX12のTextureResourceを作る
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(const DirectX::TexMetadata &metadata);

    Microsoft::WRL::ComPtr<ID3D12Resource> CreateRenderTextureResource(uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_CLEAR_VALUE color, bool allowUAV = false);

    // メイン深度以外の追加深度ステンシルリソースを生成する（プレビュー窓など）。DEPTH_WRITE 状態で返る。
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateAdditionalDepthResource(int32_t width, int32_t height);

    // ExecuteIndirect(DispatchIndirect) 用のコマンドシグネチャ（Phase 3 の sim 生存数ディスパッチ基盤）。
    ID3D12CommandSignature *GetDispatchIndirectCommandSignature();

    [[nodiscard]]
    Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage &mipImages);

    /// <summary>
    /// バリアを貼る
    /// </summary>
    /// <param name="pResource"></param>
    /// <param name="Before"></param>
    /// <param name="After"></param>
    /// <param name="subresource">遷移させるサブリソース。既定は0番（深度ステンシルなら深度面、テクスチャなら最上位ミップ）だけ。
    /// リソース丸ごとを扱う CopyResource の前後では D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES を渡すこと
    /// （深度ステンシルはステンシル面が1番にあり、0番だけでは遷移しきれない）</param>
    void BarrierTransition(ID3D12Resource *pResource, D3D12_RESOURCE_STATES Before, D3D12_RESOURCE_STATES After,
                           UINT subresource = 0);
    D3D12_CPU_DESCRIPTOR_HANDLE CreateAdditionalRTV(ID3D12Resource *resource, int index);
#pragma region getter
    /// <summary>
    /// RTVの指定番号のCPUデスクリプタハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTVCPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// RTVの指定番号のGPUデスクリプタハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetRTVGPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// DSVの指定番号のCPUデスクリプタハンドルを取得する
    /// </summary>
    D3D12_CPU_DESCRIPTOR_HANDLE GetDSVCPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// DSVの指定番号のGPUデスクリプタハンドルを取得する
    /// </summary>
    D3D12_GPU_DESCRIPTOR_HANDLE GetDSVGPUDescriptorHandle(uint32_t index);

    /// <summary>
    /// コマンドリストの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetCommandList();

    /// <summary>
    /// デバイスの取得
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12Device> GetDevice();

    /// <summary>
    /// レイトレーシング用のデバイス／コマンドリストを取得する。
    /// どちらも非対応環境では nullptr を返すので、使う前に IsRaytracingSupported() を見ること
    /// </summary>
    ID3D12Device5 *GetDevice5();
    ID3D12GraphicsCommandList4 *GetCommandList4();

    /// <summary>
    /// インラインレイトレーシング（RayQuery）が使えるか
    /// </summary>
    /// <returns>bool: 使えるなら true</returns>
    bool IsRaytracingSupported() const;

    /// <summary>
    /// DescriptorHeapの作成
    /// </summary>
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible);
    ID3D12Resource *GetOffScreenResource() { return offScreenResource_.Get(); }
    IDxcUtils *GetDxcUtils();
    IDxcCompiler3 *GetDxcCompiler();

    Vector4 GetClearColor() const
    {
        return Vector4(
            clearColorValue_.Color[0], // R
            clearColorValue_.Color[1], // G
            clearColorValue_.Color[2], // B
            clearColorValue_.Color[3]  // A
        );
    }

    // バックバッファの数を取得
    size_t GetBackBufferCount() const;

    D3D12_GPU_DESCRIPTOR_HANDLE GetOffScreenGPUHandle() { return offScreenSrvHandleGPU_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetOffScreenCPUHandle() { return offScreenSrvHandleCPU_; }
    uint32_t GetOffScreenSrvIndex() { return offScreenSrvIndex_; }
    /// <summary>シーンのカラーターゲットへコンピュートから書き込むためのUAV番号（ブルームの合成が使う）</summary>
    uint32_t GetOffScreenUavIndex() const { return offScreenUavIndex_; }

    D3D12_GPU_DESCRIPTOR_HANDLE GetDepthGPUHandle() { return depthSrvHandleGPU_; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetDepthCPUHandle() { return depthSrvHandleCPU_; }
    uint32_t GetDepthSrvIndex() { return depthSrvIndex_; }

    /// <summary>
    /// 深度バッファを読み取り用のテクスチャへ複製する。
    ///
    /// ソフトパーティクル（粒が地物へ刺さった断面を消す）のように、
    /// **描画の最中に深度を読みたい**ときに使う。
    /// 深度バッファそのものは描画中 DEPTH_WRITE のままなので直接は読めず、
    /// 読み取り専用DSVへ差し替える手もあるが、複製の方が扱いが単純で事故が少ない。
    /// パーティクルを描く直前に1回だけ呼ぶこと
    /// </summary>
    void CaptureDepthForRead();

    /// <summary>
    /// 「このフレームは深度の複製が要る」と申告する。
    /// ソフトパーティクルを使うグループが Update で呼ぶ。申告が無いフレームは
    /// CaptureDepthForRead が何もしないので、使わないゲームでは複製も確保もされない
    /// </summary>
    void RequestDepthCapture() { depthCaptureRequested_ = true; }

    /// <summary>CaptureDepthForRead で複製した深度のSRV番号（R24_UNORM_X8 として読む）</summary>
    uint32_t GetDepthCopySrvIndex() const { return depthCopySrvIndex_; }
    D3D12_CLEAR_VALUE GetClearColorValue() const { return clearColorValue_; }
    IDXGISwapChain4 *GetSwapChain();
    // バックバッファへの最終合成に使うビューポート／シザー（レターボックス済み）
    const D3D12_VIEWPORT &GetPresentViewport() const { return presentViewport_; }
    const D3D12_RECT &GetPresentScissorRect() const { return presentScissorRect_; }
    // オフスクリーン描画用のビューポート／シザー（仮想解像度固定）
    const D3D12_VIEWPORT &GetRenderViewport() const { return viewport_; }
    const D3D12_RECT &GetRenderScissorRect() const { return scissorRect_; }
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GetRTVDescriptorHeap();
    /// 深度ステンシルリソースを取得（ディファードのライトカリング／ライティングが読むため）
    ID3D12Resource *GetDepthStencilResource() { return depthStencilResource_.Get(); }

    // ---- 非同期コンピュートキュー API ----
    /// コンピュートコマンドリストを取得
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> GetComputeCommandList();
    /// Direct(graphics) コマンドキューを取得（GPUタイムスタンプ周波数取得などに使用）
    ID3D12CommandQueue *GetCommandQueue();
    /// Compute コマンドキューを取得（GPUタイムスタンプ周波数取得などに使用）
    ID3D12CommandQueue *GetComputeCommandQueue();
    /// コンピュートコマンドをGPUに送信し完了フェンスを発行する
    void ExecuteComputeCommands();
    /// Direct Queue が Compute Queue の完了を GPU 側で待機する（CPU はブロックしない）
    void WaitForComputeOnDirectQueue();
    /// フレーム先頭でコンピュートリストにデスクリプタヒープを設定する
    void BeginComputeFrame();
    /// シャットダウン時：コンピュートキューの全作業を CPU 側で完了させる
    void FlushComputeQueue();

    /// <summary>
    /// GPU の全作業完了を CPU 側で待つ（Direct/Compute 両キューをフラッシュ）。
    /// シーン破棄など、GPU がまだ参照している可能性のあるリソースを解放する前に呼ぶこと。
    /// （デバッグレイヤーは「使用中リソースの解放」を ERROR としてブレークするため）
    /// </summary>
    void WaitForGPU();
#pragma endregion

  private: // メンバ関数
    /// <summary>
    /// レンダーターゲットビューの初期化（バックバッファ＋オフスクリーン）
    /// </summary>
    void RenderTargetViewInitialize();

    /// <summary>
    /// ビューポート矩形の初期化
    /// </summary>
    void ViewPortRectInitialize();

    /// <summary>
    /// シザリング矩形の初期化
    /// </summary>
    void ScissorRectInitialize();

    /// <summary>
    /// 最終合成用ビューポート／シザーをクライアントサイズから再計算する（レターボックス）
    /// </summary>
    /// <param name="clientWidth">実クライアント幅</param>
    /// <param name="clientHeight">実クライアント高さ</param>
    void UpdatePresentViewport(uint32_t clientWidth, uint32_t clientHeight);

  private:
    // WindowsAPI
    WinApp *pWinApp_ = nullptr;

    // ---- 分割された各コンポーネント（DirectXCommon が所有・統括する）----
    std::unique_ptr<DXDevice> dxDevice_;                // デバイス・アダプタ・ファクトリ
    std::unique_ptr<ResourceFactory> resourceFactory_;  // 各種GPUリソースの生成
    std::unique_ptr<DXCommandQueue> directQueue_;       // Direct キュー＋フェンス
    std::unique_ptr<DXCommandList> directCommandList_;  // Direct リスト＋アロケータ
    std::unique_ptr<DXCommandQueue> computeQueue_;      // 非同期 Compute キュー＋フェンス
    std::unique_ptr<DXCommandList> computeCommandList_; // Compute リスト＋アロケータ
    std::unique_ptr<DXSwapChain> swapChain_;            // スワップチェーン＋バックバッファ
    std::unique_ptr<RtvManager> rtvManager_;            // RTVヒープ・ビュー管理
    std::unique_ptr<DsvManager> dsvManager_;            // DSVヒープ・ビュー管理
    std::unique_ptr<ShaderCompiler> shaderCompiler_;    // DXCシェーダーコンパイラ
    std::unique_ptr<FrameRateLimiter> fpsLimiter_;      // FPS固定

    // ---- メインの描画先リソース ----
    Microsoft::WRL::ComPtr<ID3D12Resource> offScreenResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource_;
    D3D12_CLEAR_VALUE clearColorValue_{};

    // ---- フレーム同期（ダブルバッファ）----
    // 同時に進めるフレーム数（DXCommandList::kFrameCount と同じ値。.cpp の static_assert で確かめている）
    static constexpr UINT kFrameCount = 2;
    UINT64 fenceValues_[kFrameCount] = {}; // フレームごとの最終 Signal 値
    UINT frameIndex_ = 0;                                 // 現在の描画フレームスロット（0 or 1）

    // ビューポート（オフスクリーン描画用・仮想解像度固定）
    D3D12_VIEWPORT viewport_{};
    // シザー矩形（オフスクリーン描画用・仮想解像度固定）
    D3D12_RECT scissorRect_{};
    // 最終合成用ビューポート（実ウィンドウサイズ・レターボックス済み）
    D3D12_VIEWPORT presentViewport_{};
    // 最終合成用シザー矩形
    D3D12_RECT presentScissorRect_{};
    // TransitionBarrierの設定
    D3D12_RESOURCE_BARRIER barrier_{};

    // オフスクリーン・深度のSRVハンドル
    uint32_t offScreenSrvIndex_ = 0;
    uint32_t offScreenUavIndex_ = 0; // ブルームの合成がシーンへ足し込むためのUAV
    D3D12_CPU_DESCRIPTOR_HANDLE offScreenSrvHandleCPU_{}; // SRV作成時に必要なCPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE offScreenSrvHandleGPU_{}; // 描画コマンドに必要なGPUハンドル

    uint32_t depthSrvIndex_ = 0;
    // 描画中に深度を読むための複製（ソフトパーティクル用）。必要になった最初のフレームで作る
    Microsoft::WRL::ComPtr<ID3D12Resource> depthCopyResource_;
    uint32_t depthCopySrvIndex_ = 0;
    bool depthCaptureRequested_ = false; // このフレームに複製要求があったか
    D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandleCPU_{}; // SRV作成時に必要なCPUハンドル
    D3D12_GPU_DESCRIPTOR_HANDLE depthSrvHandleGPU_{}; // 描画コマンドに必要なGPUハンドル
};
} // namespace Hagine
