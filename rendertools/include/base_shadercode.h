#pragma once

#include <string>
#include <utility>
#include <map>
#include <type_traits>

#include "shader.h"
#include "string.hpp"

// =================================================================================================

class ShaderSource {
public:
    using KeyType = String;

    String m_name;
    String m_vs;
    String m_fs;

    ShaderSource() { }

    explicit ShaderSource(String name, String vs, String fs)
        : m_name(name), m_vs(vs), m_fs(fs)
    { }

    ShaderSource(const ShaderSource& other)
        : m_name(other.m_name), m_vs(other.m_vs), m_fs(other.m_fs)
    {
    }

    inline String& GetKey(void) {
        return m_name;
    }
#if 0    
    inline const bool operator< (ShaderSource const& other) {
        return m_name < other.m_name;
    }

    inline const bool operator> (ShaderSource const& other) {
        return m_name > other.m_name;
    }

    inline const bool operator<= (ShaderSource const& other) {
        return m_name <= other.m_name;
    }

    inline const bool operator>= (ShaderSource const& other) {
        return m_name >= other.m_name;
    }

    inline const bool operator!= (ShaderSource const& other) {
        return m_name != other.m_name;
    }

    inline const bool operator== (ShaderSource const& other) {
        return m_name == other.m_name;
    }
    
    static int Compare(const ShaderSource* o1, const ShaderSource* o2) {
        return String::Compare(nullptr, o1->m_name, o2->m_name);
    }
#endif
};

// =================================================================================================

const String& Standard2DVS();
const String& Standard3DVS();
const String& Offset2DVS();
const String& GaussBlur();

// =================================================================================================

class BaseShaderCode 
    : public Shader 
{
protected:
    Dictionary<String, Shader*> m_shaders;

public:
    BaseShaderCode();
    ~BaseShaderCode() = default;

    void AddShaders(ManagedArray<const ShaderSource*>& shaderSource);

    inline Shader* GetShader(String shaderId) {
        Shader** shader = m_shaders.Find(shaderId);
        return shader ? *shader : nullptr;
    }
};

// =================================================================================================

