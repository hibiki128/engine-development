#include "../Particle.hlsli"

ConstantBuffer<ParticleCSSettings> gSettings : register(b0);
// SoA: 生存判定用 Life バッファのみ初期化すればよい。
//   未発生スロットは Update が lifeTime<=0 で早期 return するため、
//   DrawCore/SimCore/Trail/Rotation/Override は Emit が発生時に全項目を書き込む。
RWStructuredBuffer<float> gLife : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);
RWStructuredBuffer<int> gFreeListTailIndex : register(u3);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    int particleIndex = DTid.x;
    if (particleIndex < gSettings.maxParticleCount)
    {
        // lifeTime<=0 = 未使用スロットのマーカー
        gLife[particleIndex] = 0.0f;

        gFreeList[particleIndex] = particleIndex;

        if (particleIndex == 0)
        {
            gFreeListIndex[0] = 0;
            gFreeListTailIndex[0] = gSettings.maxParticleCount;
        }
    }
}
