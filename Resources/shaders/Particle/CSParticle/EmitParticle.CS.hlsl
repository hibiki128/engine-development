#include "../../Random/Random.hlsli"
#include"../Particle.hlsli"

ConstantBuffer<EmitterMesh> gEmitterMesh : register(b0);
ConstantBuffer<PerFrame> gPerFrame : register(b1);
ConstantBuffer<ParticleCSSettings> gSettings : register(b2);
ConstantBuffer<FieldCountCB> gFieldCB : register(b3);
// SoA バッファ（u0-u5）
RWStructuredBuffer<float>     gLife     : register(u0);
RWStructuredBuffer<PDrawCore> gDrawCore : register(u1);
RWStructuredBuffer<PSimCore>  gSimCore  : register(u2);
RWStructuredBuffer<PTrail>    gTrail    : register(u3);
RWStructuredBuffer<PRotation> gRotation : register(u4);
RWStructuredBuffer<uint2>     gOverride : register(u5);
// フリーリスト（u6-u8）
RWStructuredBuffer<int>  gFreeListIndex     : register(u6);
RWStructuredBuffer<uint> gFreeList          : register(u7);
RWStructuredBuffer<int>  gFreeListTailIndex : register(u8);
// 生存リスト間接ディスパッチ（§8）: 新規粒子を out リストへ append する。
//   out リスト = [今フレームEmitした粒子] + [Update後も生存した粒子]。
//   描画用 renderCompact にも同じ idx で書き、今フレームから即描画する（1F遅延なし）。
RWStructuredBuffer<uint>      gAliveList     : register(u9);  // out: 生存slot indexリスト
RWStructuredBuffer<uint>      gAliveCounter  : register(u10); // out: リスト長(append位置のアトミックカウンタ)
RWStructuredBuffer<PDrawCore> gRenderCompact : register(u11); // out: 描画データ(詰めた順)
StructuredBuffer<TriangleInfo> gTriangles : register(t0);
StructuredBuffer<float> gTriangleCDF : register(t1);
StructuredBuffer<EdgeInfo> gEdges : register(t2);
StructuredBuffer<ParticleField> gFields : register(t3);

// -------------------------------------------------------
// クォータニオンから回転行列を生成
// -------------------------------------------------------
float3x3 CreateRotationMatrixFromQuaternion(float4 q)
{
    float x = -q.x, y = -q.y, z = -q.z, w = q.w;
    return float3x3(
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y),
        2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
        2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)
    );
}

// -------------------------------------------------------
// ローカル頂点をワールド座標に変換
// -------------------------------------------------------
float3 LocalToWorld(float3 localPos, float3x3 rotMatrix)
{
    return mul(rotMatrix, localPos * gEmitterMesh.scale) + gEmitterMesh.translate;
}

// -------------------------------------------------------
// 三角形上のランダム点（面積一様サンプリング）
// -------------------------------------------------------
float3 RandomPointOnTriangle(float3 v0, float3 v1, float3 v2, float u, float v)
{
    if (u + v > 1.0f)
    {
        u = 1.0f - u;
        v = 1.0f - v;
    }
    return v0 + u * (v1 - v0) + v * (v2 - v0);
}

// -------------------------------------------------------
// CDF二分探索で面積加重ランダム三角形インデックスを取得
// -------------------------------------------------------
uint SampleTriangleByCDF(float r)
{
    uint left = 0;
    uint right = gEmitterMesh.triangleCount - 1;
    while (left < right)
    {
        uint mid = (left + right) / 2;
        if (gTriangleCDF[mid] < r)
            left = mid + 1;
        else
            right = mid;
    }
    return left;
}

// -------------------------------------------------------
// 点がいずれかの enableEmitSpawn フィールド球の内側にあるか判定
// 戻り値: ヒットしたフィールドのインデックス（-1=範囲外）
// -------------------------------------------------------
int CheckFieldContact(float3 worldPos)
{
    for (uint i = 0; i < gFieldCB.fieldCount; i++)
    {
        if (gFields[i].enableEmitSpawn == 0)
            continue;

        bool groupMatch = (gFields[i].groupId == -1) ||
                          (gPerFrame.emitterFieldGroupId == -1) ||
                          (gFields[i].groupId == gPerFrame.emitterFieldGroupId);
        if (!groupMatch)
            continue;

        float3 diff = worldPos - gFields[i].position;
        if (dot(diff, diff) < gFields[i].radius * gFields[i].radius)
            return (int) i;
    }
    return -1;
}

// -------------------------------------------------------
// enableEmitSpawn フィールドが存在するか確認
// -------------------------------------------------------
bool HasEmitSpawnField()
{
    for (uint i = 0; i < gFieldCB.fieldCount; i++)
    {
        if (gFields[i].enableEmitSpawn == 0)
            continue;
        bool groupMatch = (gFields[i].groupId == -1) ||
                          (gPerFrame.emitterFieldGroupId == -1) ||
                          (gFields[i].groupId == gPerFrame.emitterFieldGroupId);
        if (groupMatch)
            return true;
    }
    return false;
}

// -------------------------------------------------------
// [フィールド接触Emitモード] メインロジック
//
// 戦略:
//   CDF面積加重で三角形をランダム選択し、その三角形上のランダム点が
//   フィールド球内かどうかを判定する。
//   フィールド外なら別の三角形を再試行する（最大 kMaxRetry 回）。
//
//   こうすることで:
//   ・エミッターの線や頂点への集中が起きない（面積加重で均一分布）
//   ・全てのEmit結果がフィールド接触領域に収まる
//   ・フィールドが小さくてもリトライ回数を増やすことで対応できる
//
//   フィールドがエミッター表面の X% を占める場合、
//   1回のリトライで失敗する確率 = (1-X%)
//   32回全て失敗する確率 = (1-X%)^32
//   例: X=5%  → 失敗率 0.95^32 ≈ 19%（81%のスレッドは成功）
//       X=10% → 失敗率 0.90^32 ≈  4%（96%のスレッドは成功）
//       X=1%  → 失敗率 0.99^32 ≈ 73%（27%のスレッドは成功）
//   ※ 失敗したスレッドはスロットを返却してEmitしないだけなので問題なし
//
// 戻り値: true=Emit位置決定, false=フィールドなし or リトライ上限超過
// -------------------------------------------------------
bool TryFieldContactEmit(inout RandomGenerator rng, float3x3 rotMatrix,
                          out float3 outPos, out int outFieldIndex)
{
    outPos = gEmitterMesh.translate;
    outFieldIndex = -1;

    if (!HasEmitSpawnField())
        return false;

    static const int kMaxRetry = 32;

    // エッジ発生モード(2)が選ばれ辺があれば辺を、そうでなければ三角形を優先。
    // 三角形が無ければ辺、それも無ければ点にフォールバックする。
    bool useEdges = (gEmitterMesh.edgeCount > 0) &&
                    (gEmitterMesh.emitFromSurface == 2 || gEmitterMesh.triangleCount == 0);

    if (!useEdges && gEmitterMesh.triangleCount > 0)
    {
        // ---- メッシュエミッター: CDF面積加重でリトライ ----
        [loop]
        for (int retry = 0; retry < kMaxRetry; retry++)
        {
            uint triIndex = SampleTriangleByCDF(rng.Generate1d());

            float3 v0 = LocalToWorld(gTriangles[triIndex].v0, rotMatrix);
            float3 v1 = LocalToWorld(gTriangles[triIndex].v1, rotMatrix);
            float3 v2 = LocalToWorld(gTriangles[triIndex].v2, rotMatrix);

            float u = rng.Generate1d();
            float v = rng.Generate1d();
            float3 candidate = RandomPointOnTriangle(v0, v1, v2, u, v);

            int fieldIdx = CheckFieldContact(candidate);
            if (fieldIdx >= 0)
            {
                outPos = candidate;
                outFieldIndex = fieldIdx;
                return true;
            }
        }
    }
    else if (gEmitterMesh.edgeCount > 0)
    {
        // ---- エッジエミッター: ランダムエッジでリトライ ----
        [loop]
        for (int retry = 0; retry < kMaxRetry; retry++)
        {
            uint edgeIndex = uint(rng.Generate1d() * float(gEmitterMesh.edgeCount)) % gEmitterMesh.edgeCount;
            float t = rng.Generate1d();
            float3 v0 = LocalToWorld(gEdges[edgeIndex].v0, rotMatrix);
            float3 v1 = LocalToWorld(gEdges[edgeIndex].v1, rotMatrix);
            float3 candidate = lerp(v0, v1, t);

            int fieldIdx = CheckFieldContact(candidate);
            if (fieldIdx >= 0)
            {
                outPos = candidate;
                outFieldIndex = fieldIdx;
                return true;
            }
        }
    }
    else
    {
        // ---- 点エミッター: 中心がフィールド内かチェック ----
        int fieldIdx = CheckFieldContact(gEmitterMesh.translate);
        if (fieldIdx >= 0)
        {
            outPos = gEmitterMesh.translate;
            outFieldIndex = fieldIdx;
            return true;
        }
    }

    return false; // リトライ上限超過
}

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (gEmitterMesh.emit == 0)
        return;

    // 発生数のゲート。フィールド接触Emitモードでは gEmitterMesh.emitCountOverride が発生数を決める。
    // （通常モードは override=0 なのでグループ設定 gSettings.emitCount を使う）
    uint effectiveEmitCount = (gEmitterMesh.emitCountOverride > 0u)
                                  ? gEmitterMesh.emitCountOverride
                                  : gSettings.emitCount;
    if (DTid.x >= effectiveEmitCount)
        return;

    // -------------------------------------------------------
    // フリーリストからスロットを確保
    // -------------------------------------------------------
    uint headOld;
    InterlockedAdd(gFreeListIndex[0], 1, headOld);
    uint tailVal = gFreeListTailIndex[0];

    if (headOld >= tailVal)
    {
        int dummy;
        InterlockedAdd(gFreeListIndex[0], -1, dummy);
        return;
    }

    uint freePos = headOld % gSettings.maxParticleCount;
    uint particleIndex = gFreeList[freePos];

    RandomGenerator generator;
    generator.InitSeed(
        uint3(DTid.x, gPerFrame.groupId, DTid.x * 7919),
        gPerFrame.time
    );

    float3x3 rotMatrix = CreateRotationMatrixFromQuaternion(gEmitterMesh.rotation);

    // -------------------------------------------------------
    // Emit位置の決定
    //
    // gFieldCB.fieldCount > 0  → フィールド接触Emitモード
    //   TryFieldContactEmit でフィールド球内の三角形上の点を探す。
    //   失敗した場合はスロットを返却してEmitしない。
    //
    // gFieldCB.fieldCount == 0 → 通常モード
    //   従来通りエミッター全体からランダムEmit。
    // -------------------------------------------------------
    float3 emitPosition;
    int hitFieldIndex = -1;

    if (gFieldCB.fieldCount > 0)
    {
        float3 fieldPos;
        int fieldIdx;
        bool success = TryFieldContactEmit(generator, rotMatrix, fieldPos, fieldIdx);

        if (!success)
        {
            // フィールド接触点を見つけられなかった → スロット返却
            int oldTail;
            InterlockedAdd(gFreeListTailIndex[0], 1, oldTail);
            int slot = oldTail % (int) gSettings.maxParticleCount;
            gFreeList[slot] = particleIndex;
            return;
        }

        emitPosition = fieldPos;
        hitFieldIndex = fieldIdx;
    }
    else
    {
        // ---- 通常モード ----
        if (gEmitterMesh.triangleCount > 0 || gEmitterMesh.edgeCount > 0)
        {
            float3 randomPoint;

            if (gEmitterMesh.emitFromSurface == 2 && gEmitterMesh.edgeCount > 0)
            {
                uint edgeIndex = uint(generator.Generate1d() * float(gEmitterMesh.edgeCount)) % gEmitterMesh.edgeCount;
                float t = generator.Generate1d();
                randomPoint = lerp(gEdges[edgeIndex].v0, gEdges[edgeIndex].v1, t);
                randomPoint = mul(rotMatrix, randomPoint * gEmitterMesh.scale);
            }
            else if (gEmitterMesh.emitFromSurface == 1 && gEmitterMesh.triangleCount > 0)
            {
                uint triIndex = SampleTriangleByCDF(generator.Generate1d());
                float3 v0 = gTriangles[triIndex].v0;
                float3 v1 = gTriangles[triIndex].v1;
                float3 v2 = gTriangles[triIndex].v2;
                float u = generator.Generate1d();
                float v = generator.Generate1d();
                randomPoint = RandomPointOnTriangle(v0, v1, v2, u, v);
                randomPoint = mul(rotMatrix, randomPoint * gEmitterMesh.scale);
            }
            else
            {
                float3 r01 = float3(generator.Generate1d(), generator.Generate1d(), generator.Generate1d());
                float3 offset = (gEmitterMesh.anchorPoint - 0.5f) * 4.0f;
                randomPoint = lerp(-1.0f + offset, 1.0f + offset, r01);
                randomPoint = mul(rotMatrix, randomPoint * gEmitterMesh.scale);
            }

            emitPosition = gEmitterMesh.translate + randomPoint;
        }
        else
        {
            // ---- プリミティブ発生形状 ----
            if (gSettings.emitShape == 1)
            {
                // Sphere Surface: 球面上に一様分布
                float3 rnd = float3(
                    generator.Generate1d() * 2.0f - 1.0f,
                    generator.Generate1d() * 2.0f - 1.0f,
                    generator.Generate1d() * 2.0f - 1.0f
                );
                float len = length(rnd);
                float3 dir = (len > 0.001f) ? rnd / len : float3(0.0f, 1.0f, 0.0f);
                float r = (gSettings.emitSphereRadius > 0.001f)
                              ? gSettings.emitSphereRadius
                              : max(max(gEmitterMesh.scale.x, gEmitterMesh.scale.y), gEmitterMesh.scale.z);
                emitPosition = gEmitterMesh.translate + mul(rotMatrix, dir * r);
            }
            else if (gSettings.emitShape == 2)
            {
                // Cone: Y軸上向きコーン面（半開角 emitConeAngle）
                float theta = generator.Generate1d() * 6.28318530f;
                float cosMax = cos(gSettings.emitConeAngle);
                float cosAngle = lerp(cosMax, 1.0f, generator.Generate1d());
                float sinAngle = sqrt(max(0.0f, 1.0f - cosAngle * cosAngle));
                float3 dir = float3(sinAngle * cos(theta), cosAngle, sinAngle * sin(theta));
                float r = (gSettings.emitSphereRadius > 0.001f)
                              ? gSettings.emitSphereRadius
                              : max(max(gEmitterMesh.scale.x, gEmitterMesh.scale.y), gEmitterMesh.scale.z);
                emitPosition = gEmitterMesh.translate + mul(rotMatrix, dir * r);
            }
            else
            {
                // Box: スケールを半辺として使用したランダムボックス内点
                float3 r01 = float3(generator.Generate1d(), generator.Generate1d(), generator.Generate1d());
                emitPosition = gEmitterMesh.translate + mul(rotMatrix, (r01 * 2.0f - 1.0f) * gEmitterMesh.scale);
            }
        }
    }

    // -------------------------------------------------------
    // パーティクル初期化（SoA: 各バッファへローカルに組んで1回ずつ書き出す）
    //   endScale は廃止（Update で gSettings.endScaleValue を直読み）
    // -------------------------------------------------------
    PDrawCore dc;
    PSimCore sc;
    PTrail tr;
    PRotation rot;

    float scaleValue = lerp(gSettings.scaleMin, gSettings.scaleMax, generator.Generate1d());
    float3 scale3 = float3(scaleValue, scaleValue, scaleValue);
    dc.scaleXY = PackScaleXY(scale3);
    dc.scaleZ = PackScaleZ(scale3);
    sc.initialScaleXY = PackScaleXY(scale3);
    sc.initialScaleZ_isTrail = PackScaleZTrail(scale3, 0u); // 通常パーティクル isTrail=0
    dc.translate = emitPosition;
    tr.lastTrailPosition = emitPosition;

    float4 emitColor;
    if (gSettings.enableColorGradient)
    {
        // グラデーション有効時は発生フレームから始点色(LUT[0])で描画する（次フレーム以降は Update が更新）
        emitColor = UnpackColorRGBA8(gSettings.colorLUT[0].x);
    }
    else if (gSettings.enableRandomColor)
    {
        emitColor.rgb = generator.Generate3d() * 0.5f + 0.5f;
        emitColor.a = 1.0f;
    }
    else
    {
        emitColor = gSettings.startColor;
    }
    dc.color = PackColorRGBA8(emitColor);

    float3 vel;
    if (gSettings.enableRadialVelocity)
    {
        float3 radialDir = normalize(emitPosition - gSettings.radialVelocityCenter);
        if (gSettings.radialVelocityRandomness > 0.0f)
        {
            float3 rndOfs = (float3(generator.Generate1d(), generator.Generate1d(), generator.Generate1d()) * 2.0f - 1.0f)
                            * gSettings.radialVelocityRandomness;
            radialDir = normalize(radialDir + rndOfs);
        }
        float speed = lerp(gSettings.velocityMin.x, gSettings.velocityMax.x, generator.Generate1d());
        vel = radialDir * speed * gSettings.radialVelocityStrength;
    }
    else
    {
        vel = float3(
            lerp(gSettings.velocityMin.x, gSettings.velocityMax.x, generator.Generate1d()),
            lerp(gSettings.velocityMin.y, gSettings.velocityMax.y, generator.Generate1d()),
            lerp(gSettings.velocityMin.z, gSettings.velocityMax.z, generator.Generate1d())
        );
    }
    dc.velocity = vel;

    float life = lerp(gSettings.lifeTimeMin, gSettings.lifeTimeMax, generator.Generate1d());

    // フィールドにヒットしていれば lifeTime をフィールド値で上書き
    if (hitFieldIndex >= 0 && gFields[hitFieldIndex].emitSpawnLifeTimeMax > 0.0f)
    {
        life = lerp(
            gFields[hitFieldIndex].emitSpawnLifeTimeMin,
            gFields[hitFieldIndex].emitSpawnLifeTimeMax,
            generator.Generate1d()
        );
    }

    sc.currentTime = 0.0f;
    // isTrailParticle(=0) は上の sc.initialScaleZ_isTrail に同梱済み
    tr.parentIndex = 0xFFFFFFFF;
    tr.trailSpawnDistance = gSettings.trailSpawnDistance;

    if (gSettings.enableRandomRotation)
    {
        rot.rotation = float3(
            lerp(gSettings.rotationMin.x, gSettings.rotationMax.x, generator.Generate1d()),
            lerp(gSettings.rotationMin.y, gSettings.rotationMax.y, generator.Generate1d()),
            lerp(gSettings.rotationMin.z, gSettings.rotationMax.z, generator.Generate1d())
        );
    }
    else
        rot.rotation = float3(0, 0, 0);

    if (gSettings.enableRandomAngularVelocity)
    {
        rot.angularVelocity = float3(
            lerp(gSettings.angularVelocityMin.x, gSettings.angularVelocityMax.x, generator.Generate1d()),
            lerp(gSettings.angularVelocityMin.y, gSettings.angularVelocityMax.y, generator.Generate1d()),
            lerp(gSettings.angularVelocityMin.z, gSettings.angularVelocityMax.z, generator.Generate1d())
        );
    }
    else
        rot.angularVelocity = float3(0, 0, 0);

    // SoA バッファへ書き出し（Life は最後に書いてスロットを「生存」にする）
    gDrawCore[particleIndex] = dc;
    gSimCore[particleIndex] = sc;
    gTrail[particleIndex] = tr;
    gRotation[particleIndex] = rot;
    gOverride[particleIndex] = uint2(0u, 0u);
    gLife[particleIndex] = life;

    // 生存リスト間接ディスパッチ（§8）: 新規粒子を out リストへ append する。
    //   これにより次フレームの Update がこの粒子を sim 対象にする。
    //   renderCompact にも同じ idx で書き、今フレームから即描画する（drawCount=out カウンタ）。
    uint emitDst;
    InterlockedAdd(gAliveCounter[0], 1, emitDst);
    gAliveList[emitDst] = particleIndex;
    gRenderCompact[emitDst] = dc;
}
