#pragma once

/// ===================================================
/// プリコンパイル済みヘッダ
///
/// 変更頻度が低く、多くの .cpp から読まれる重いヘッダだけを置く場所。
/// Hagine.vcxproj で強制インクルード(/FI)と /Yu を設定しているので、
/// 各 .cpp の先頭に #include "pch.h" と書く必要は無い。
///
/// ここにエンジン自身のヘッダは入れないこと。
/// 1つ直すたびに全ソースが再コンパイルになり、かえって遅くなる。
/// ===================================================

// Windows.h の min/max マクロは MyMath などと衝突するため必ず無効化する。
// 各 .cpp にある #define NOMINMAX と同一の定義なので、再定義警告は出ない
#ifndef NOMINMAX
#define NOMINMAX
#endif

// --- 標準ライブラリ ---
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// --- Windows / DirectX ---
#include <Windows.h>
#include <d3d12.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <wrl.h>

// --- 外部ライブラリ ---
#include <DirectXTex/DirectXTex.h>
#include <nlohmann/json.hpp>

#ifdef USE_IMGUI
#include <imgui.h>
#endif
