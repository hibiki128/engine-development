#pragma once
#include"type/Vector4.h"
#include"d3d12.h"
#include"wrl.h"
#include"DirectXCommon.h"

// 定数バッファ用データ構造体
namespace Hagine {
struct ConstBufferDataObjColor {
	Vector4 color_;
};

class ObjColor
{
public:

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// 行列の転送
	/// </summary>
	void TransferMatrix();

	/// <summary>
	/// グラフィックスコマンドを積む
	/// </summary>
	void SetGraphicCommand(UINT rootParameterIndex)const;

	const Vector4 GetColor(){
		return color_;
	}

	void SetColor(Vector4 color) {
            color_ = color;
	}

private:

	DirectXCommon* dxCommon_ = nullptr;

	Vector4 color_ = { 1.0f,1.0f,1.0f,1.0f, };
	// 定数バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> constBuffer_;
	// マッピング済みアドレス
	ConstBufferDataObjColor* constMap_ = nullptr;

	/// <summary>
	/// 定数バッファ生成
	/// </summary>
	void CreateConstBuffer();

	/// <summary>
	/// マッピングする
	/// </summary>
	void Map();
};

} // namespace Hagine
