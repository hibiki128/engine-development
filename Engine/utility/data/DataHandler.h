#pragma once
#include "iostream"
#include "type/Vector4.h"
#include <cstdint>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <type/Matrix4x4.h>
#include <type/Quaternion.h>
#include <type/Vector2.h>
#include <type/Vector3.h>

namespace Hagine {
using json = nlohmann::json;
namespace fs = std::filesystem;

/// <summary>
/// JSONファイルへのデータの保存・読み込みを管理するクラス
/// メモリ上にキャッシュし、変更があればデストラクタやFlushでファイルへ書き出す
/// </summary>
class DataHandler
{
  private:
    /// ===================================================
    /// private variables
    /// ===================================================

    std::string basePath_;               // 固定の基準パス（jsons ルート）
    std::string folderPath_ = "";        // インスタンスごとのフォルダパス
    std::string fileName_ = "data.json"; // インスタンスごとのファイル名
    json cachedJson_;                    // メモリ上にキャッシュしたJSONデータ
    bool isDirty_ = false;               // Saveによる変更がある場合にファイル書き出しが必要かを示すフラグ

    /// <summary>
    /// ファイルからJSONを読み込んでキャッシュに格納する
    /// </summary>
    void LoadFromFile();

  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// キャッシュ内容をファイルに書き出す
    /// </summary>
    /// <returns>bool: 書き出しに成功（または変更なし）なら true</returns>
    bool Flush();
    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="folder">フォルダパス</param>
    /// <param name="file">ファイル名</param>
    DataHandler(const std::string &folder, const std::string &file);

    /// <summary>
    /// デストラクタ（未書き出しの変更をファイルに反映）
    /// </summary>
    ~DataHandler();

    /// <summary>
    /// キャッシュに書き込み、ダーティフラグを立てる
    /// </summary>
    /// <param name="key">キー</param>
    /// <param name="value">保存する値</param>
    template <typename T>
    void Save(const std::string &key, const T &value);

    /// <summary>
    /// キャッシュから直接読み込む
    /// </summary>
    /// <param name="key">キー</param>
    /// <param name="defaultValue">キーが存在しない場合の既定値</param>
    /// <returns>T: 読み込んだ値（なければ既定値）</returns>
    template <typename T>
    T Load(const std::string &key, const T &defaultValue);

    /// <summary>
    /// 指定キーをキャッシュから削除する（次回Flushでファイルへ反映）
    /// </summary>
    /// <param name="key">削除するキー</param>
    void Remove(const std::string &key);

    /// <summary>
    /// 指定した接頭辞で始まるキーをまとめて削除する（次回Flushでファイルへ反映）
    /// </summary>
    /// <param name="prefix">削除するキーの接頭辞</param>
    void RemoveByPrefix(const std::string &prefix);

    /// <summary>
    /// 指定キーが存在するか
    /// </summary>
    bool Contains(const std::string &key) const;

    /// <summary>
    /// JSONファイルの存在確認
    /// </summary>
    /// <returns>bool: 存在すれば true</returns>
    bool Exists() const;

    /// <summary>
    /// 指定したJSONファイルを削除
    /// </summary>
    /// <param name="jsonName">削除するJSONファイル名</param>
    void DeleteJson(const std::string &jsonName);

    /// <summary>
    /// 対象フォルダ内のすべてのJSONファイルを削除
    /// </summary>
    void DeleteAllJsonsInFolder();
};

/// ===================================================
/// 各型のJSON変換定義（nlohmann::json 用 to_json / from_json）
/// ===================================================

// Vector2
inline void to_json(json &j, const Vector2 &v)
{
    j = json{{"x", v.x}, {"y", v.y}};
}

inline void from_json(const json &j, Vector2 &v)
{
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
}

// JSON変換の定義 (Vector3)
inline void to_json(json &j, const Vector3 &v)
{
    j = json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

inline void from_json(const json &j, Vector3 &v)
{
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
    v.z = j.at("z").get<float>();
}

// JSON変換の定義 (Vector4)
inline void to_json(json &j, const Vector4 &v)
{
    j = json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
}

inline void from_json(const json &j, Vector4 &v)
{
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
    v.z = j.at("z").get<float>();
    v.w = j.at("w").get<float>();
}

// JSON変換の定義 (Quaternion)
inline void to_json(json &j, const Quaternion &q)
{
    j = json{{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}};
}

inline void from_json(const json &j, Quaternion &q)
{
    q.x = j.at("x").get<float>();
    q.y = j.at("y").get<float>();
    q.z = j.at("z").get<float>();
    q.w = j.at("w").get<float>();
}

// JSON変換の定義 (Matrix4x4)
inline void to_json(json &j, const Matrix4x4 &matrix)
{
    j = json::array();
    for (int i = 0; i < 4; ++i)
    {
        json row = json::array();
        for (int k = 0; k < 4; ++k)
        {
            row.push_back(matrix.m[i][k]);
        }
        j.push_back(row);
    }
}

inline void from_json(const json &j, Matrix4x4 &matrix)
{
    for (int i = 0; i < 4; ++i)
    {
        for (int k = 0; k < 4; ++k)
        {
            matrix.m[i][k] = j[i][k].get<float>();
        }
    }
}

// キャッシュに書き込み、ダーティフラグを立てる
template <typename T>
void DataHandler::Save(const std::string &key, const T &value)
{
    cachedJson_[key] = value;
    isDirty_ = true;
}

// キャッシュから直接読み込む（ファイルアクセスなし）
template <typename T>
T DataHandler::Load(const std::string &key, const T &defaultValue)
{
    if (cachedJson_.contains(key))
    {
        try
        {
            return cachedJson_[key].get<T>();
        }
        catch (const json::exception &e)
        {
            std::cerr << "JSON Load Error: " << e.what() << " (Key: " << key << ")" << std::endl;
        }
    }
    return defaultValue;
}

// 実体化は DataHandler.cpp
extern template void DataHandler::Save<int>(const std::string &, const int &);
extern template void DataHandler::Save<uint32_t>(const std::string &, const uint32_t &);
extern template void DataHandler::Save<float>(const std::string &, const float &);
extern template void DataHandler::Save<std::string>(const std::string &, const std::string &);
extern template void DataHandler::Save<Vector2>(const std::string &, const Vector2 &);
extern template void DataHandler::Save<Vector3>(const std::string &, const Vector3 &);
extern template void DataHandler::Save<Vector4>(const std::string &, const Vector4 &);
extern template void DataHandler::Save<Quaternion>(const std::string &, const Quaternion &);
extern template void DataHandler::Save<Matrix4x4>(const std::string &, const Matrix4x4 &);

extern template int DataHandler::Load<int>(const std::string &, const int &);
extern template uint32_t DataHandler::Load<uint32_t>(const std::string &, const uint32_t &);
extern template float DataHandler::Load<float>(const std::string &, const float &);
extern template std::string DataHandler::Load<std::string>(const std::string &, const std::string &);
extern template Vector2 DataHandler::Load<Vector2>(const std::string &, const Vector2 &);
extern template Vector3 DataHandler::Load<Vector3>(const std::string &, const Vector3 &);
extern template Vector4 DataHandler::Load<Vector4>(const std::string &, const Vector4 &);
extern template Quaternion DataHandler::Load<Quaternion>(const std::string &, const Quaternion &);
extern template Matrix4x4 DataHandler::Load<Matrix4x4>(const std::string &, const Matrix4x4 &);
} // namespace Hagine
