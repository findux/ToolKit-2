#pragma once

#include "Primative.h"
#include "MathUtil.h"

namespace ToolKit
{
	namespace Editor
	{
		class Cursor : public Billboard
		{
		public:
			Cursor();

		private:
			void Generate();
		};

		class Axis3d : public Billboard
		{
		public:
			Axis3d();

		private:
			void Generate();
		};

		class MoveGizmo : public Billboard
		{
		public:
			MoveGizmo();

			AxisLabel HitTest(const Ray& ray);
			void Update(float deltaTime);

		private:
			void Generate();

		public:
			AxisLabel m_inAccessable;

		private:
			BoundingBox m_hitBox[3]; // X - Y - Z.
			std::shared_ptr<Mesh> m_lines[3];
			std::shared_ptr<Mesh> m_solids[3];
		};
	}
}
