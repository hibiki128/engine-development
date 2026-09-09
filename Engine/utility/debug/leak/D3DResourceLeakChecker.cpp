#include "D3DResourceLeakChecker.h"
#include "debug/log/Logger.h"
#include "d3d12.h"
#include "dxgi1_6.h"
#include "dxgidebug.h"
#include "wrl.h"
#include <string>
#include <vector>

namespace Hagine {
namespace {

/// <summary>
/// ReportLiveObjects が積んだメッセージを DXGI のキューから取り出してログへ流す。
///
/// ReportLiveObjects は結果を OutputDebugString へ出すので、デバッガを繋いでいないと
/// どこにも残らない。同じ内容は DXGI のメッセージキューにも積まれるので、
/// そちらを読んで GameLog.txt へ書いておく。こうしておけば
/// 「実行して落として、ログを見れば何が残っているか分かる」状態になる。
/// </summary>
void DrainDebugMessagesToLog()
{
    Microsoft::WRL::ComPtr<IDXGIInfoQueue> infoQueue;
    if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&infoQueue))))
    {
        return;
    }

    const UINT64 messageCount = infoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
    if (messageCount == 0)
    {
        // 1件も積まれない＝生き残っている D3D12 オブジェクトが無い。
        // 何も書かずに帰ると「ログが壊れているのか綺麗なのか」が区別できないので、
        // 綺麗だったことを明示しておく
        Logger::Info("---- D3D12 残存オブジェクト: なし（リークなし）----");
        return;
    }

    Logger::Info("---- D3D12 残存オブジェクト（ReportLiveObjects の結果）ここから ----");

    std::vector<uint8_t> buffer;
    for (UINT64 index = 0; index < messageCount; ++index)
    {
        SIZE_T length = 0;
        if (FAILED(infoQueue->GetMessage(DXGI_DEBUG_ALL, index, nullptr, &length)) || length == 0)
        {
            continue;
        }

        buffer.resize(length);
        DXGI_INFO_QUEUE_MESSAGE *message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE *>(buffer.data());
        if (FAILED(infoQueue->GetMessage(DXGI_DEBUG_ALL, index, message, &length)))
        {
            continue;
        }
        if (message->pDescription && message->DescriptionByteLength > 0)
        {
            Logger::Info(std::string(message->pDescription, message->DescriptionByteLength - 1));
        }
    }

    Logger::Info("---- D3D12 残存オブジェクト ここまで ----");
    infoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
}

} // namespace

D3DResourceLeakChecker::~D3DResourceLeakChecker()
{
    // リソースリークチェック
    Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
    {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
        debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);

        // デバッガ無しでも中身が追えるよう、同じ内容をログへ残す
        DrainDebugMessagesToLog();
    }
}
} // namespace Hagine
