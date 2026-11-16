#pragma once
#include "BubbleData.h"
#include "BubbleGeometry.h"
#include "BubbleText.h"
#include <cstdint>
#include <d3d12.h>
#include <string>
#include <vector>
#include <wrl.h>

namespace Hagine {
class ViewProjection;

/// <summary>
/// 表示中の吹き出しを指す番号（0 は無効）
/// </summary>
using BubbleId = uint32_t;

/// <summary>
/// 吹き出しの表示・文字送り・描画と、エディタの窓
/// </summary>
class BubbleManager
{
  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static BubbleManager *GetInstance();

    /// <summary>
    /// スタイルと出し方の一覧（初回に読み込み）
    /// </summary>
    BubbleLibrary &GetLibrary();

    /// <summary>
    /// 保存した出し方で吹き出しを表示
    /// </summary>
    /// <param name="instanceName">出し方の名前</param>
    /// <returns>BubbleId: 表示した吹き出し（見つからなければ 0）</returns>
    BubbleId Show(const std::string &instanceName);

    /// <summary>
    /// 保存した出し方で、文字だけ差し替えて吹き出しを表示
    /// </summary>
    BubbleId Show(const std::string &instanceName, const std::string &text);

    /// <summary>
    /// 出し方を直接渡して吹き出しを表示
    /// </summary>
    BubbleId Show(const BubbleInstance &instance);

    /// <summary>
    /// 文字送り中なら全文を表示し、出しきっていれば閉じる
    /// </summary>
    void Advance(BubbleId id);

    /// <summary>
    /// 消える動きを付けて閉じる
    /// </summary>
    void Close(BubbleId id);

    /// <summary>
    /// すべての吹き出しをすぐに消去
    /// </summary>
    void Clear();

    /// <summary>
    /// 表示中か（閉じている途中も含む）
    /// </summary>
    bool IsShowing(BubbleId id) const;

    /// <summary>
    /// 文字送りの途中か
    /// </summary>
    bool IsTyping(BubbleId id) const;

    /// <summary>
    /// 出方・文字送り・自動で消えるまでの時間を更新
    /// </summary>
    void Update(float deltaTime);

    /// <summary>
    /// 表示中の吹き出しを描画（UIの合成中に呼ぶ）
    /// </summary>
    void Draw(const ViewProjection &viewProjection);

    /// <summary>
    /// エディタの窓の中身を描画
    /// </summary>
    void DrawEditor();

  private:
    BubbleManager() = default;
    ~BubbleManager() = default;
    BubbleManager(const BubbleManager &) = delete;
    BubbleManager &operator=(const BubbleManager &) = delete;

    /// <summary>
    /// 頂点1つ（Bubble.VS.hlsl の入力と同じ並び）
    /// </summary>
    struct Vertex
    {
        Vector2 position; // 画面上の位置 [px]
        Vector2 texcoord; // 画像内の位置
        Vector4 color;    // 掛ける色
    };

    /// <summary>
    /// 表示中の吹き出し1つ分の状態
    /// </summary>
    struct ActiveBubble
    {
        BubbleId id = 0;               // 番号
        BubbleInstance instance;       // 出し方
        BubbleStyle style;             // 見た目
        BubbleTextLayout text;         // 文字の配置と画像
        std::vector<float> revealTimes; // 文字ごとの出始める時刻 [秒]
        std::vector<float> charProgress; // 文字ごとの出る進み具合
        float typingEnd = 0.0f;        // 全文が出きる時刻 [秒]
        int textSlot = -1;             // 文字の画像の置き場所の番号
        D3D12_GPU_DESCRIPTOR_HANDLE textHandle{}; // 文字の画像
        float time = 0.0f;             // 表示してからの時間 [秒]
        float typingTime = 0.0f;       // 文字送りの経過時間 [秒]
        float idleTime = 0.0f;         // 全文が出てからの時間 [秒]
        bool closing = false;          // 閉じている途中か
        float closeTime = 0.0f;        // 閉じ始めてからの時間 [秒]
    };

    /// <summary>
    /// 1回の描画呼び出し（同じ画像を使う頂点の範囲）
    /// </summary>
    struct DrawCall
    {
        D3D12_GPU_DESCRIPTOR_HANDLE texture{}; // 使う画像
        uint32_t firstVertex = 0;              // 先頭の頂点
        uint32_t vertexCount = 0;              // 頂点の数
    };

    /// <summary>
    /// 番号から表示中の吹き出しを検索
    /// </summary>
    ActiveBubble *Find(BubbleId id);
    const ActiveBubble *Find(BubbleId id) const;

    /// <summary>
    /// 文字ごとの出始める時刻を算出
    /// </summary>
    static void BuildRevealTimes(const BubbleInstance &instance, const BubbleTextLayout &text, std::vector<float> &revealTimes, float &typingEnd);

    /// <summary>
    /// 文字送りの経過時間から文字ごとの出る進み具合を算出
    /// </summary>
    static void UpdateCharProgress(const BubbleInstance &instance, const std::vector<float> &revealTimes, float typingTime, std::vector<float> &charProgress);

    /// <summary>
    /// 出方の進み具合（イージング済み）を算出
    /// </summary>
    static float AppearProgress(const ActiveBubble &bubble);

    /// <summary>
    /// 追従先から本体の基準点を合わせる位置を算出
    /// </summary>
    /// <returns>bool: 追従先が見つからない・カメラの後ろなら false</returns>
    static bool ResolveAnchor(const BubbleInstance &instance, const ViewProjection &viewProjection, Vector2 &anchor);

    /// <summary>
    /// 文字の画像の置き場所を確保して画像を送信
    /// </summary>
    /// <returns>int: 置き場所の番号</returns>
    int UploadText(const BubbleTextLayout &text, D3D12_GPU_DESCRIPTOR_HANDLE &handle);

    /// <summary>
    /// 文字の画像の置き場所の登録名
    /// </summary>
    static std::string TextTextureKey(int slot);

    /// <summary>
    /// 画像の読み込みと、描画に使う番号・大きさの取得
    /// </summary>
    /// <returns>bool: 画像があれば true</returns>
    static bool ResolveTexture(const std::string &path, D3D12_GPU_DESCRIPTOR_HANDLE &handle, Vector2 &size);

    /// <summary>
    /// 四角形を頂点として追加し、描画呼び出しへまとめる
    /// </summary>
    static void AppendQuads(const std::vector<BubbleQuad> &quads, D3D12_GPU_DESCRIPTOR_HANDLE texture,
                            std::vector<Vertex> &vertices, std::vector<DrawCall> &calls);

    /// <summary>
    /// 頂点を GPU へ送れる場所を確保
    /// </summary>
    /// <returns>Vertex*: 書き込み先（確保できなければ nullptr）</returns>
    Vertex *ReserveVertices(size_t count, D3D12_GPU_VIRTUAL_ADDRESS &address);

    // ---- エディタ（BubbleManagerImGui.cpp）----

    /// <summary>
    /// スタイルの一覧と編集
    /// </summary>
    void DrawStyleTab();

    /// <summary>
    /// 出し方の一覧と編集
    /// </summary>
    void DrawInstanceTab();

    /// <summary>
    /// スタイルの詳細
    /// </summary>
    void DrawStyleDetails(BubbleStyle &style);

    /// <summary>
    /// 出し方の詳細
    /// </summary>
    void DrawInstanceDetails(BubbleInstance &instance);

    /// <summary>
    /// 吹き出しのプレビュー（しっぽはドラッグで移動）
    /// </summary>
    /// <returns>bool: しっぽの位置を変えたら true</returns>
    bool DrawPreview(const BubbleStyle &style, const BubbleInstance &instance, float &tailPosition, bool showGuides);

    /// <summary>
    /// 9スライスの境界をドラッグで編集
    /// </summary>
    bool DrawSliceEditor(BubbleStyle &style);

    /// <summary>
    /// しっぽの付け根をクリックで編集
    /// </summary>
    bool DrawTailPivotEditor(BubbleStyle &style);

    /// <summary>
    /// 変更済みの印を付ける
    /// </summary>
    void MarkDirty() { dirty_ = true; }

    BubbleLibrary library_;       // スタイルと出し方の一覧
    bool loaded_ = false;         // 一覧を読み込み済みか
    std::vector<ActiveBubble> bubbles_; // 表示中の吹き出し
    BubbleId nextId_ = 1;         // 次に振る番号
    std::vector<bool> textSlots_; // 文字の画像の置き場所の使用状況

    // ---- 描画 ----
    static constexpr uint32_t kFrameSlots = 3; // 頂点を書くバッファの数
    Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffers_[kFrameSlots]; // 頂点バッファ
    Vertex *mappedVertices_[kFrameSlots] = {};  // 頂点バッファの書き込み先
    size_t vertexCapacity_[kFrameSlots] = {};   // 頂点バッファの容量
    uint32_t frameSlot_ = 0;                    // 今のフレームで使うバッファ
    size_t vertexUsed_ = 0;                     // 今のフレームで使った頂点の数
    Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_; // 画面の大きさ
    Vector2 *mappedConstants_ = nullptr;        // 画面の大きさの書き込み先

    // ---- エディタ ----
    bool dirty_ = false;          // 保存していない変更があるか
    int selectedStyle_ = 0;       // 選択中のスタイル
    int selectedInstance_ = 0;    // 選択中の出し方
    std::string sampleText_ = "吹き出しの見本です。\n文字の量で大きさが変わります。"; // スタイルの見本の文字
    float sampleTailPosition_ = 0.6f; // スタイルの見本のしっぽの位置
    std::string previewSignature_; // プレビューの文字の画像を作ったときの入力
    BubbleTextLayout previewText_; // プレビューの文字の配置
    D3D12_GPU_DESCRIPTOR_HANDLE previewTextHandle_{}; // プレビューの文字の画像
    float previewTime_ = 1000.0f; // プレビューの再生時刻 [秒]
    bool draggingTail_ = false;   // プレビューでしっぽを掴んでいるか
    int sliceDragLine_ = -1;      // 9スライスの編集で掴んでいる線（-1 で無し）
    BubbleId lastShownId_ = 0;    // エディタからゲームに出した吹き出し
    std::vector<std::string> fontFiles_; // フォントの一覧
    bool fontListLoaded_ = false; // フォントの一覧を読み込み済みか
};

} // namespace Hagine
