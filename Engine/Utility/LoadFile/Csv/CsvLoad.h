#pragma once
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

namespace Hagine {
class CsvLoad {
  private:
    CsvLoad() = default;
    ~CsvLoad() = default;
    CsvLoad(CsvLoad &) = delete;
    CsvLoad &operator=(CsvLoad &) = delete;

  public:
    /// <summary>
    /// シングルトンインスタンスの取得
    /// </summary>
    /// <returns></returns>
      static CsvLoad* GetInstance() {
        static CsvLoad instance;
          return &instance;
    }

    /// <summary>
    /// 終了
    /// </summary>
    void Finalize();
 
    /// <summary>
    /// CSVファイルを読み込み、二次元配列の MapChip に変換
    /// </summary>
    /// <param name="filePath"></param>
    /// <returns></returns>
    std::vector<std::vector<int>> LoadCsv(const std::string &filePath);

  private:
    // CSV 読み込みキャッシュ（キー: ファイル名, 値: 二次元配列のマップチップ）
    std::unordered_map<std::string, std::vector<std::vector<int>>> csvCache_;

    // 実際に CSV を開いて読み込む関数
    std::vector<std::vector<int>> ReadCsvFile(const std::string &filePath);
};
} // namespace Hagine
