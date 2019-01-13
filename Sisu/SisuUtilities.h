#pragma once
namespace Sisu
{
	struct Vector3
	{
		float x, y, z;
	};

	struct Color
	{
		float r, g, b, a;
	};

	struct Vector4
	{
		float x, y, z, w;
	};

	struct Matrix4
	{
		Vector4 row0, row1, row2, row3;
	};
}
