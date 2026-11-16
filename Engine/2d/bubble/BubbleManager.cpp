#include "BubbleManager.h"
#include "../SpriteManager.h"
#include "DirectXCommon.h"
#include "WinApp.h"
#include <MyMath.h>
#include <camera/projection/ViewProjection.h>
#include <graphics/pipeline/PipelineManager.h>
#include <graphics/texture/TextureManager.h>
#include <asset/AssetPath.h>
#include <object/base/BaseObject.h>
#include <object/base/BaseObjectManager.h>
#include <algorithm>
#include <cstring>

namespace Hagine {
namespace {

constexpr const char *kWhiteTexture = "debug/white1x1.png"; // 本体の画像が無いときの白
constexpr size_t kMinVertexCapacity = 1024;               // 頂点バッファの最小容量

} // namespace

BubbleManager *BubbleManager::GetInstance()
{
    static BubbleManager instance;
    return &instance;
}

BubbleLibrary &BubbleManager::GetLibrary()
{
    if (!loaded_)
    {
        library_.Load();
        loaded_ = true;
    }
    return library_;
}

// ============================================================
//  表示
// ============================================================

BubbleId BubbleManager::Show(const std::string &instanceName)
{
    const BubbleInstance *instance = GetLibrary().FindInstance(instanceName);
    return instance ? Show(*instance) : 0;
}

BubbleId BubbleManager::Show(const std::string &instanceName, const std::string &text)
{
    const BubbleInstance *instance = GetLibrary().FindInstance(instanceName);
    if (!instance)
    {
        return 0;
    }
    BubbleInstance copy = *instance;
    copy.text = text;
    return Show(copy);
}

BubbleId BubbleManager::Show(const BubbleInstance &instance)
{
    BubbleLibrary &library = GetLibrary();
    const BubbleStyle *style = library.FindStyle(instance.style);

    ActiveBubble bubble;
    bubble.id = nextId_++;
    if (nextId_ == 0)
    {
        nextId_ = 1;
    }
    bubble.instance = instance;
    bubble.style = style ? *style : (library.styles.empty() ? BubbleStyle{} : library.styles.front());

    // 文字の配置と画像
    bubble.text = BubbleText::Build(instance.text, bubble.style, BubbleText::UsesGarble(instance));
    bubble.textSlot = UploadText(bubble.text, bubble.textHandle);
    bubble.text.pixels.clear();
    bubble.text.pixels.shrink_to_fit();

    // 文字送りの時刻
    BuildRevealTimes(instance, bubble.text, bubble.revealTimes, bubble.typingEnd);
    bubble.charProgress.assign(bubble.revealTimes.size(), 0.0f);

    // 本体としっぽの画像
    TextureManager *pTextureManager = TextureManager::GetInstance();
    pTextureManager->LoadTexture(bubble.style.bodyTexture.empty() ? kWhiteTexture : bubble.style.bodyTexture);
    if (!bubble.style.tailTexture.empty())
    {
        pTextureManager->LoadTexture(bubble.style.tailTexture);
    }

    const BubbleId id = bubble.id;
    bubbles_.push_back(std::move(bubble));
    return id;
}

void BubbleManager::Advance(BubbleId id)
{
    ActiveBubble *bubble = Find(id);
    if (!bubble || bubble->closing)
    {
        return;
    }
    if (bubble->typingTime < bubble->typingEnd)
    {
        bubble->time = (std::max)(bubble->time, bubble->instance.appearDuration);
        bubble->typingTime = bubble->typingEnd;
        UpdateCharProgress(bubble->instance, bubble->revealTimes, bubble->typingTime, bubble->charProgress);
        return;
    }
    bubble->closing = true;
}

void BubbleManager::Close(BubbleId id)
{
    if (ActiveBubble *bubble = Find(id))
    {
        bubble->closing = true;
    }
}

void BubbleManager::Clear()
{
    bubbles_.clear();
    std::fill(textSlots_.begin(), textSlots_.end(), false);
}

bool BubbleManager::IsShowing(BubbleId id) const
{
    return Find(id) != nullptr;
}

bool BubbleManager::IsTyping(BubbleId id) const
{
    const ActiveBubble *bubble = Find(id);
    return bubble && !bubble->closing && bubble->typingTime < bubble->typingEnd;
}

BubbleManager::ActiveBubble *BubbleManager::Find(BubbleId id)
{
    return const_cast<ActiveBubble *>(static_cast<const BubbleManager *>(this)->Find(id));
}

const BubbleManager::ActiveBubble *BubbleManager::Find(BubbleId id) const
{
    for (const ActiveBubble &bubble : bubbles_)
    {
        if (bubble.id == id)
        {
            return &bubble;
        }
    }
    return nullptr;
}

// ============================================================
//  更新
// ============================================================

void BubbleManager::Update(float deltaTime)
{
    // 頂点バッファを次のフレーム用へ切り替え
    frameSlot_ = (frameSlot_ + 1) % kFrameSlots;
    vertexUsed_ = 0;

    for (ActiveBubble &bubble : bubbles_)
    {
        bubble.time += deltaTime;
        if (bubble.closing)
        {
            bubble.closeTime += deltaTime;
            continue;
        }

        // 出きってから文字送り
        const bool appeared = bubble.instance.appear == BubbleAppear::None || bubble.time >= bubble.instance.appearDuration;
        if (appeared && bubble.typingTime < bubble.typingEnd)
        {
            bubble.typingTime = (std::min)(bubble.typingTime + deltaTime, bubble.typingEnd);
        }
        UpdateCharProgress(bubble.instance, bubble.revealTimes, bubble.typingTime, bubble.charProgress);

        // 全文が出てから消えるまで
        if (bubble.typingTime >= bubble.typingEnd)
        {
            bubble.idleTime += deltaTime;
            if (bubble.instance.lifeTime >= 0.0f && bubble.idleTime >= bubble.instance.lifeTime)
            {
                bubble.closing = true;
            }
        }
    }

    // 閉じ終えた吹き出しを削除
    for (auto it = bubbles_.begin(); it != bubbles_.end();)
    {
        if (it->closing && it->closeTime >= it->instance.disappearDuration)
        {
            if (it->textSlot >= 0 && it->textSlot < static_cast<int>(textSlots_.size()))
            {
                textSlots_[static_cast<size_t>(it->textSlot)] = false;
            }
            it = bubbles_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void BubbleManager::BuildRevealTimes(const BubbleInstance &instance, const BubbleTextLayout &text, std::vector<float> &revealTimes, float &typingEnd)
{
    revealTimes.resize(text.chars.size());
    float time = 0.0f;
    for (size_t i = 0; i < text.chars.size(); ++i)
    {
        revealTimes[i] = time;
        if (instance.charInterval > 0.0f)
        {
            time += instance.charInterval;
            if (BubbleText::IsPause(text.chars[i]))
            {
                time += (std::max)(instance.punctuationPause, 0.0f);
            }
        }
    }
    const float last = revealTimes.empty() ? 0.0f : revealTimes.back();
    const float duration = instance.charAppear == BubbleCharAppear::Instant ? 0.0f : (std::max)(instance.charDuration, 0.0f);
    typingEnd = last + duration;
}

void BubbleManager::UpdateCharProgress(const BubbleInstance &instance, const std::vector<float> &revealTimes, float typingTime, std::vector<float> &charProgress)
{
    charProgress.resize(revealTimes.size());
    const bool instant = instance.charAppear == BubbleCharAppear::Instant || instance.charDuration <= 0.0f;
    for (size_t i = 0; i < revealTimes.size(); ++i)
    {
        const float elapsed = typingTime - revealTimes[i];
        if (elapsed < 0.0f)
        {
            charProgress[i] = 0.0f;
        }
        else if (instant)
        {
            charProgress[i] = 1.0f;
        }
        else
        {
            charProgress[i] = (std::max)((std::min)(elapsed / instance.charDuration, 1.0f), 1e-4f);
        }
    }
}

float BubbleManager::AppearProgress(const ActiveBubble &bubble)
{
    const BubbleInstance &instance = bubble.instance;
    if (bubble.closing)
    {
        if (instance.disappearDuration <= 0.0f)
        {
            return 0.0f;
        }
        return std::clamp(1.0f - bubble.closeTime / instance.disappearDuration, 0.0f, 1.0f);
    }
    if (instance.appear == BubbleAppear::None || instance.appearDuration <= 0.0f)
    {
        return 1.0f;
    }
    const float t = std::clamp(bubble.time / instance.appearDuration, 0.0f, 1.0f);
    return ApplyEasing(instance.appearEasing, 0.0f, 1.0f, t, 1.0f);
}

// ============================================================
//  描画
// ============================================================

bool BubbleManager::ResolveAnchor(const BubbleInstance &instance, const ViewProjection &viewProjection, Vector2 &anchor)
{
    switch (instance.targetKind)
    {
    case BubbleTargetKind::Object:
    {
        BaseObject *object = BaseObjectManager::GetInstance()->GetObjectByName(instance.target);
        if (!object)
        {
            return false;
        }
        const Vector3 world = object->GetWorldPosition() + instance.worldOffset;
        const Vector4 clip = TransformationRaw({world.x, world.y, world.z, 1.0f}, viewProjection.matView_ * viewProjection.matProjection_);
        if (clip.w <= 1e-4f)
        {
            return false;
        }
        const float width = static_cast<float>(WinApp::GetVirtualWidth());
        const float height = static_cast<float>(WinApp::GetVirtualHeight());
        anchor = {(clip.x / clip.w * 0.5f + 0.5f) * width, (0.5f - clip.y / clip.w * 0.5f) * height};
        break;
    }
    case BubbleTargetKind::Sprite:
    {
        SpriteData *sprite = SpriteManager::GetInstance()->GetSprite(instance.target);
        if (!sprite || !sprite->sprite)
        {
            return false;
        }
        anchor = sprite->sprite->GetPosition();
        break;
    }
    default:
        anchor = instance.screenPosition;
        break;
    }
    anchor = anchor + instance.offset;
    return true;
}

int BubbleManager::UploadText(const BubbleTextLayout &text, D3D12_GPU_DESCRIPTOR_HANDLE &handle)
{
    int slot = -1;
    for (size_t i = 0; i < textSlots_.size(); ++i)
    {
        if (!textSlots_[i])
        {
            slot = static_cast<int>(i);
            break;
        }
    }
    if (slot < 0)
    {
        slot = static_cast<int>(textSlots_.size());
        textSlots_.push_back(false);
    }
    textSlots_[static_cast<size_t>(slot)] = true;
    handle = TextureManager::GetInstance()->UpdateDynamicTexture(TextTextureKey(slot), text.pixels.data(), text.imageWidth, text.imageHeight);
    return slot;
}

std::string BubbleManager::TextTextureKey(int slot)
{
    return "__bubble_text_" + std::to_string(slot) + "__";
}

bool BubbleManager::ResolveTexture(const std::string &path, D3D12_GPU_DESCRIPTOR_HANDLE &handle, Vector2 &size)
{
    if (path.empty())
    {
        return false;
    }
    TextureManager *pTextureManager = TextureManager::GetInstance();
    pTextureManager->LoadTexture(path);
    handle = pTextureManager->GetSrvHandleGPU(AssetPath::Image(path));
    const DirectX::TexMetadata &metadata = pTextureManager->GetMetaData(path);
    size = {static_cast<float>(metadata.width), static_cast<float>(metadata.height)};
    return handle.ptr != 0;
}

void BubbleManager::AppendQuads(const std::vector<BubbleQuad> &quads, D3D12_GPU_DESCRIPTOR_HANDLE texture,
                                std::vector<Vertex> &vertices, std::vector<DrawCall> &calls)
{
    if (quads.empty() || texture.ptr == 0)
    {
        return;
    }
    if (calls.empty() || calls.back().texture.ptr != texture.ptr)
    {
        calls.push_back({texture, static_cast<uint32_t>(vertices.size()), 0});
    }
    static constexpr int kTriangleCorners[6] = {0, 1, 2, 0, 2, 3};
    for (const BubbleQuad &quad : quads)
    {
        for (int corner : kTriangleCorners)
        {
            vertices.push_back({quad.position[corner], quad.uv[corner], quad.color});
        }
    }
    calls.back().vertexCount = static_cast<uint32_t>(vertices.size()) - calls.back().firstVertex;
}

BubbleManager::Vertex *BubbleManager::ReserveVertices(size_t count, D3D12_GPU_VIRTUAL_ADDRESS &address)
{
    const uint32_t slot = frameSlot_;
    const size_t needed = vertexUsed_ + count;
    if (needed > vertexCapacity_[slot])
    {
        // 書き込み済みの分があれば作り直さずに省略
        if (vertexUsed_ > 0)
        {
            return nullptr;
        }
        const size_t capacity = (std::max)(needed * 2, kMinVertexCapacity);
        vertexBuffers_[slot] = DirectXCommon::GetInstance()->CreateBufferResource(sizeof(Vertex) * capacity);
        void *mapped = nullptr;
        vertexBuffers_[slot]->Map(0, nullptr, &mapped);
        mappedVertices_[slot] = static_cast<Vertex *>(mapped);
        vertexCapacity_[slot] = capacity;
    }
    Vertex *dst = mappedVertices_[slot] + vertexUsed_;
    address = vertexBuffers_[slot]->GetGPUVirtualAddress() + sizeof(Vertex) * vertexUsed_;
    vertexUsed_ = needed;
    return dst;
}

void BubbleManager::Draw(const ViewProjection &viewProjection)
{
    if (bubbles_.empty())
    {
        return;
    }
    const Vector2 screenSize = {static_cast<float>(WinApp::GetVirtualWidth()), static_cast<float>(WinApp::GetVirtualHeight())};

    // 四角形を頂点へ
    std::vector<Vertex> vertices;
    std::vector<DrawCall> calls;
    for (const ActiveBubble &bubble : bubbles_)
    {
        Vector2 anchor;
        if (!ResolveAnchor(bubble.instance, viewProjection, anchor))
        {
            continue;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE bodyHandle{};
        Vector2 bodySize;
        if (!ResolveTexture(bubble.style.bodyTexture.empty() ? kWhiteTexture : bubble.style.bodyTexture, bodyHandle, bodySize))
        {
            continue;
        }
        D3D12_GPU_DESCRIPTOR_HANDLE tailHandle{};
        Vector2 tailSize = {0.0f, 0.0f};
        if (!ResolveTexture(bubble.style.tailTexture, tailHandle, tailSize))
        {
            tailSize = {0.0f, 0.0f};
        }

        BubbleShapeParams params;
        params.anchor = anchor;
        params.pivot = bubble.instance.pivot;
        params.bodyTextureSize = bodySize;
        params.tailTextureSize = tailSize;
        params.showTail = bubble.instance.showTail;
        params.tailPosition = bubble.instance.tailPosition;
        params.tailFlip = bubble.instance.tailFlip;
        params.clampToScreen = true;
        params.screenSize = screenSize;
        params.appear = bubble.instance.appear;
        params.appearProgress = AppearProgress(bubble);
        params.charAppear = bubble.instance.charAppear;
        params.charProgress = &bubble.charProgress;
        params.motion = &bubble.instance.motion;
        params.time = bubble.time;
        const BubbleMesh mesh = BubbleGeometry::Build(bubble.style, bubble.text, params);

        AppendQuads(mesh.body, bodyHandle, vertices, calls);
        AppendQuads(mesh.tail, tailHandle, vertices, calls);
        AppendQuads(mesh.text, bubble.textHandle, vertices, calls);
    }
    if (vertices.empty())
    {
        return;
    }

    // GPU へ送る
    D3D12_GPU_VIRTUAL_ADDRESS address = 0;
    Vertex *dst = ReserveVertices(vertices.size(), address);
    if (!dst)
    {
        return;
    }
    std::memcpy(dst, vertices.data(), sizeof(Vertex) * vertices.size());
    if (!constantBuffer_)
    {
        constantBuffer_ = DirectXCommon::GetInstance()->CreateBufferResource(256);
        void *mapped = nullptr;
        constantBuffer_->Map(0, nullptr, &mapped);
        mappedConstants_ = static_cast<Vector2 *>(mapped);
    }
    mappedConstants_[0] = screenSize;

    // 描画
    PipelineManager *pPipelineManager = PipelineManager::GetInstance();
    pPipelineManager->DrawCommonSetting(PipelineType::Bubble, BlendMode::Normal);
    const ShaderRootSignature *rootSignature = pPipelineManager->GetCurrentRootSignature();
    if (!rootSignature)
    {
        return;
    }
    ID3D12GraphicsCommandList *pCommandList = DirectXCommon::GetInstance()->GetCommandList().Get();
    pCommandList->SetGraphicsRootConstantBufferView(rootSignature->GetCbvIndex(0), constantBuffer_->GetGPUVirtualAddress());
    D3D12_VERTEX_BUFFER_VIEW view{};
    view.BufferLocation = address;
    view.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * vertices.size());
    view.StrideInBytes = sizeof(Vertex);
    pCommandList->IASetVertexBuffers(0, 1, &view);
    const UINT textureIndex = rootSignature->GetSrvIndex(0);
    for (const DrawCall &call : calls)
    {
        pCommandList->SetGraphicsRootDescriptorTable(textureIndex, call.texture);
        pCommandList->DrawInstanced(call.vertexCount, 1, call.firstVertex, 0);
    }
}

} // namespace Hagine
