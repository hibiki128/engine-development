#include "PostEffectChain.h"
#include <algorithm>

// -------------------------------------------------------
//  追加・削除
// -------------------------------------------------------

namespace Hagine {
int PostEffectChain::AddEffect(ShaderMode mode,
                               const std::string &name,
                               DirectXCommon *dxCommon,
                               int slotIndex) {
    // スロット番号の決定
    if (slotIndex == -1) {
        slotIndex = FindFirstFreeSlot();
    }

    if (!IsValidIndex(slotIndex)) {
        // 空きなし or 無効なインデックス
        return -1;
    }

    if (slots_[slotIndex].occupied) {
        // 指定スロットが既に使用中
        return -1;
    }

    // スロットを占有してパラメータを生成
    auto &slot = slots_[slotIndex];
    slot.occupied = true;
    slot.enabled = true;
    slot.name = name.empty() ? ("Effect_" + std::to_string(slotIndex)) : name;
    slot.params = PostEffectParamsFactory::Create(mode, dxCommon);

    return slotIndex;
}

bool PostEffectChain::RemoveEffect(int slotIndex) {
    if (!IsValidIndex(slotIndex) || !slots_[slotIndex].occupied) {
        return false;
    }

    auto &slot = slots_[slotIndex];
    slot.occupied = false;
    slot.enabled = false;
    slot.name.clear();
    slot.params.reset(); // パラメータのGPUリソースを解放

    return true;
}

void PostEffectChain::Clear(DirectXCommon * /*dxCommon*/) {
    for (auto &slot : slots_) {
        slot.occupied = false;
        slot.enabled = false;
        slot.name.clear();
        slot.params.reset();
    }
}

// -------------------------------------------------------
//  スロット操作
// -------------------------------------------------------

bool PostEffectChain::SetEnabled(int slotIndex, bool enabled) {
    if (!IsValidIndex(slotIndex) || !slots_[slotIndex].occupied) {
        return false;
    }
    slots_[slotIndex].enabled = enabled;
    return true;
}

bool PostEffectChain::SetName(int slotIndex, const std::string &name) {
    if (!IsValidIndex(slotIndex) || !slots_[slotIndex].occupied) {
        return false;
    }
    slots_[slotIndex].name = name;
    return true;
}

bool PostEffectChain::SwapSlots(int slotA, int slotB) {
    if (!IsValidIndex(slotA) || !IsValidIndex(slotB)) {
        return false;
    }
    std::swap(slots_[slotA], slots_[slotB]);
    return true;
}

bool PostEffectChain::MoveUp(int slotIndex) {
    if (slotIndex <= 0 || !IsValidIndex(slotIndex)) {
        return false;
    }
    return SwapSlots(slotIndex, slotIndex - 1);
}

bool PostEffectChain::MoveDown(int slotIndex) {
    if (slotIndex >= kMaxSlots - 1 || !IsValidIndex(slotIndex)) {
        return false;
    }
    return SwapSlots(slotIndex, slotIndex + 1);
}

// -------------------------------------------------------
//  参照・クエリ
// -------------------------------------------------------

bool PostEffectChain::IsOccupied(int slotIndex) const {
    return IsValidIndex(slotIndex) && slots_[slotIndex].occupied;
}

IPostEffectParams *PostEffectChain::GetParams(int slotIndex) {
    if (!IsValidIndex(slotIndex) || !slots_[slotIndex].occupied) {
        return nullptr;
    }
    return slots_[slotIndex].params.get();
}

std::vector<int> PostEffectChain::GetEnabledSlotIndices() const {
    std::vector<int> result;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (slots_[i].occupied && slots_[i].enabled) {
            result.push_back(i);
        }
    }
    return result;
}

bool PostEffectChain::HasEnabledEffects() const {
    for (const auto &slot : slots_) {
        if (slot.occupied && slot.enabled) {
            return true;
        }
    }
    return false;
}

bool PostEffectChain::IsEmpty() const {
    for (const auto &slot : slots_) {
        if (slot.occupied) {
            return false;
        }
    }
    return true;
}

int PostEffectChain::GetFreeSlotCount() const {
    int count = 0;
    for (const auto &slot : slots_) {
        if (!slot.occupied) {
            ++count;
        }
    }
    return count;
}

int PostEffectChain::RemoveEffectByName(const std::string &name) {
    const int slot = FindSlotByName(name);
    if (slot == -1) {
        return -1;
    }
    RemoveEffect(slot);
    return slot;
}

int PostEffectChain::RemoveAllEffectsByName(const std::string &name) {
    int count = 0;
    for (int i = 0; i < kMaxSlots; ++i) {
        if (slots_[i].occupied && slots_[i].name == name) {
            RemoveEffect(i);
            ++count;
        }
    }
    return count;
}

int PostEffectChain::FindSlotByName(const std::string &name) const {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (slots_[i].occupied && slots_[i].name == name) {
            return i;
        }
    }
    return -1;
}

bool PostEffectChain::SetEnabledByName(const std::string &name, bool enabled) {
    const int slot = FindSlotByName(name);
    if (slot == -1) {
        return false;
    }
    return SetEnabled(slot, enabled);
}

int PostEffectChain::FindFirstFreeSlot() const {
    for (int i = 0; i < kMaxSlots; ++i) {
        if (!slots_[i].occupied) {
            return i;
        }
    }
    return -1;
}
} // namespace Hagine
