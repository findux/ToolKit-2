#include "Material.h"

#include "ToolKit.h"
#include "Util.h"
#include "rapidxml.hpp"
#include "rapidxml_utils.hpp"

#include "DebugNew.h"

namespace ToolKit
{

  Material::Material()
  {
    m_color = Vec3(1.0f);
    m_alpha = 1.0f;
  }

  Material::Material(String file) : Material() { SetFile(file); }

  Material::~Material() { UnInit(); }

  void Material::Load()
  {
    if (m_loaded)
    {
      return;
    }

    XmlFilePtr file = GetFileManager()->GetXmlFile(GetFile());
    XmlDocument doc;
    doc.parse<0>(file->data());

    XmlNode* rootNode = doc.first_node("material");
    DeSerialize(&doc, rootNode);

    m_loaded = true;
  }

  void Material::Save(bool onlyIfDirty)
  {
    Resource::Save(onlyIfDirty);
    if (m_vertexShader)
    {
      m_vertexShader->Save(onlyIfDirty);
    }

    if (m_fragmentShader)
    {
      m_fragmentShader->Save(onlyIfDirty);
    }
  }

  void Material::Init(bool flushClientSideArray)
  {
    if (m_initiated)
    {
      return;
    }

    if (m_diffuseTexture)
    {
      m_diffuseTexture->Init(flushClientSideArray);
      m_renderState.diffuseTexture      = m_diffuseTexture->m_textureId;
      m_renderState.diffuseTextureInUse = true;
    }

    if (m_emissiveTexture)
    {
      m_emissiveTexture->Init(flushClientSideArray);
      m_renderState.emissiveTexture      = m_emissiveTexture->m_textureId;
      m_renderState.emissiveTextureInUse = true;
    }

    if (m_metallicRoughnessTexture)
    {
      m_metallicRoughnessTexture->Init(flushClientSideArray);
    }

    if (m_cubeMap)
    {
      m_cubeMap->Init(flushClientSideArray);
      m_renderState.cubeMap      = m_cubeMap->m_textureId;
      m_renderState.cubeMapInUse = true;
    }

    if (m_vertexShader)
    {
      m_vertexShader->Init();
    }
    else
    {
      m_vertexShader = GetShaderManager()->Create<Shader>(
          ShaderPath("defaultVertex.shader", true));
      m_vertexShader->Init();
    }

    if (m_fragmentShader)
    {
      m_fragmentShader->Init();
    }
    else
    {
      m_fragmentShader = GetShaderManager()->Create<Shader>(
          ShaderPath("defaultFragment.shader", true));
      m_fragmentShader->Init();
    }

    m_initiated = true;
  }

  void Material::UnInit() { m_initiated = false; }

  void Material::CopyTo(Resource* other)
  {
    Resource::CopyTo(other);
    Material* cpy          = static_cast<Material*>(other);
    cpy->m_cubeMap         = m_cubeMap;
    cpy->m_diffuseTexture  = m_diffuseTexture;
    cpy->m_vertexShader    = m_vertexShader;
    cpy->m_fragmentShader  = m_fragmentShader;
    cpy->m_color           = m_color;
    cpy->m_alpha           = m_alpha;
    cpy->m_renderState     = m_renderState;
    cpy->m_dirty           = true;
    cpy->m_emissiveTexture = m_emissiveTexture;
    cpy->m_emissiveColor   = m_emissiveColor;
  }

  RenderState* Material::GetRenderState()
  {
    if (m_diffuseTexture)
    {
      m_renderState.diffuseTextureInUse = true;
      m_renderState.diffuseTexture      = m_diffuseTexture->m_textureId;
    }
    else
    {
      m_renderState.diffuseTextureInUse = false;
    }
    if (m_emissiveTexture)
    {
      m_renderState.emissiveTextureInUse = true;
      m_renderState.emissiveTexture      = m_emissiveTexture->m_textureId;
    }
    else
    {
      m_renderState.emissiveTextureInUse = false;
    }

    if (m_cubeMap)
    {
      m_renderState.cubeMap = m_cubeMap->m_textureId;
    }
    else
    {
      m_renderState.cubeMap = false;
    }

    return &m_renderState;
  }

  void Material::SetRenderState(RenderState* state)
  {
    m_renderState = *state; // Copy
  }

  void Material::SetDefaultMaterialTypeShaders()
  {
    switch (m_materialType)
    {
    case MaterialType::Phong:
    case MaterialType::PBR:
      UnInit();
      m_vertexShader = GetShaderManager()->Create<Shader>(
          ShaderPath("defaultVertex.shader", true));
      m_fragmentShader = GetShaderManager()->Create<Shader>(
          ShaderPath("defaultFragment.shader", true));
      Init();
      break;
    default: // Custom
      break;
    }
  }

  void Material::Serialize(XmlDocument* doc, XmlNode* parent) const
  {
    XmlNode* container = CreateXmlNode(doc, "material", parent);

    if (m_diffuseTexture && !m_renderState.isColorMaterial)
    {
      XmlNode* node = CreateXmlNode(doc, "diffuseTexture", container);
      String file =
          GetRelativeResourcePath(m_diffuseTexture->GetSerializeFile());
      WriteAttr(node, doc, XmlNodeName.data(), file);
    }

    if (m_cubeMap)
    {
      XmlNode* node = CreateXmlNode(doc, "cubeMap", container);
      String file   = GetRelativeResourcePath(m_cubeMap->GetSerializeFile());
      WriteAttr(node, doc, XmlNodeName.data(), file);
    }

    if (m_vertexShader)
    {
      XmlNode* node = CreateXmlNode(doc, "shader", container);
      String file = GetRelativeResourcePath(m_vertexShader->GetSerializeFile());
      WriteAttr(node, doc, XmlNodeName.data(), file);
    }

    if (m_fragmentShader)
    {
      XmlNode* node = CreateXmlNode(doc, "shader", container);
      String file =
          GetRelativeResourcePath(m_fragmentShader->GetSerializeFile());
      WriteAttr(node, doc, XmlNodeName.data(), file);
    }

    if (m_emissiveTexture)
    {
      XmlNode* node = CreateXmlNode(doc, "emissiveTexture", container);
      String file =
          GetRelativeResourcePath(m_emissiveTexture->GetSerializeFile());
      WriteAttr(node, doc, XmlNodeName.data(), file);
    }

    if (m_metallicRoughnessTexture)
    {
      XmlNode* node = CreateXmlNode(doc, "metallicRoughnessTexture", container);
      String file   = GetRelativeResourcePath(
          m_metallicRoughnessTexture->GetSerializeFile());
      WriteAttr(node, doc, XmlNodeName.data(), file);
    }

    XmlNode* node = CreateXmlNode(doc, "color", container);
    WriteVec(node, doc, m_color);

    node = CreateXmlNode(doc, "emissiveColor", container);
    WriteVec(node, doc, m_emissiveColor);

    node = CreateXmlNode(doc, "alpha", container);
    WriteAttr(node, doc, XmlNodeName.data(), std::to_string(m_alpha));

    node = CreateXmlNode(doc, "metallic", container);
    WriteAttr(node, doc, XmlNodeName.data(), std::to_string(m_metallic));

    node = CreateXmlNode(doc, "roughness", container);
    WriteAttr(node, doc, XmlNodeName.data(), std::to_string(m_roughness));

    node = CreateXmlNode(doc, "materialType", container);
    WriteAttr(node,
              doc,
              XmlNodeName.data(),
              std::to_string((int) m_materialType));

    m_renderState.Serialize(doc, container);
  }

  void Material::DeSerialize(XmlDocument* doc, XmlNode* parent)
  {
    if (parent == nullptr)
    {
      return;
    }

    XmlNode* rootNode = parent;
    for (XmlNode* node = rootNode->first_node(); node;
         node          = node->next_sibling())
    {
      if (strcmp("diffuseTexture", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);
        m_diffuseTexture =
            GetTextureManager()->Create<Texture>(TexturePath(path));
        m_renderState.isColorMaterial = false;
      }
      else if (strcmp("cubeMap", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);
        m_cubeMap = GetTextureManager()->Create<CubeMap>(TexturePath(path));
      }
      else if (strcmp("shader", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);
        ShaderPtr shader = GetShaderManager()->Create<Shader>(ShaderPath(path));
        if (shader->m_shaderType == ShaderType::VertexShader)
        {
          m_vertexShader = shader;
        }
        else if (shader->m_shaderType == ShaderType::FragmentShader)
        {
          m_fragmentShader = shader;
        }
        else
        {
          assert(false);
        }
      }
      else if (strcmp("color", node->name()) == 0)
      {
        ReadVec(node, m_color);
      }
      else if (strcmp("alpha", node->name()) == 0)
      {
        ReadAttr(node, XmlNodeName.data(), m_alpha);
      }
      else if (strcmp("renderState", node->name()) == 0)
      {
        m_renderState.DeSerialize(doc, parent);
      }
      else if (strcmp("emissiveTexture", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);
        m_emissiveTexture =
            GetTextureManager()->Create<Texture>(TexturePath(path));
      }
      else if (strcmp("emissiveColor", node->name()) == 0)
      {
        ReadVec(node, m_emissiveColor);
      }
      else if (strcmp("metallicRoughnessTexture", node->name()) == 0)
      {
        XmlAttribute* attr = node->first_attribute(XmlNodeName.data());
        String path        = attr->value();
        NormalizePath(path);
        m_metallicRoughnessTexture =
            GetTextureManager()->Create<Texture>(TexturePath(path));
      }
      else if (strcmp("metallic", node->name()) == 0)
      {
        ReadAttr(node, XmlNodeName.data(), m_metallic);
      }
      else if (strcmp("roughness", node->name()) == 0)
      {
        ReadAttr(node, XmlNodeName.data(), m_roughness);
      }
      else if (strcmp("materialType", node->name()) == 0)
      {
        int matType;
        ReadAttr(node, XmlNodeName.data(), matType);
        m_materialType = (MaterialType)matType;
      }
      else
      {
        assert(false);
      }
    }
  }

  MaterialManager::MaterialManager() { m_type = ResourceType::Material; }

  MaterialManager::~MaterialManager() {}

  void MaterialManager::Init()
  {
    ResourceManager::Init();

    Material* material       = new Material();
    material->m_vertexShader = GetShaderManager()->Create<Shader>(
        ShaderPath("defaultVertex.shader", true));
    material->m_fragmentShader = GetShaderManager()->Create<Shader>(
        ShaderPath("defaultFragment.shader", true));
    material->m_diffuseTexture =
        GetTextureManager()->Create<Texture>(TexturePath("default.png", true));
    material->GetRenderState()->isColorMaterial = false;
    material->Init();

    m_storage[MaterialPath("default.material", true)] = MaterialPtr(material);

    material                                          = new Material();
    material->m_vertexShader = GetShaderManager()->Create<Shader>(
        ShaderPath("defaultVertex.shader", true));
    material->m_fragmentShader = GetShaderManager()->Create<Shader>(
        ShaderPath("unlitFrag.shader", true));
    material->m_diffuseTexture =
        GetTextureManager()->Create<Texture>(TexturePath("default.png", true));
    material->GetRenderState()->useForwardPath  = true;
    material->GetRenderState()->isColorMaterial = false;
    material->Init();

    m_storage[MaterialPath("unlit.material", true)] = MaterialPtr(material);
  }

  bool MaterialManager::CanStore(ResourceType t)
  {
    return t == ResourceType::Material;
  }

  ResourcePtr MaterialManager::CreateLocal(ResourceType type)
  {
    return ResourcePtr(new Material());
  }

  String MaterialManager::GetDefaultResource(ResourceType type)
  {
    return MaterialPath("missing.material", true);
  }

  MaterialPtr MaterialManager::GetCopyOfUnlitMaterial()
  {
    return m_storage[MaterialPath("unlit.material", true)]->Copy<Material>();
  }

  MaterialPtr MaterialManager::GetCopyOfUIMaterial()
  {
    MaterialPtr material = GetMaterialManager()->GetCopyOfUnlitMaterial();
    material->UnInit();
    material->GetRenderState()->blendFunction =
        BlendFunction::SRC_ALPHA_ONE_MINUS_SRC_ALPHA;
    material->GetRenderState()->depthTestEnabled = true;

    return material;
  }

  MaterialPtr MaterialManager::GetCopyOfUnlitColorMaterial()
  {
    MaterialPtr umat                        = GetCopyOfUnlitMaterial();
    umat->GetRenderState()->isColorMaterial = true;
    return umat;
  }

  MaterialPtr MaterialManager::GetCopyOfDefaultMaterial()
  {
    return m_storage[MaterialPath("default.material", true)]->Copy<Material>();
  }

} // namespace ToolKit
