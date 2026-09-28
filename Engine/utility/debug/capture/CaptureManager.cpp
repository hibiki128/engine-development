#include "CaptureManager.h"
#include "DirectXCommon.h"
#include <debug/log/Logger.h>
#include <string/StringUtility.h>

#ifdef USE_IMGUI
#include "imgui.h"
#include <debug/imgui/DebugUIHelper.h>
#include <debug/imgui/ImGuiNotification.h>
#include <icon/IconsFontAwesome5.h>
#endif // USE_IMGUI

#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>

namespace Hagine {
namespace {

/// 「20260913_2214_03」のような、並べ替えたときに時系列になる名前を作る
std::string MakeTimeStamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local = {};
    localtime_s(&local, &raw);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &local);
    return buffer;
}

} // namespace

CaptureManager *CaptureManager::GetInstance()
{
    static CaptureManager instance;
    return &instance;
}

void CaptureManager::Initialize(DirectXCommon *dxCommon)
{
    pDxCommon_ = dxCommon;

    std::error_code ec;
    std::filesystem::create_directories(outputDirectory_, ec);

    threadRunning_.store(true);
    encoderThread_ = std::thread(&CaptureManager::EncoderThreadMain, this);
}

void CaptureManager::Finalize()
{
    recording_ = false;
    screenshotRequested_ = false;

    if (threadRunning_.load())
    {
        threadRunning_.store(false);
        queueSignal_.notify_all();
        if (encoderThread_.joinable())
            encoderThread_.join();
    }
    pDxCommon_ = nullptr;
}

void CaptureManager::RequestScreenshot()
{
    screenshotRequested_ = true;
}

void CaptureManager::StartSequence()
{
    if (recording_)
        return;
    recording_ = true;
    frameCounter_ = 0;
    sequenceIndex_ = 0;
    recordedCount_ = 0;
    droppedCount_ = 0;
}

void CaptureManager::StopSequence()
{
    recording_ = false;
}

int CaptureManager::GetPendingCount() const
{
    std::lock_guard<std::mutex> lock(queueMutex_);
    return static_cast<int>(pendingImages_.size());
}

std::string CaptureManager::MakeFilePath(const char *prefix) const
{
    const char *extension = useJpeg_ ? ".jpg" : ".png";
    return outputDirectory_ + "/" + prefix + extension;
}

void CaptureManager::UpdateAutoCapture()
{
    // 環境変数 HAGINE_AUTO_CAPTURE="20,25.5" で、起動からその秒数に1枚ずつ撮る。
    // HAGINE_AUTO_CAPTURE_SOURCE=window でエディタUI込み（既定はゲーム画面だけ）。
    // DirectInput は外からのキー入力を受けないので、画面の確認を自動でしたいときに使う
    if (!autoCaptureParsed_)
    {
        autoCaptureParsed_ = true;
        char *value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "HAGINE_AUTO_CAPTURE") == 0 && value)
        {
            std::string text = value;
            free(value);
            size_t start = 0;
            while (start < text.size())
            {
                const size_t comma = text.find(',', start);
                const std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                try
                {
                    autoCaptureTimes_.push_back(std::stof(item));
                }
                catch (...)
                {
                }
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
            std::sort(autoCaptureTimes_.begin(), autoCaptureTimes_.end());
        }
        value = nullptr;
        if (_dupenv_s(&value, &length, "HAGINE_AUTO_CAPTURE_SOURCE") == 0 && value)
        {
            if (std::string(value) == "window")
                source_ = static_cast<int>(CaptureSource::Window);
            free(value);
        }
    }
    if (autoCaptureTimes_.empty())
        return;
    // 最初のフレームからの実時間で数える（Debug はフレームレートが低いので、フレーム数だと遅れる）
    static const auto start = std::chrono::steady_clock::now();
    autoCaptureElapsed_ = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
    if (autoCaptureElapsed_ >= autoCaptureTimes_.front())
    {
        autoCaptureTimes_.pop_front();
        screenshotRequested_ = true;
    }
}

void CaptureManager::EndFrame()
{
    if (pDxCommon_ == nullptr)
        return;

    UpdateAutoCapture();

    if (screenshotRequested_)
    {
        screenshotRequested_ = false;
        // 時刻は秒までなので、1秒に何枚撮っても上書きしないよう通し番号を足す
        const std::string path =
            MakeFilePath(("shot_" + MakeTimeStamp() + "_" + std::to_string(screenshotCounter_++)).c_str());
        CaptureOnce(path);
#ifdef USE_IMGUI
        // 撮れたことが分からないと何度も押してしまうので、必ず知らせる
        ImGuiNotification::Post("スクリーンショットを保存しました: " + path);
#endif // USE_IMGUI
    }

    if (recording_)
    {
        // 毎フレーム撮ると書き出しが追いつかないので、間引けるようにしてある
        if (++frameCounter_ >= std::max(1, frameStride_))
        {
            frameCounter_ = 0;
            char name[64];
            snprintf(name, sizeof(name), "seq_%04d", sequenceIndex_++);
            CaptureOnce(MakeFilePath(name));
        }
    }
}

void CaptureManager::CaptureOnce(const std::string &path)
{
    ID3D12CommandQueue *queue = pDxCommon_->GetCommandQueue();
    if (queue == nullptr)
        return;

    // 溜まりすぎているときは撮らずに捨てる。
    // ここで待つとゲームが止まってしまうので、落としたことを数えて後で知らせる
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (static_cast<int>(pendingImages_.size()) >= maxPendingImages_)
        {
            ++droppedCount_;
            return;
        }
    }

    ID3D12Resource *source = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> backBuffer;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_GENERIC_READ;

    if (static_cast<CaptureSource>(source_) == CaptureSource::Window)
    {
        // Present 済みのバッファは1つ前のインデックス。
        // 現在のインデックスは既に次のフレーム用へ進んでいる
        IDXGISwapChain4 *swapChain = pDxCommon_->GetSwapChain();
        if (swapChain == nullptr)
            return;
        const UINT count = static_cast<UINT>(pDxCommon_->GetBackBufferCount());
        const UINT current = swapChain->GetCurrentBackBufferIndex();
        const UINT presented = (current + count - 1) % count;
        if (FAILED(swapChain->GetBuffer(presented, IID_PPV_ARGS(&backBuffer))))
            return;
        source = backBuffer.Get();
        state = D3D12_RESOURCE_STATE_PRESENT;
    }
    else
    {
        // オフスクリーンは描画後に読み取り状態で置かれている
        source = pDxCommon_->GetOffScreenResource();
        state = D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    if (source == nullptr)
        return;

    DirectX::ScratchImage captured;
    HRESULT hr = DirectX::CaptureTexture(queue, source, false, captured, state, state);
    if (FAILED(hr))
    {
        Logger::Error("画面の読み出しに失敗しました。");
        return;
    }

    PendingImage pending;
    pending.image = std::move(captured);
    pending.path = path;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pendingImages_.push_back(std::move(pending));
    }
    queueSignal_.notify_one();
    ++recordedCount_;
    lastSavedPath_ = path;
}

void CaptureManager::EncoderThreadMain()
{
    // WIC を使うのでこのスレッドでも COM を立ち上げる
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    while (true)
    {
        PendingImage pending;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueSignal_.wait(lock, [this] { return !pendingImages_.empty() || !threadRunning_.load(); });
            if (pendingImages_.empty())
            {
                if (!threadRunning_.load())
                    break;
                continue;
            }
            pending = std::move(pendingImages_.front());
            pendingImages_.pop_front();
        }

        const DirectX::Image *image = pending.image.GetImage(0, 0, 0);
        if (image == nullptr)
            continue;

        const GUID codec = DirectX::GetWICCodec(useJpeg_ ? DirectX::WIC_CODEC_JPEG : DirectX::WIC_CODEC_PNG);
        const std::wstring widePath = StringUtility::ConvertString(pending.path);

        // JPEG は品質を指定できるので、指定があればプロパティとして渡す
        HRESULT hr = DirectX::SaveToWICFile(
            *image, DirectX::WIC_FLAGS_NONE, codec, widePath.c_str(), nullptr,
            [this](IPropertyBag2 *props) {
                if (!useJpeg_)
                    return;
                PROPBAG2 option = {};
                option.pstrName = const_cast<wchar_t *>(L"ImageQuality");
                VARIANT value = {};
                value.vt = VT_R4;
                value.fltVal = static_cast<float>(jpegQualityPercent_) / 100.0f;
                props->Write(1, &option, &value);
            });

        if (FAILED(hr))
        {
            Logger::Error("画像の書き出しに失敗しました: " + pending.path);
        }
    }

    if (SUCCEEDED(comResult))
        CoUninitialize();
}

void CaptureManager::DrawImGui(bool *open)
{
#ifdef USE_IMGUI
    if (open && !*open)
        return;

    ImGui::SetNextWindowSize(ImVec2(380.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(ICON_FA_CAMERA " キャプチャ", open, ImGuiWindowFlags_NoFocusOnAppearing))
    {
        ImGui::End();
        return;
    }

    SectionHeader("[ 撮影 ]", DebugTheme::kAccentBlue);

    const char *kSourceNames[] = {"ゲーム画面のみ (UIなし)", "画面そのまま (UI込み)"};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##source", &source_, kSourceNames, IM_ARRAYSIZE(kSourceNames));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("提出用の素材なら「ゲーム画面のみ」。\nエディタの見た目を残したいときは「画面そのまま」");

    bool jpeg = useJpeg_;
    if (ToggleRow("JPEG で保存する", "##jpeg", &jpeg, DebugTheme::kAccentOrange))
        useJpeg_ = jpeg;
    if (useJpeg_)
    {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderInt("##quality", &jpegQualityPercent_, 40, 100, "画質 %d%%");
    }
    else
    {
        DimText("PNG は無劣化ですが、書き出しに時間がかかります");
    }

    ImGui::Spacing();
    if (PrimaryButton(ICON_FA_CAMERA " いま1枚撮る", ImVec2(-1.0f, 0.0f)))
        RequestScreenshot();

    ImGui::Spacing();
    SectionHeader("[ 連番で録画 ]", DebugTheme::kAccentRed);

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##stride", &frameStride_, 1, 6, "%d フレームに1枚");
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("60fps で動いているとき、2 なら 30fps、3 なら 20fps 相当で撮れます。\n"
                          "書き出しが追いつかない場合はここを増やしてください");
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt("##maxPending", &maxPendingImages_, 4, 64, "溜められる枚数 %d");

    if (recording_)
    {
        if (DangerButton(ICON_FA_STOP " 録画を止める", ImVec2(-1.0f, 0.0f)))
        {
            StopSequence();
            ImGuiNotification::Post("録画を止めました（" + std::to_string(recordedCount_) + " 枚）");
        }
    }
    else
    {
        if (ConfirmButton(ICON_FA_CIRCLE " 録画を始める", ImVec2(-1.0f, 0.0f)))
            StartSequence();
    }

    ImGui::Spacing();
    SectionHeader("[ 状態 ]", DebugTheme::kAccentGreen);
    ReadOnlyRow("保存先", "%s", outputDirectory_.c_str());
    ReadOnlyRow("撮影枚数", "%d", recordedCount_);
    ReadOnlyRow("書き出し待ち", "%d", GetPendingCount());
    if (droppedCount_ > 0)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, DebugTheme::kAccentRed);
        ImGui::Text("間に合わず捨てた枚数: %d", droppedCount_);
        ImGui::PopStyleColor();
        DimText("「何フレームに1枚」を増やすか、JPEG にすると減ります");
    }
    if (!lastSavedPath_.empty())
        ReadOnlyRow("直近の保存", "%s", lastSavedPath_.c_str());

    ImGui::Spacing();
    if (NeutralButton(ICON_FA_FOLDER_OPEN " 保存先フォルダを開く", ImVec2(-1.0f, 0.0f)))
    {
        const std::string full = std::filesystem::absolute(outputDirectory_).string();
        ShellExecuteA(nullptr, "open", full.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::End();
#else
    (void)open;
#endif // USE_IMGUI
}

} // namespace Hagine
