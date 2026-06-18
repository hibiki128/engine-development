#pragma once
#include "array"
#include "cstdint"
#include "memory"
#include "string"
#include "vector"
#include "wrl.h"
#include "xaudio2.h"
#include <filesystem>
#include <map>
#include <set>

namespace Hagine {
class Audio {

    class VoiceCallback : public IXAudio2VoiceCallback {
      public:
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
        void STDMETHODCALLTYPE OnBufferStart(void *) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void *) override {}
        void STDMETHODCALLTYPE OnVoiceError(void *, HRESULT) override {}

        void STDMETHODCALLTYPE OnBufferEnd(void *pBufferContext) override {
            if (pBufferContext) {
                Voice *voice = reinterpret_cast<Voice *>(pBufferContext);
                if (voice && voice->sourceVoice) {
                    voice->sourceVoice = nullptr;
                }
            }
        }
    };

  private:
    static const int kMaxSoundData = 2108;

    Audio() = default;
    ~Audio() = default;
    Audio(Audio &) = default;
    Audio &operator=(Audio &) = default;

  private:
    struct ChunkHeader {
        char id[4];
        int32_t size;
    };

    struct RiffHeader {
        ChunkHeader chunk;
        char type[4];
    };

    struct FormatChunk {
        ChunkHeader chunk;
        WAVEFORMATEX fmt;
    };

    struct SoundData {
        WAVEFORMATEX wfex;
        std::vector<uint8_t> buffer;
        std::string name_;
    };

    struct Voice {
        uint32_t handle = 0u;
        IXAudio2SourceVoice *sourceVoice = nullptr;
        float volume = 1.0f;
        std::unique_ptr<VoiceCallback> callback;
    };

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    static Audio *GetInstance() {
        static Audio instance;
        return &instance;
    }

    /// <summary>
    /// 初期化
    /// </summary>
    void Initialize(const std::string &directoryPath = "resources/sounds");

    /// <summary>
    /// 音声読み込み
    /// </summary>
    uint32_t LoadWave(const std::string &filename);

    /// <summary>
    /// 音声データ解放
    /// </summary>
    void Unload(uint32_t soundIndex);

    /// <summary>
    /// 音声再生
    /// </summary>
    void PlayWave(uint32_t soundIndex, float volume, bool loop = false);

    /// <summary>
    /// 音声停止
    /// </summary>
    void StopWave(uint32_t soundIndex);

    /// <summary>
    /// 音量設定
    /// </summary>
    void SetVolume(uint32_t soundIndex, float volume);

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();

    /// <summary>
    /// 再生終了済みボイスを解放（毎フレーム呼び出し推奨）
    /// </summary>
    void CleanupFinishedVoices();

    /// <summary>
    /// ImGui デバッグウィンドウ描画
    /// _DEBUG ビルド時に毎フレーム呼び出してください
    /// </summary>
    void Debug();

  private:
    //------------------------------------------------------------------
    // デバッグ補助関数（private）
    //------------------------------------------------------------------

    /// Resources\sounds\ を再帰スキャンして .wav ファイル一覧を更新する
    void DebugScanWavFiles();

    /// 指定インデックスの音声総時間（秒）を返す
    float DebugGetDurationSec(uint32_t index) const;

    /// 指定インデックスの現在再生位置（秒）を返す（再生中でなければ 0）
    float DebugGetPositionSec(uint32_t index) const;

    /// 指定インデックスが再生中かどうかを返す
    bool DebugIsPlaying(uint32_t index) const;

    /// ロード済みファイル名 → soundIndex を解決する（未ロードなら UINT32_MAX）
    uint32_t DebugResolveIndex(const std::string &filename) const;

  private:
    //------------------------------------------------------------------
    // 通常メンバ
    //------------------------------------------------------------------
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2_;
    IXAudio2MasteringVoice *masterVoice_ = nullptr;
    std::string directoryPath_;
    std::array<SoundData, kMaxSoundData> soundDatas_;
    size_t soundDataIndex_ = 0;
    std::set<std::unique_ptr<Voice>> voices_;
    std::set<std::string> loadedFiles_;

    uint16_t audioFormat_ = 0;
    uint16_t numChannels_ = 0;
    uint32_t sampleRate_ = 0;
    uint32_t byteRate_ = 0;
    uint16_t blockAlign_ = 0;
    uint16_t bitsPerSample_ = 0;

    //------------------------------------------------------------------
    // デバッグ専用メンバ
    //------------------------------------------------------------------
    std::vector<std::string> debugWavFileList_;      // スキャン済み .wav 一覧
    int debugSelectedFile_ = -1;                     // リスト選択インデックス
    float debugVolume_ = 1.0f;                       // 再生ボリューム
    bool debugLoop_ = false;                         // ループフラグ
    float debugMasterVolume_ = 1.0f;                 // マスター音量
    std::map<std::string, uint32_t> debugLoadedMap_; // ファイル名 → soundIndex
};
} // namespace Hagine
