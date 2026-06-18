#pragma once
#include <random>

namespace Hagine {

/// <summary>
/// 乱数生成ユーティリティ
/// メルセンヌ・ツイスタを用いて範囲指定の乱数を返す
/// </summary>
class Random {
public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// int型のランダムな値を返す
    /// </summary>
    /// <param name="min">最小値</param>
    /// <param name="max">最大値</param>
    /// <returns>int: min〜maxの範囲の乱数</returns>
    static int Range(int min, int max);

    /// <summary>
    /// float型のランダムな値を返す
    /// </summary>
    /// <param name="min">最小値</param>
    /// <param name="max">最大値</param>
    /// <returns>float: min〜maxの範囲の乱数</returns>
    static float Range(float min, float max);

private:
    /// ===================================================
    /// private method
    /// ===================================================

    /// <summary>
    /// 乱数生成器（メルセンヌ・ツイスタ）を取得
    /// </summary>
    /// <returns>std::mt19937&: 乱数生成エンジン</returns>
    static std::mt19937& GetEngine();
};
} // namespace Hagine
