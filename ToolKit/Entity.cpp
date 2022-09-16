#include "Entity.h"

#include "Light.h"
#include "MathUtil.h"
#include "Node.h"
#include "Prefab.h"
#include "ResourceComponent.h"
#include "Skeleton.h"
#include "Sky.h"
#include "ToolKit.h"
#include "Util.h"

#include "DebugNew.h"

namespace ToolKit
{

  Entity::Entity()
  {
    ParameterConstructor();

    m_node           = new Node();
    m_node->m_entity = this;
    _parentId        = 0;
  }

  Entity::~Entity()
  {
    SafeDel(m_node);
    ClearComponents();
  }

  bool Entity::IsDrawable() const
  {
    return GetComponent<MeshComponent>() != nullptr;
  }

  EntityType Entity::GetType() const
  {
    return EntityType::Entity_Base;
  }

  void Entity::SetPose(const AnimationPtr& anim, float time)
  {
    // Search for a SkinMesh
    // Animate only entity node if there is no SkinMesh
    MeshComponentPtr meshComponent = GetMeshComponent();
    if (meshComponent)
    {
      MeshPtr mesh = meshComponent->GetMeshVal();
      if (mesh->IsSkinned())
      {
        SkinMesh* skinMesh   = static_cast<SkinMesh*>(mesh.get());
        SkeletonPtr skeleton = skinMesh->m_skeleton;
        anim->GetPose(skeleton, time);
        return;
      }
    }
    // SkinMesh isn't found, animate entity nodes
    anim->GetPose(m_node, time);
  }

  struct BoundingBox Entity::GetAABB(bool inWorld) const
  {
    BoundingBox aabb;

    MeshComponentPtrArray meshCmps;
    GetComponent<MeshComponent>(meshCmps);

    if (meshCmps.empty())
    {
      // Unit aabb.
      aabb.min = Vec3(0.5f, 0.5f, 0.5f);
      aabb.max = Vec3(-0.5f, -0.5f, -0.5f);
    }
    else
    {
      BoundingBox cmpAABB;
      for (MeshComponentPtr& cmp : meshCmps)
      {
        cmpAABB = cmp->GetAABB();
        aabb.UpdateBoundary(cmpAABB.max);
        aabb.UpdateBoundary(cmpAABB.min);
      }
    }

    if (inWorld)
    {
      TransformAABB(aabb, m_node->GetTransform(TransformationSpace::TS_WORLD));
    }

    return aabb;
  }

  Entity* Entity::Copy() const
  {
    EntityType t = GetType();
    Entity* e    = GetEntityFactory()->CreateByType(t);
    return CopyTo(e);
  }

  void Entity::ClearComponents()
  {
    m_components.clear();
  }

  Entity* Entity::CopyTo(Entity* other) const
  {
    WeakCopy(other);

    other->ClearComponents();
    for (const ComponentPtr& com : m_components)
    {
      other->m_components.push_back(com->Copy(other));
    }

    return other;
  }

  Entity* Entity::Instantiate() const
  {
    EntityType t = GetType();
    Entity* e    = GetEntityFactory()->CreateByType(t);
    return InstantiateTo(e);
  }

  Entity* Entity::InstantiateTo(Entity* other) const
  {
    WeakCopy(other);
    other->SetIsInstanceVal(true);
    other->_baseEntityId = this->GetIdVal();
    return other;
  }

  void Entity::ParameterConstructor()
  {
    m_localData.m_variants.reserve(6);
    ULongID id = GetHandleManager()->GetNextHandle();

    Id_Define(id, EntityCategory.Name, EntityCategory.Priority, true, false);

    Name_Define("Entity_" + std::to_string(id),
                EntityCategory.Name,
                EntityCategory.Priority,
                true,
                true);

    Tag_Define("", EntityCategory.Name, EntityCategory.Priority, true, true);

    Visible_Define(
        true, EntityCategory.Name, EntityCategory.Priority, true, true);

    TransformLock_Define(
        false, EntityCategory.Name, EntityCategory.Priority, true, true);

    IsInstance_Define(
        false,
        EntityCategory.Name,
        EntityCategory.Priority,
        false,
        true // isInstance shouldn't change except WeakCopy is called
    );
  }

  void Entity::WeakCopy(Entity* other, bool copyComponents) const
  {
    assert(other->GetType() == GetType());
    SafeDel(other->m_node);
    other->m_node           = m_node->Copy();
    other->m_node->m_entity = other;

    // Preserve Ids.
    ULongID id         = other->GetIdVal();
    other->m_localData = m_localData;
    other->SetIdVal(id);

    if (copyComponents)
    {
      other->m_components = m_components;
    }
  }

  void Entity::AddComponent(Component* component)
  {
    AddComponent(ComponentPtr(component));
  }

  void Entity::AddComponent(ComponentPtr component)
  {
    assert(GetComponent(component->m_id) == nullptr &&
           "Component has already been added.");

    component->m_entity = this;
    m_components.push_back(component);
  }

  MeshComponentPtr Entity::GetMeshComponent()
  {
    return GetComponent<MeshComponent>();
  }

  MaterialComponentPtr Entity::GetMaterialComponent()
  {
    return GetComponent<MaterialComponent>();
  }

  ComponentPtr Entity::RemoveComponent(ULongID componentId)
  {
    for (size_t i = 0; i < m_components.size(); i++)
    {
      ComponentPtr com = m_components[i];
      if (com->m_id == componentId)
      {
        m_components.erase(m_components.begin() + i);
        return com;
      }
    }

    return nullptr;
  }

  ComponentPtrArray& Entity::GetComponentPtrArray()
  {
    if (GetIsInstanceVal())
    {
      if (GetSceneManager() && GetSceneManager()->GetCurrentScene())
      {
        Entity* baseEntity =
            GetSceneManager()->GetCurrentScene()->GetEntity(_baseEntityId);
        if (baseEntity)
        {
          return baseEntity->m_components;
        }
      }
    }
    return m_components;
  }

  const ComponentPtrArray& Entity::GetComponentPtrArray() const
  {
    if (GetIsInstanceVal())
    {
      if (GetSceneManager() && GetSceneManager()->GetCurrentScene())
      {
        Entity* baseEntity =
            GetSceneManager()->GetCurrentScene()->GetEntity(_baseEntityId);
        if (baseEntity)
        {
          return baseEntity->m_components;
        }
      }
    }
    return m_components;
  }

  ComponentPtr Entity::GetComponent(ULongID id) const
  {
    for (const ComponentPtr& com : GetComponentPtrArray())
    {
      if (com->m_id == id)
      {
        return com;
      }
    }

    return nullptr;
  }

  void Entity::Serialize(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* node = CreateXmlNode(doc, XmlEntityElement, parent);
    WriteAttr(node, doc, XmlEntityIdAttr, std::to_string(GetIdVal()));
    if (m_node->m_parent && m_node->m_parent->m_entity)
    {
      WriteAttr(node,
                doc,
                XmlParentEntityIdAttr,
                std::to_string(m_node->m_parent->m_entity->GetIdVal()));
    }
    if (GetIsInstanceVal())
    {
      WriteAttr(node, doc, XmlBaseEntityIdAttr, std::to_string(_baseEntityId));
    }

    WriteAttr(node,
              doc,
              XmlEntityTypeAttr,
              std::to_string(static_cast<int>(GetType())));

    m_node->Serialize(doc, node);
    m_localData.Serialize(doc, node);

    XmlNode* compNode = CreateXmlNode(doc, "Components", node);
    for (const ComponentPtr& cmp : m_components)
    {
      cmp->Serialize(doc, compNode);
    }
  }

  void Entity::DeSerialize(XmlDocument* doc, XmlNode* parent)
  {
    XmlNode* node = nullptr;
    if (parent != nullptr)
    {
      node = parent;
    }
    else
    {
      node = doc->first_node(XmlEntityElement.c_str());
    }

    ReadAttr(node, XmlParentEntityIdAttr, _parentId);

    if (XmlNode* transformNode = node->first_node(XmlNodeElement.c_str()))
    {
      m_node->DeSerialize(doc, transformNode);
    }

    m_localData.DeSerialize(doc, parent);

    // For instance entities, don't load components
    //  Use InstantiateTo() after deserializing base entity
    if (GetIsInstanceVal())
    {
      ReadAttr(node, XmlBaseEntityIdAttr, _baseEntityId);
      return;
    }

    ClearComponents();
    if (XmlNode* components = node->first_node("Components"))
    {
      XmlNode* comNode = components->first_node(XmlComponent.c_str());
      while (comNode)
      {
        int type = -1;
        ReadAttr(comNode, XmlParamterTypeAttr, type);
        Component* com =
            Component::CreateByType(static_cast<ComponentType>(type));

        com->DeSerialize(doc, comNode);
        AddComponent(com);

        comNode = comNode->next_sibling();
      }
    }
  }

  void Entity::RemoveResources()
  {
    assert(false && "Not implemented");
  }

  void Entity::SetVisibility(bool vis, bool deep)
  {
    SetVisibleVal(vis);
    if (deep)
    {
      EntityRawPtrArray children;
      GetChildren(this, children);
      for (Entity* c : children)
      {
        c->SetVisibility(vis, true);
      }
    }
  }

  void Entity::SetTransformLock(bool lock, bool deep)
  {
    SetTransformLockVal(lock);
    if (deep)
    {
      EntityRawPtrArray children;
      GetChildren(this, children);
      for (Entity* c : children)
      {
        c->SetTransformLock(lock, true);
      }
    }
  }

  bool Entity::IsSurfaceInstance()
  {
    switch (GetType())
    {
    case EntityType::Entity_Surface:
    case EntityType::Entity_Button:
    case EntityType::Entity_Canvas:
      return true;
    default:
      return false;
    }
  }

  bool Entity::IsLightInstance() const
  {
    EntityType type = GetType();
    return (type == EntityType::Entity_Light ||
            type == EntityType::Entity_DirectionalLight ||
            type == EntityType::Entity_PointLight ||
            type == EntityType::Entity_SpotLight);
  }

  ULongID Entity::GetBaseEntityID() const
  {
    return _baseEntityId;
  }

  void Entity::SetBaseEntityID(ULongID id)
  {
    assert(GetIsInstanceVal() &&
           "You should call this only for instance entities!");
    _baseEntityId = id;
  }

  EntityNode::EntityNode()
  {
  }

  EntityNode::EntityNode(const String& name)
  {
    SetNameVal(name);
  }

  EntityNode::~EntityNode()
  {
  }

  EntityType EntityNode::GetType() const
  {
    return EntityType::Entity_Node;
  }

  void EntityNode::RemoveResources()
  {
  }

  EntityFactory::EntityFactory()
  {
    m_overrideFns.resize(static_cast<size_t>(EntityType::ENTITY_TYPE_COUNT),
                         nullptr);
  }

  EntityFactory::~EntityFactory()
  {
    m_overrideFns.clear();
  }

  Entity* EntityFactory::CreateByType(EntityType type)
  {
    // Overriden constructors
    if (m_overrideFns[static_cast<int>(type)] != nullptr)
    {
      return m_overrideFns[static_cast<int>(type)]();
    }

    Entity* e = nullptr;
    switch (type)
    {
    case EntityType::Entity_Base:
      e = new Entity();
      break;
    case EntityType::Entity_Node:
      e = new EntityNode();
      break;
    case EntityType::Entity_AudioSource:
      e = new AudioSource();
      break;
    case EntityType::Entity_Billboard:
      e = new Billboard(Billboard::Settings());
      break;
    case EntityType::Entity_Cube:
      e = new Cube(false);
      break;
    case EntityType::Entity_Quad:
      e = new Quad(false);
      break;
    case EntityType::Entity_Sphere:
      e = new Sphere(false);
      break;
    case EntityType::Etity_Arrow:
      e = new Arrow2d(false);
      break;
    case EntityType::Entity_LineBatch:
      e = new LineBatch();
      break;
    case EntityType::Entity_Cone:
      e = new Cone(false);
      break;
    case EntityType::Entity_Drawable:
      e = new Drawable();
      break;
    case EntityType::Entity_Camera:
      e = new Camera();
      break;
    case EntityType::Entity_Surface:
      e = new Surface();
      break;
    case EntityType::Entity_Button:
      e = new Button();
      break;
    case EntityType::Entity_Light:
      e = new Light();
      break;
    case EntityType::Entity_DirectionalLight:
      e = new DirectionalLight();
      break;
    case EntityType::Entity_PointLight:
      e = new PointLight();
      break;
    case EntityType::Entity_SpotLight:
      e = new SpotLight();
      break;
    case EntityType::Entity_Sky:
      e = new Sky();
      break;
    case EntityType::Entity_Canvas:
      e = new Canvas();
      break;
    case EntityType::Entity_Prefab:
      e = new Prefab();
      break;
    case EntityType::Entity_SpriteAnim:
    case EntityType::Entity_Directional:
    default:
      assert(false);
      break;
    }

    return e;

    return nullptr;
  }

  void EntityFactory::OverrideEntityConstructor(EntityType type,
                                                std::function<Entity*()> fn)
  {
    m_overrideFns[static_cast<int>(type)] = fn;
  }

} // namespace ToolKit
