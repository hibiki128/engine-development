#include "DataHandler.h"

namespace Hagine {
DataHandler::DataHandler(const std::string &folder, const std::string &file) {
    folderPath_ = basePath_ + "/" + folder;
    fileName_ = file + ".json";
    fs::create_directories(folderPath_); // フォルダを作成
    LoadFromFile();                     // コンストラクタで一度だけファイルを読み込む
}

void DataHandler::DeleteJson(const std::string &jsonName) {
    std::string filePath = folderPath_ + "/" + jsonName + ".json";

    if (fs::exists(filePath)) {
        fs::remove(filePath);
    }
}

bool DataHandler::Exists() const {
    std::string filePath = folderPath_ + "/" + fileName_;
    return fs::exists(filePath);
}

void DataHandler::DeleteAllJsonsInFolder() {
    std::string fullFolderPath = folderPath_;

    if (!fs::exists(fullFolderPath) || !fs::is_directory(fullFolderPath)) {
        return;
    }

    for (const auto &entry : fs::directory_iterator(fullFolderPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            fs::remove(entry.path()); // ファイルごと削除
        }
    }
}

// 明示的なテンプレートインスタンス化
template void DataHandler::Save<int>(const std::string &, const int &);
template void DataHandler::Save<int32_t>(const std::string &, const int32_t &);
template void DataHandler::Save<uint32_t>(const std::string &, const uint32_t &);
template void DataHandler::Save<float>(const std::string &, const float &);
template void DataHandler::Save<std::string>(const std::string &, const std::string &);
template void DataHandler::Save<Vector2>(const std::string &, const Vector2 &);
template void DataHandler::Save<Vector3>(const std::string &, const Vector3 &);
template void DataHandler::Save<Vector4>(const std::string &, const Vector4 &);
template void DataHandler::Save<Quaternion>(const std::string &, const Quaternion &);
template void DataHandler::Save<PrimitiveType>(const std::string &, const PrimitiveType &);
template void DataHandler::Save<Matrix4x4>(const std::string &, const Matrix4x4 &);
template void DataHandler::Save<BlendMode>(const std::string &, const BlendMode &);

template int DataHandler::Load<int>(const std::string &, const int &);
template int32_t DataHandler::Load<int32_t>(const std::string &, const int32_t &);
template uint32_t DataHandler::Load<uint32_t>(const std::string &, const uint32_t &);
template float DataHandler::Load<float>(const std::string &, const float &);
template std::string DataHandler::Load<std::string>(const std::string &, const std::string &);
template Vector2 DataHandler::Load<Vector2>(const std::string &, const Vector2 &);
template Vector3 DataHandler::Load<Vector3>(const std::string &, const Vector3 &);
template Vector4 DataHandler::Load<Vector4>(const std::string &, const Vector4 &);
template Quaternion DataHandler::Load<Quaternion>(const std::string &, const Quaternion &);
template PrimitiveType DataHandler::Load<PrimitiveType>(const std::string &, const PrimitiveType &);
template Matrix4x4 DataHandler::Load<Matrix4x4>(const std::string &, const Matrix4x4 &);
template BlendMode DataHandler::Load<BlendMode>(const std::string &, const BlendMode &);
} // namespace Hagine
