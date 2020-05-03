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
			virtual ~Cursor();

		private:
			void Generate();
		};

		class Axis3d : public Billboard
		{
		public:
			Axis3d();
			virtual ~Axis3d();

		private:
			void Generate();
		};

		class GizmoHandle
		{
		public:
			enum class SolidType
			{
				Cube,
				Cone,
				Circle
			};

			struct HandleParams
			{
				AxisLabel axis;
				Mat3 normalVectors;
				Ray dir;
				Vec2 toeTip;
				Vec3 solidDim;
				Vec3 color;
				SolidType type;
			};

		public:
			GizmoHandle();
			virtual ~GizmoHandle();

			virtual void Generate(const HandleParams& params);
			virtual bool HitTest(const Ray& ray, float& t) const;

		public:
			MeshPtr m_mesh;

		protected:
			HandleParams m_params;
		};

		class PolarHandle : public GizmoHandle
		{
		public:
			virtual bool HitTest(const Ray& ray, float& t) const override;
		};

		class Gizmo : public Billboard
		{
		public:
			Gizmo(const Billboard::Settings& set);
			virtual ~Gizmo();

			virtual AxisLabel HitTest(const Ray& ray) const;
			virtual void Update(float deltaTime) = 0;
			bool IsLocked(AxisLabel axis) const;
			void Lock(AxisLabel axis);
			void UnLock(AxisLabel axis);
			bool IsGrabbed(AxisLabel axis) const;
			void Grab(AxisLabel axis);
			AxisLabel GetGrabbedAxis() const;

		public:
			Mat3 m_normalVectors;

		protected:
			std::vector<GizmoHandle*> m_handles;
			std::vector<AxisLabel> m_lockedAxis;
			AxisLabel m_grabbedAxis;
		};

		class LinearGizmo : public Gizmo
		{
		public:
			LinearGizmo();
			virtual ~LinearGizmo();

			virtual void Update(float deltaTime) override;

		protected:
			virtual GizmoHandle::HandleParams GetParam() const;
		};

		class MoveGizmo : public LinearGizmo
		{
		public:
			MoveGizmo();
			virtual ~MoveGizmo();
		};

		class ScaleGizmo : public LinearGizmo
		{
		public:
			ScaleGizmo();
			virtual ~ScaleGizmo();

		protected:
			virtual GizmoHandle::HandleParams GetParam() const override;
		};

		class PolarGizmo : public Gizmo 
		{
			using Billboard::LookAt;
		public:
			PolarGizmo();
			virtual ~PolarGizmo();

			virtual void LookAt(Camera* cam) override;
			virtual void Update(float deltaTime) override;
		};
	}
}
