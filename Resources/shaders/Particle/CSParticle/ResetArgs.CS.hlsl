// =============================================
// ResetArgs.CS
//   生存コンパクション用カウンタを毎フレーム先頭で 0 にリセットする。
//   UpdateParticle.CS の InterlockedAdd(gAliveCounter[0], ...) の前に
//   1スレッドだけ走らせる軽量パス。
//   (Phase 2 でインダイレクト dispatch/draw 引数の初期化もここに拡張予定)
// =============================================
RWStructuredBuffer<uint> gAliveCounter : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    gAliveCounter[0] = 0;
}
