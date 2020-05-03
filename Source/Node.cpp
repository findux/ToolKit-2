#include "stdafx.h"
#include "Node.h"
#include "MathUtil.h"
#include "DebugNew.h"

namespace ToolKit
{

	Node::Node()
	{
	}

	Node::~Node()
	{
		if (m_parent != nullptr)
		{
			for (
				NodePtrArray::iterator itr = m_parent->m_children.begin();
				itr != m_parent->m_children.end();
				itr++
				) 
			{
				if (*itr == this)
				{
					m_parent->m_children.erase(itr);
					break;
				}
			}
		}

		for (Node* n : m_children)
		{
			n->m_parent = nullptr;
		}
	}

	void Node::Translate(const Vec3& val, TransformationSpace space)
	{
		Vec3 tmpScl = m_scale;
		m_scale = Vec3(1.0f);

		Mat4 ts = glm::translate(Mat4(), val);
		TransformImp(ts, space, &m_translation, nullptr, nullptr);

		m_scale = tmpScl;
	}

	void Node::Rotate(const Quaternion& val, TransformationSpace space)
	{
		Vec3 tmpScl = m_scale;
		m_scale = Vec3(1.0f);

		Mat4 ts = glm::toMat4(val);
		TransformImp(ts, space, nullptr, &m_orientation, nullptr);

		m_scale = tmpScl;
	}

	void Node::Scale(const Vec3& val, TransformationSpace space)
	{
		Mat4 ts = glm::diagonal4x4(Vec4(val, 1.0f));
		TransformImp(ts, space, nullptr, nullptr, &m_scale);
	}

	void Node::Transform(const Mat4& val, TransformationSpace space)
	{
		TransformImp(val, space, &m_translation, &m_orientation, &m_scale);
	}

	void Node::SetTransform(const Mat4& val, TransformationSpace space)
	{
		SetTransformImp(val, space, &m_translation, &m_orientation, &m_scale);
	}

	Mat4 Node::GetTransform(TransformationSpace space) const
	{
		Mat4 ts;
		GetTransformImp(space, &ts, nullptr, nullptr, nullptr);
		return ts;
	}

	void Node::SetTranslation(const Vec3& val, TransformationSpace space)
	{
		if (m_parent == nullptr)
		{
			if (space == TransformationSpace::TS_LOCAL)
			{
				Translate(val, space);
			}
			else
			{
				m_translation = val;
			}
		}
		else
		{
			Mat4 ts = glm::translate(Mat4(), val);
			SetTransformImp(ts, space, &m_translation, nullptr, nullptr);
		}
	}

	Vec3 Node::GetTranslation(TransformationSpace space) const
	{
		Vec3 t;
		GetTransformImp(space, nullptr, &t, nullptr, nullptr);
		return t;
	}

	void Node::SetOrientation(const Quaternion& val, TransformationSpace space)
	{
		if (m_parent == nullptr)
		{
			if (space == TransformationSpace::TS_LOCAL)
			{
				Rotate(val, space);
			}
			else
			{
				m_orientation = val;
			}
		}
		else
		{
			Mat4 ts = glm::toMat4(val);
			SetTransformImp(ts, space, nullptr, &m_orientation, nullptr);
		}
	}

	Quaternion Node::GetOrientation(TransformationSpace space) const
	{
		Quaternion q;
		GetTransformImp(space, nullptr, nullptr, &q, nullptr);
		return q;
	}

	void Node::SetScale(const Vec3& val, TransformationSpace space)
	{
		// Unless locally applied, Scale needs to preserve directions. Apply rotation first.
		switch (space)
		{
		case TransformationSpace::TS_WORLD:
		{
			Mat3 ts, ps;
			ts = glm::diagonal3x3(val);
			Quaternion ws = GetOrientation(TransformationSpace::TS_WORLD);
			if (m_parent != nullptr)
			{
				ps = m_parent->GetTransform(TransformationSpace::TS_WORLD);
			}
			ts = glm::inverse(ps) * ts * glm::toMat3(ws);
			DecomposeMatrix(ts, nullptr, nullptr, &m_scale);
		}
		break;
		case TransformationSpace::TS_PARENT:
		{
			Mat3 ts, ps;
			ts = glm::diagonal3x3(val);
			Quaternion ws = GetOrientation(TransformationSpace::TS_WORLD);
			if (m_parent != nullptr)
			{
				ps = m_parent->GetTransform(TransformationSpace::TS_WORLD);
			}
			ts = glm::inverse(ps) * ts * glm::inverse(ps) * glm::toMat3(ws);
			DecomposeMatrix(ts, nullptr, nullptr, &m_scale);
		}
		break;
		case TransformationSpace::TS_LOCAL:
			m_scale = val;
			break;
		}
	}

	Vec3 Node::GetScale(TransformationSpace space) const
	{
		Vec3 s;
		GetTransformImp(space, nullptr, nullptr, nullptr, &s);
		return s;
	}

	void Node::AddChild(Node* child)
	{
		m_children.push_back(child);
		child->m_parent = this;
	}

	Node* Node::GetRoot() const
	{
		if (m_parent == nullptr)
		{
			return nullptr;
		}

		return m_parent->GetRoot();
	}

	void Node::TransformImp(const Mat4& val, TransformationSpace space, Vec3* translation, Quaternion* orientation, Vec3* scale)
	{
		Mat4 ps, ts;
		ps = GetParentTransform();
		switch (space)
		{
		case TransformationSpace::TS_WORLD:
			ts = glm::inverse(ps) * val * ps * GetLocalTransform();
			break;
		case TransformationSpace::TS_PARENT:
			ts = val * GetLocalTransform();
			break;
		case TransformationSpace::TS_LOCAL:
			ts = GetLocalTransform() * val;
			break;
		}

		DecomposeMatrix(ts, translation, orientation, scale);
	}

	void Node::SetTransformImp(const Mat4& val, TransformationSpace space, Vec3* translation, Quaternion* orientation, Vec3* scale)
	{
		Mat4 ts;
		switch (space)
		{
		case TransformationSpace::TS_WORLD:
			if (m_parent != nullptr)
			{
				Mat4 ps = m_parent->GetTransform(TransformationSpace::TS_WORLD);
				ts = glm::inverse(ps) * val;
				break;
			} // Fall trough.
		case TransformationSpace::TS_PARENT:
			ts = val;
			break;
		case TransformationSpace::TS_LOCAL:
			Transform(val, TransformationSpace::TS_LOCAL);
			return;
		}

		DecomposeMatrix(ts, translation, orientation, scale);
	}

	void Node::GetTransformImp(TransformationSpace space, Mat4* transform, Vec3* translation, Quaternion* orientation, Vec3* scale) const
	{
		switch (space)
		{
		case TransformationSpace::TS_WORLD:
			if (m_parent != nullptr)
			{
				Mat4 ts = GetLocalTransform();
				Mat4 ps = GetParentTransform();
				ts = ps * ts;
				if (transform != nullptr)
				{
					*transform = ts;
				}
				DecomposeMatrix(ts, translation, orientation, scale);
				break;
			} // Fall trough.
		case TransformationSpace::TS_PARENT:
			if (transform != nullptr)
			{
				*transform = GetLocalTransform();
			}
			if (translation != nullptr)
			{
				*translation = m_translation;
			}
			if (orientation != nullptr)
			{
				*orientation = m_orientation;
			}
			if (scale != nullptr)
			{
				*scale = m_scale;
			}
			break;
		case TransformationSpace::TS_LOCAL:
		default:
			if (transform != nullptr)
			{
				*transform = Mat4();
			}
			if (translation != nullptr)
			{
				*translation = Vec3();
			}
			if (orientation != nullptr)
			{
				*orientation = Quaternion();
			}
			if (scale != nullptr)
			{
				*scale = Vec3(1.0f);
			}
			break;
		}
	}

	ToolKit::Mat4 Node::GetLocalTransform() const
	{
		Mat4 ts = glm::toMat4(m_orientation);
		ts = glm::scale(ts, m_scale);
		ts[3].xyz = m_translation;
		return ts;
	}

	Mat4 Node::GetParentTransform() const
	{
		Mat4 ps;
		if (m_parent != nullptr)
		{
			ps = m_parent->GetTransform(TransformationSpace::TS_WORLD);

			if (m_inheritOnlyTranslate)
			{
				Vec3 t = ps[3];
				ps = glm::translate(Mat4(), t);
			}
			else if (!m_inheritScale)
			{
				for (int i = 0; i < 3; i++)
				{
					Vec3 v = ps[i];
					ps[i].xyz = glm::normalize(v);
				}
			}
		}

		return ps;
	}

}
