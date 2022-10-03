#pragma once

/**
 * @file Entity.h Header for Entity,
 */

#include "Animation.h"
#include "Component.h"
#include "Node.h"
#include "ParameterBlock.h"
#include "ResourceComponent.h"
#include "Serialize.h"
#include "Types.h"

#include <memory>
#include <vector>

namespace ToolKit
{

  /**
   * Enums that shows the type of the Entity. Each derived class should provide
   * a type identifier for itself to make itself known to the ToolKit.
   */
  enum class EntityType
  {
    // Order is important. Don't change for backward comparable scene files.
    Entity_Base,
    Entity_AudioSource,
    Entity_Billboard,
    Entity_Cube,
    Entity_Quad,
    Entity_Sphere,
    Etity_Arrow,
    Entity_LineBatch,
    Entity_Cone,
    Entity_Drawable,
    Entity_SpriteAnim,
    Entity_Surface,
    Entity_Light,
    Entity_Camera,
    Entity_Directional,
    Entity_Node,
    Entity_Button,
    Entity_Sky,
    Entity_DirectionalLight,
    Entity_PointLight,
    Entity_SpotLight,
    Entity_Canvas,
    Entity_Prefab,
    ENTITY_TYPE_COUNT // Holds the size of the enum
  };

  static VariantCategory EntityCategory{"Meta", 100};

  /**
   * Fundamental object that all the ToolKit utilities can interacted with.
   * Entity is the base class for all the objects that can be inserted in any
   * scene.
   */
  class TK_API Entity : public Serializable
  {
   public:
    Entity();
    virtual ~Entity();

    virtual bool IsDrawable() const;
    virtual EntityType GetType() const;
    virtual void SetPose(const AnimationPtr& anim, float time);
    virtual struct BoundingBox GetAABB(bool inWorld = false) const;
    virtual Entity* Copy() const;
    virtual Entity* Instantiate() const;
    virtual void Serialize(XmlDocument* doc, XmlNode* parent) const;
    virtual void DeSerialize(XmlDocument* doc, XmlNode* parent);
    virtual void RemoveResources();
    void SetVisibility(bool vis, bool deep);
    void SetTransformLock(bool vis, bool deep);
    bool IsSurfaceInstance();
    bool IsLightInstance() const;
    ULongID GetBaseEntityID() const;
    void SetBaseEntityID(ULongID);

    /**
     * Adds the given component into the components of the Entity. While adding
     * the component to the list, function also converts the component to a
     * shared pointer for obtaining lifetime management. Also Entity set itself
     * as the new components parent.
     * @param component Component pointer that will be added to components of
     * this entity.
     */
    void AddComponent(Component* component);

    /**
     * Adds a component to the Entity same as AddComponent(Component*
     * component).
     */
    void AddComponent(ComponentPtr component);

    /**
     * Used to easily access first MeshComponentPtr.
     * @return First MeshComponentPtr if any otherwise empty pointer.
     */
    MeshComponentPtr GetMeshComponent();

    /**
     * Used to easily access first MaterialComponentPtr.
     * @return First MaterialComponentPtr if any exist, otherwise empty pointer.
     */
    MaterialComponentPtr GetMaterialComponent();

    /**
     * Remove the given component from the components of the Entity.
     * @param componentId Id of the component to be removed.
     * @return Removed ComponentPtr. If nothing gets removed, returns nullptr.
     */
    ComponentPtr RemoveComponent(ULongID componentId);

    ComponentPtrArray& GetComponentPtrArray();
    const ComponentPtrArray& GetComponentPtrArray() const;

    /**
     * Used to return first encountered component of type T.
     * @return First Component of type T if any exist, otherwise empty pointer.
     */
    template <typename T>
    std::shared_ptr<T> GetComponent() const
    {
      for (const ComponentPtr& com : GetComponentPtrArray())
      {
        if (com->GetType() == T::GetTypeStatic())
        {
          return std::reinterpret_pointer_cast<T>(com);
        }
      }

      return nullptr;
    }

    /**
     * Used to return all components of type T.
     * @param components ComponentPtrArray that will contain all encountered
     * components of type T.
     */
    template <typename T>
    void GetComponent(std::vector<std::shared_ptr<T>>& components) const
    {
      for (const ComponentPtr& com : GetComponentPtrArray())
      {
        if (com->GetType() == T::GetTypeStatic())
        {
          components.push_back(std::static_pointer_cast<T>(com));
        }
      }
    }

    /**
     * Used to return ComponentPtr with given id.
     * @param id Id of the Component that will be returned.
     * @return ComponentPtr with the given id.
     */
    ComponentPtr GetComponent(ULongID id) const;

    /**
     * Removes all components from the entity.
     */
    void ClearComponents();

    virtual Entity* InstantiateTo(Entity* other) const;

   protected:
    virtual Entity* CopyTo(Entity* other) const;
    void ParameterConstructor();
    void WeakCopy(Entity* other, bool copyComponents = true) const;

   public:
    TKDeclareParam(ULongID, Id);
    TKDeclareParam(String, Name);
    TKDeclareParam(String, Tag);
    TKDeclareParam(bool, Visible);
    TKDeclareParam(bool, TransformLock);

    /**
     * True if this entity is an instance of another entity
     * If true;
     *  Deserialization doesn't load components
     *  You should instantiate from base entity by yourself
     *   Example: Scene deserialization instantiates from base entities last
     * Don't use this outside of scene,
     *  otherwise base entity's component list can't be accessed
     */
    TKDeclareParam(bool, IsInstance);

    Node* m_node;
    ParameterBlock m_localData; // Entity's own data.

    /**
     * Internally used variable.
     * Helper ID for entity De serialization. Points to parent of the entity.
     */
    ULongID _parentId;

   private:
    // This should be private, because instantiated entities don't use this list
    // NOTE: Entity's own functions shouldn't access this either.
    //  They should use GetComponentPtrArray instead.
    ComponentPtrArray m_components;

    /**
     * Internally used variable.
     * Helper ID for finding base entity. Id must be valid in the current scene.
     */
    ULongID _baseEntityId;
  };

  class TK_API EntityNode : public Entity
  {
   public:
    EntityNode();
    explicit EntityNode(const String& name);
    virtual ~EntityNode();

    EntityType GetType() const override;
    void RemoveResources() override;
  };

  class TK_API EntityFactory
  {
   public:
    EntityFactory();
    ~EntityFactory();

    Entity* CreateByType(EntityType type);
    void OverrideEntityConstructor(EntityType type,
                                   std::function<Entity*()> fn);

   private:
    std::vector<std::function<Entity*()>> m_overrideFns;
  };

} // namespace ToolKit
