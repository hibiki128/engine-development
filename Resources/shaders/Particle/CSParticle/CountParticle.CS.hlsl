#include "../Particle.hlsli"

ConstantBuffer<ParticleCSSettings> gSettings : register(b0);
RWStructuredBuffer<uint> gAliveCount : register(u0);
RWStructuredBuffer<Particle> gParticles : register(u1);

groupshared uint groupAliveCount;

[numthreads(1024, 1, 1)]
void main(uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    
    // グループ内の最初のスレッドでグループ共有メモリを初期化
    if (GTid.x == 0)
    {
        groupAliveCount = 0;
        
        // さらに全体の最初のスレッドならグローバルカウンターも初期化
        if (DTid.x == 0)
        {
            gAliveCount[0] = 0;
        }
    }
    
    // グループ内の全スレッドが初期化を待つ
    GroupMemoryBarrierWithGroupSync();
    
    // 各スレッドがパーティクルをチェック
    if (particleIndex < gSettings.maxParticleCount)
    {
        if (gParticles[particleIndex].color.a > 0.0f)
        {
            InterlockedAdd(groupAliveCount, 1);
        }
    }
    
    // グループ内の全スレッドが完了するのを待つ
    GroupMemoryBarrierWithGroupSync();
    
    // グループ内の最初のスレッドがグローバルカウンターに加算
    if (GTid.x == 0 && groupAliveCount > 0)
    {
        InterlockedAdd(gAliveCount[0], groupAliveCount);
    }
}