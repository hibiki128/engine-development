#include "CsvLoad.h"
#include <cassert>

namespace Hagine {
void CsvLoad::Finalize() {
   
}

// CSVファイルを読み込み、キャッシュに存在すればそれを返す
std::vector<std::vector<int>> CsvLoad::LoadCsv(const std::string &filePath) {
    // すでにキャッシュに存在する場合、それを返す
    if (csvCache_.find(filePath) != csvCache_.end()) {
        return csvCache_[filePath];
    }

    // CSVファイルを読み込む
    std::vector<std::vector<int>> mapChipData = ReadCsvFile(filePath);

    // 読み込んだデータをキャッシュに保存し、コピーを返す
    csvCache_[filePath] = mapChipData;
    return mapChipData;
}

// CSVファイルを開いてデータを二次元配列に変換する関数
std::vector<std::vector<int>> CsvLoad::ReadCsvFile(const std::string &filePath) {
    std::vector<std::vector<int>> mapChipGrid;
    std::ifstream file(filePath);

    // ファイルが開けなかった場合、assert で強制停止
    assert(file && "ファイルがありません");

    std::string line;
    while (std::getline(file, line)) {
        std::vector<int> row;
        std::stringstream ss(line);
        std::string cell;

        while (std::getline(ss, cell, ',')) {
            try {
                int chipId = std::stoi(cell);
                row.emplace_back(chipId);
            } catch (const std::exception &e) {
                // 無効なデータが含まれていたら assert で停止
                std::cerr << "Error: Invalid data in CSV (" << cell << ") -> " << e.what() << std::endl;
                assert(false && "Invalid data in CSV file!");
            }
        }

        if (!row.empty()) {
            mapChipGrid.push_back(row);
        }
    }

    return mapChipGrid;
}
} // namespace Hagine
