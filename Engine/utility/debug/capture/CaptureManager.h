#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <DirectXTex/DirectXTex.h>

namespace Hagine {

class DirectXCommon;

/// <summary>
/// 何を撮るか
/// </summary>
enum class CaptureSource
{
    GameView, // オフスクリーン（エディタUIを含まない、ゲーム画面そのもの）
    Window,   // バックバッファ（エディタUI込みの、画面に出ているとおり）
};

/// <summary>
/// 画面の撮影。1枚のスクリーンショットと、連番での録画を扱う。
///
/// 撮影そのものはフレーム末尾（Present 直後）に行い、
/// PNG/JPEG への変換と書き出しは別スレッドへ回す。
/// 画像のエンコードは1枚あたり数十ミリ秒かかるので、
/// ゲームスレッドでやると録画中だけカクついてしまうため。
/// </summary>
class CaptureManager
{
  private:
    CaptureManager() = default;
    ~CaptureManager() = default;
    CaptureManager(const CaptureManager &) = delete;
    CaptureManager &operator=(const CaptureManager &) = delete;

  public:
    /// ====================================
    /// public method
    /// ====================================

    /// <summary>シングルトンインスタンスの取得</summary>
    static CaptureManager *GetInstance();

    /// <summary>
    /// 初期化する
    /// </summary>
    /// <param name="dxCommon">撮影対象のリソースを持つ DirectX 基盤</param>
    void Initialize(DirectXCommon *dxCommon);

    /// <summary>書き出し待ちを片付けてからスレッドを畳む</summary>
    void Finalize();

    /// <summary>次のフレーム末尾に1枚だけ撮る</summary>
    void RequestScreenshot();

    /// <summary>連番での録画を始める</summary>
    void StartSequence();

    /// <summary>連番での録画を止める</summary>
    void StopSequence();

    /// <summary>録画中かどうか</summary>
    bool IsRecording() const { return recording_; }

    /// <summary>
    /// 何を撮るかを切り替える。GameView はポストエフェクト前の3D描画先、
    /// Window は画面に出ているとおり（ポストエフェクト・トーンマップ・UI込み）
    /// </summary>
    void SetSource(CaptureSource source) { source_ = static_cast<int>(source); }

    /// <summary>
    /// フレーム末尾の処理。撮影要求があればここで実際に読み出す。
    /// Present の直後に呼ぶこと（それ以前だと、まだ描き終わっていない絵を撮ってしまう）
    /// </summary>
    void EndFrame();

    /// <summary>
    /// 設定ウィンドウを描く
    /// </summary>
    /// <param name="open">表示フラグ。閉じるボタンで false になる</param>
    void DrawImGui(bool *open);

    /// <summary>撮影したファイルの置き場</summary>
    /// <returns>const std::string&amp;: フォルダパス</returns>
    const std::string &GetOutputDirectory() const { return outputDirectory_; }

    /// <summary>書き出し待ちの枚数</summary>
    int GetPendingCount() const;

  private:
    /// ====================================
    /// private structures
    /// ====================================

    /// 書き出し待ちの1枚
    struct PendingImage
    {
        DirectX::ScratchImage image;
        std::string path;
    };

    /// ====================================
    /// private method
    /// ====================================

    /// 書き出し担当スレッドの本体
    void EncoderThreadMain();

    /// 撮影対象のリソースを読み出して、書き出し待ちへ積む
    void CaptureOnce(const std::string &path);

    /// 出力先の連番ファイル名を作る
    std::string MakeFilePath(const char *prefix) const;

  private:
    /// ====================================
    /// private variables
    /// ====================================

    DirectXCommon *pDxCommon_ = nullptr;

    std::string outputDirectory_ = "Captures";
    int source_ = static_cast<int>(CaptureSource::GameView);
    bool useJpeg_ = false;       // true なら JPEG（速いがにじむ）、false なら PNG
    int jpegQualityPercent_ = 90;

    bool screenshotRequested_ = false;
    int screenshotCounter_ = 0; // 同じ秒に続けて撮っても上書きしないよう、ファイル名に付ける通し番号
    bool recording_ = false;
    int frameStride_ = 2;      // 何フレームに1枚撮るか。2 なら 60fps 時に 30fps 相当
    int frameCounter_ = 0;
    int sequenceIndex_ = 0;
    int recordedCount_ = 0;
    int droppedCount_ = 0;     // 書き出しが追いつかず捨てた枚数
    int maxPendingImages_ = 24; // 溜められる枚数の上限（メモリを食い潰さないため）

    // 自動撮影（環境変数 HAGINE_AUTO_CAPTURE="秒,秒,..."。キー入力なしで画面を確かめる用）
    void UpdateAutoCapture();
    bool autoCaptureParsed_ = false;
    std::deque<float> autoCaptureTimes_; // 起動からの秒数（小さい順）
    float autoCaptureElapsed_ = 0.0f;

    std::string lastSavedPath_;

    // --- 書き出しスレッド ---
    std::thread encoderThread_;
    std::atomic<bool> threadRunning_{false};
    mutable std::mutex queueMutex_;
    std::condition_variable queueSignal_;
    std::deque<PendingImage> pendingImages_;
};

} // namespace Hagine
