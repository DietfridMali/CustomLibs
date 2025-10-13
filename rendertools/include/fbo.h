#pragma once

#include "glew.h"

#include "array.hpp"
#include "array.hpp"
#include "viewport.h"
#include "texture.h"
#include "colordata.h"

// =================================================================================================

#define INVALID_BUFFER_INDEX 0x80000000

class BufferInfo {
public:
    typedef enum {
        btColor,
        btDepth,
        btVertex
    } eBufferType;


    SharedTextureHandle m_handle;
    int                 m_attachment;
    int                 m_tmuIndex;
    eBufferType         m_type;
    bool                m_isAttached;

    BufferInfo(GLuint handle = 0, int attachment = 0)
        : m_handle(handle), m_attachment(attachment), m_tmuIndex(-1), m_type(btColor), m_isAttached(false)
    { }

    void Init(void) {
        m_handle = 0;
        m_attachment = 0;
        m_tmuIndex = -1; 
        m_type = btColor;
        m_isAttached = false;
    }
};

// =================================================================================================

class FBO {
public:
    using DrawBufferList = ManagedArray <GLuint>;

    typedef enum {
        dbAll,
        dbColor,
        dbExtra,
        dbSingle,
        dbCustom,
        dbCount,
        dbNone = -1
    } eDrawBufferGroups;

    String                      m_name;
    SharedFramebufferHandle     m_handle;
    int                         m_width;
    int                         m_height;
    int                         m_scale;
    int                         m_bufferCount;
    int                         m_colorBufferCount;
    int                         m_vertexBufferCount;
    int                         m_vertexBufferIndex;
    int                         m_depthBufferIndex;
    int                         m_activeBufferIndex;
    ManagedArray<BufferInfo>    m_bufferInfo;
    DrawBufferList              m_drawBuffers; 
    Viewport                    m_viewport;
    Viewport*                   m_viewportSave;
    FBOTexture                  m_renderTexture;
    bool                        m_pingPong;
    bool                        m_isAvailable;
    int                         m_lastDestination;
    BaseQuad                    m_viewportArea;
    eDrawBufferGroups           m_drawBufferGroup;

    static GLuint               m_activeHandle;

    struct FBOBufferParams {
        String name{ "" };
        int colorBufferCount{ 1 };
        int depthBufferCount{ 0 };
        int vertexBufferCount{ 0 };
        bool hasMRTs{ false };
    };

    //static inline constexpr FBOBufferParams defaultBufferParams = FBOBufferParams{};

    struct FBORenderParams {
        int source{ 0 };
        int destination{ -1 };
        bool clearBuffer{ true };
        bool premultiply{ false };
        int flipVertically{ 0 }; // -1: flip, 1: don't flip, 0: renderer decides
        bool centerOrigin{ true };
        float rotation{ 0.0f };
        float scale{ 1.0f };
        Shader* shader{ nullptr };
    };

    //static inline constexpr FBORenderParams defaultBufferParams = FBORenderParams{};

    FBO();

    ~FBO() {
        Destroy();
    }

    void Init(void);

    bool Create(int width, int height, int scale, FBOBufferParams params = {});

    void Destroy(void);

    bool Enable(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll, bool clear = true, bool reenable = false);

    bool EnableBuffers(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear, bool reenable);

    inline bool Reenable(bool clear = false, bool reenable = false) {
        return Enable(m_activeBufferIndex, m_drawBufferGroup, clear, reenable);
    }

    void Disable(void);

    void SetViewport(bool flipVertically = false)
        noexcept;

    void Fill(RGBAColor color);

    void Clear(int bufferIndex, eDrawBufferGroups drawBufferGroup, bool clear);

    Texture* GetRenderTexture(FBORenderParams params = {});

    bool UpdateTransformation(const FBORenderParams& params);

    bool RenderTexture(Texture* texture, const FBORenderParams& params, const RGBAColor& color);

    inline bool RenderTexture(Texture* texture, const FBORenderParams& params, RGBAColor&& color) {
        return RenderTexture(texture, params, static_cast<const RGBAColor&>(color));
    }

    inline bool RenderTexture(Texture* texture, const FBORenderParams& params) {
        return RenderTexture(texture, params, ColorData::White);
    }

    bool Render(const FBORenderParams& params, const RGBAColor& color);

    inline bool Render(const FBORenderParams& params, RGBAColor&& color) {
        return Render(params, static_cast<const RGBAColor&>(color));
    }

    inline bool Render(const FBORenderParams& params) {
        return Render(params, ColorData::White);
    }

    bool AutoRender(const FBORenderParams& params, const RGBAColor& color);

    bool AutoRender(const FBORenderParams& params, RGBAColor&& color) {
        return AutoRender(params, static_cast<const RGBAColor&>(color));
    }

    bool AutoRender(const FBORenderParams& params) {
        return AutoRender(params, ColorData::White);
    }

    bool RenderToScreen(const FBORenderParams& params, const RGBAColor&);

    bool RenderToScreen(const FBORenderParams& params, RGBAColor&& color) {
        return RenderToScreen(params, static_cast<const RGBAColor&>(color));
    }

    inline bool RenderToScreen(const FBORenderParams& params) {
        return RenderToScreen(params, ColorData::White);
    }

    inline int GetWidth(bool scaled = false) {
        return scaled ? m_width * m_scale : m_width;
    }

    inline int GetHeight(bool scaled = false) {
        return scaled ? m_height * m_scale : m_height;
    }

    inline int GetScale(void) noexcept {
        return m_scale;
    }

    inline bool IsAvailable(void) noexcept {
        return m_isAvailable;
    }

    inline bool IsEnabled(void) noexcept {
        return (m_activeHandle != GL_NONE) and (m_activeHandle == m_handle.Data());
    }

    inline int GetLastDestination(void) noexcept {
        return m_lastDestination;
    }

    inline void SetLastDestination(int i) noexcept {
        m_lastDestination = i;
    }

    inline int NextBuffer(int i) noexcept {
        return (i + 1) % m_bufferCount;
    }

    inline GLuint GetHandle(int bufferIndex) {
        return m_bufferInfo[bufferIndex].m_handle;
    }

    FBOTexture* GetTexture(void) noexcept {
        return &m_renderTexture;
    }

    bool AttachBuffer(int bufferIndex);

    bool DetachBuffer(int bufferIndex);

    bool BindBuffer(int bufferIndex, int tmuIndex = -1);

    void ReleaseBuffers(void);

    bool SetDrawBuffers(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll, bool reenable = false);

    bool SelectDrawBuffers(int bufferIndex = -1, eDrawBufferGroups drawBufferGroup = dbAll);

    void SelectCustomDrawBuffers(DrawBufferList& drawBuffers);

    SharedTextureHandle& BufferHandle(int bufferIndex) {
#ifdef _DEBUG
        if (bufferIndex < m_bufferCount)
            return m_bufferInfo[bufferIndex].m_handle;
        return Texture::nullHandle;
#else
        return (bufferIndex < m_bufferCount) ? m_bufferInfo[bufferIndex].m_handle : Texture::nullHandle;
#endif
    }

    inline int VertexBufferIndex(int i = 0) noexcept {
        return m_vertexBufferIndex + i;
    }


    inline int DepthBufferIndex(void) noexcept {
        return m_depthBufferIndex;
    }


    inline bool operator==(const FBO& other) const noexcept {
        return m_handle == other.m_handle;
    }


    inline bool operator!=(const FBO& other) const noexcept {
        return m_handle != other.m_handle;
    }



private:
    void CreateBuffer(int bufferIndex, int& attachmentIndex, BufferInfo::eBufferType bufferType, bool isMRT);

    int CreateSpecialBuffers(BufferInfo::eBufferType bufferType, int& attachmentIndex, int bufferCount);

    bool AttachBuffers(bool hasMRTs);

    bool ReattachBuffers();

    bool DepthBufferIsActive(int bufferIndex, eDrawBufferGroups drawBufferGroup);
        
    void CreateRenderArea(void);
};

// =================================================================================================
