#pragma once
#include "iostream"
#include "type/Vector4.h"
#include <Primitive/PrimitiveModel.h>
#include <cstdint>
#include <../nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <type/Vector2.h>
#include <type/Vector3.h>

namespace Hagine {
using json = nlohmann::json;
namespace fs = std::filesystem;

/// <summary>
/// JSONファイルへのデータの保存・読み込みを管理するクラス
/// メモリ上にキャッシュし、変更があればデストラクタやFlushでファイルへ書き出す
/// </summary>
class DataHandler {
  private:
    /// ===================================================
    /// private variants
    /// ===================================================

    std::string basePath_ = "resources/jsons"; // 固定の基準パス
    std::string folderPath_ = "";              // インスタンスごとのフォルダパス
    std::string fileName_ = "data.json";       // インスタンスごとのファイル名
    json cachedJson_;                          // メモリ上にキャッシュしたJSONデータ
    bool isDirty_ = false;                     // Saveによる変更がある場合にファイル書き出しが必要かを示すフラグ

    /// <summary>
    /// ファイルからJSONを読み込んでキャッシュに格納する
    /// </summary>
    void LoadFromFile() {
        std::string filePath = folderPath_ + "/" + fileName_;
        std::ifstream inFile(filePath);
        if (inFile.is_open()) {
            inFile >> cachedJson_;
            inFile.close();
        }
    }


  public:
    /// ===================================================
    /// public method
    /// ===================================================

    /// <summary>
    /// キャッシュ内容をファイルに書き出す
    /// </summary>
    /// <returns>bool: 書き出しに成功（または変更なし）なら true</returns>
    bool Flush() {
        if (!isDirty_)
            return true;
        std::string filePath = folderPath_ + "/" + fileName_;
        std::ofstream outFile(filePath);
        if (outFile.is_open()) {
            outFile << cachedJson_.dump(4);
            outFile.close();
            isDirty_ = false;
            return true;
        }
        return false;
    }
    /// <summary>
    /// コンストラクタ
    /// </summary>
    /// <param name="folder">フォルダパス</param>
    /// <param name="file">ファイル名</param>
    DataHandler(const std::string &folder, const std::string &file);

    /// <summary>
    /// デストラクタ（未書き出しの変更をファイルに反映）
    /// </summary>
    ~DataHandler() { Flush(); }

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
inline void to_json(json &j, const Vector2 &v) {
    j = json{{"x", v.x}, {"y", v.y}};
}

inline void from_json(const json &j, Vector2 &v) {
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
}

// JSON変換の定義 (Vector3)
inline void to_json(json &j, const Vector3 &v) {
    j = json{{"x", v.x}, {"y", v.y}, {"z", v.z}};
}

inline void from_json(const json &j, Vector3 &v) {
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
    v.z = j.at("z").get<float>();
}

// JSON変換の定義 (Vector4)
inline void to_json(json &j, const Vector4 &v) {
    j = json{{"x", v.x}, {"y", v.y}, {"z", v.z}, {"w", v.w}};
}

inline void from_json(const json &j, Vector4 &v) {
    v.x = j.at("x").get<float>();
    v.y = j.at("y").get<float>();
    v.z = j.at("z").get<float>();
    v.w = j.at("w").get<float>();
}

// JSON変換の定義 (Quaternion)
inline void to_json(json &j, const Quaternion &q) {
    j = json{{"x", q.x}, {"y", q.y}, {"z", q.z}, {"w", q.w}};
}

inline void from_json(const json &j, Quaternion &q) {
    q.x = j.at("x").get<float>();
    q.y = j.at("y").get<float>();
    q.z = j.at("z").get<float>();
    q.w = j.at("w").get<float>();
}

// JSON変換の定義 (Matrix4x4)
inline void to_json(json &j, const Matrix4x4 &matrix) {
    j = json::array();
    for (int i = 0; i < 4; ++i) {
        json row = json::array();
        for (int k = 0; k < 4; ++k) {
            row.push_back(matrix.m[i][k]);
        }
        j.push_back(row);
    }
}

inline void from_json(const json &j, Matrix4x4 &matrix) {
    for (int i = 0; i < 4; ++i) {
        for (int k = 0; k < 4; ++k) {
            matrix.m[i][k] = j[i][k].get<float>();
        }
    }
}

// JSON変換の定義 (PrimitiveType)
inline void to_json(json &j, const PrimitiveType &type) {
    j = static_cast<int>(type);
}

inline void from_json(const json &j, PrimitiveType &type) {
    type = static_cast<PrimitiveType>(j.get<int>());
}

// JSON変換の定義 (BlendMode)
inline void to_json(json &j, const BlendMode &mode) {
    j = static_cast<int>(mode);
}

inline void from_json(const json &j, BlendMode &mode) {
    mode = static_cast<BlendMode>(j.get<int>());
}

// キャッシュに書き込み、ダーティフラグを立てる
template <typename T>
void DataHandler::Save(const std::string &key, const T &value) {
    cachedJson_[key] = value;
    isDirty_ = true;
}

// キャッシュから直接読み込む（ファイルアクセスなし）
template <typename T>
T DataHandler::Load(const std::string &key, const T &defaultValue) {
    if (cachedJson_.contains(key)) {
        try {
            return cachedJson_[key].get<T>();
        } catch (const json::exception &e) {
            std::cerr << "JSON Load Error: " << e.what() << " (Key: " << key << ")" << std::endl;
        }
    }
    return defaultValue;
}
} // namespace Hagine
