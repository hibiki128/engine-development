#include "MetaBall.hlsli"

// ============================================================
// 出力頂点バッファの初期化
//   Marching Cubes は「今フレームに出た三角形」だけを先頭から詰めるので、
//   前フレームの残りが後ろに居ると古い形が描かれてしまう。
//   ここで全頂点を原点へ倒し、書かれなかったぶんを面積0の三角形にしておく
//   （面積0はラスタライザが捨てるので、1ピクセルも塗られない）。
//
//   ついでに出力カウンタも 0 に戻す。
// ============================================================

RWStructuredBuffer<uint> gVertices : register(u0);
RWStructuredBuffer<uint> gCounter : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint vertexIndex = id.x;
    if (vertexIndex == 0)
    {
        gCounter[0] = 0;
    }
    if (vertexIndex >= gMaxVertexCount)
    {
        return;
    }

    // 位置だけ潰せば三角形は面積0になる。UV・法線は書き換えなくてよい
    const uint base = vertexIndex * METABALL_VERTEX_WORDS;
    gVertices[base + 0] = asuint(0.0f);
    gVertices[base + 1] = asuint(0.0f);
    gVertices[base + 2] = asuint(0.0f);
    gVertices[base + 3] = asuint(1.0f);
}
