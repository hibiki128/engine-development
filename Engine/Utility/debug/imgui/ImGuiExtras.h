#pragma once
// ============================================================
//  ImGuiExtras.h
//  重量級の ImGui 拡張ヘッダの取り込み口。
//
//  imspinner / implot3d はこちらのコードではないので、縮小変換の警告
//  （C4244 / C4267 など）が出る。Hagine は警告をエラー扱いにしているため、
//  素で include するとビルドが止まる。ヘッダ本体を書き換えると
//  ライブラリ更新のたびに消えるので、この取り込み口でだけ黙らせる。
//
//  どちらも数十〜百KB 超で includeコストが高い。共通ヘッダ
//  （DebugUIHelper.h）には入れず、実際に使う .cpp からだけ include すること。
// ============================================================
#ifdef USE_IMGUI

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // 'double' から 'float' への変換
#pragma warning(disable : 4267) // 'size_t' から 'int' への変換
#pragma warning(disable : 4305) // 引数の切り詰め
#endif

#include <implot3d.h>
#include <imspinner.h>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#endif // USE_IMGUI
