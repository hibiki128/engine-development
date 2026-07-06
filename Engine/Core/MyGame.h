#pragma once
#include"Framework.h"
#include "memory"

namespace Hagine {
class MyGame : public Framework
{
public: // メンバ関数
	
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 終了
	/// </summary>
	void Finalize()override;

	/// <summary>
	/// 更新
	/// </summary>
	void Update()override;

	/// <summary>
	/// 描画
	/// </summary>
	void Draw()override;

private:
	// シーン

};
} // namespace Hagine
