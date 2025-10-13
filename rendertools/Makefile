.RECIPEPREFIX := >
CXX := g++
AR  := ar
FILES := \
 base_displayhandler \
 base_quad \
 base_renderer \
 base_shadercode \
 base_shaderhandler \
 base_texture_shaders \
 blur_shader \
 cube \
 cubemap \
 drawbufferhandler \
 fbo \
 fonthandler \
 framecounter \
 icosphere \
 linesegment \
 mesh \
 noisetexture \
 opengl_states \
 outlinerenderer \
 outline_shader \
 plane \
 prerenderedtexture \
 primitive_shaders \
 projection \
 rendermatrices \
 shader \
 shader_macros \
 SSBO \
 textrenderer \
 textureatlas \
 texture \
 texturehandler \
 vao \
 vbo \
 viewport

SRCDIR := src
INCDIRS := ./include ../basetools/include ../../ExternalLibs/glew/include/GL /usr/include/SDL2
INCLUDES := $(addprefix -I,$(INCDIRS))
OBJDIR := obj
DEFINES := -DNDEBUG -DUSE_STD=1 -DUSE_GLM=1
LIB := librendertools.a

CXXFLAGS := -std=c++20 -O3 $(DEFINES) $(INCLUDES)
SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(FILES)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJECTS)
> $(AR) rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(CXXFLAGS) -c $< -o $@

rendertools.lib: $(LIB)
> cp $(LIB) $@

clean:
> rm -rf $(OBJDIR) $(LIB) librendertools.a
