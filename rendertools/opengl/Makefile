.RECIPEPREFIX := >
ifeq ($(strip $(CXX)),)
  CXX := g++
endif
#CXX := clang++
AR  := ar

FILES := \
 base_displayhandler \
 base_quad \
 base_renderer \
 base_shader_funcs \
 base_shadercode \
 base_shaderhandler \
 base_texture_shaders \
 billboard \
 blur_shader \
 cube \
 cubemap \
 drawbufferhandler \
 fbo \
 fonthandler \
 framecounter \
 glbloader \
 icosphere \
 linesegment \
 mesh \
 noise \
 noisetexture \
 gfxstates \
 outlinerenderer \
 outline_shader \
 coplanar_rectangle \
 prerenderedtexture \
 primitive_shaders \
 projector \
 rendermatrices \
 shader \
 shadowmap \
 skybox \
 SSBO \
 textrenderer \
 textureatlas \
 texture \
 texturebuffer \
 texturehandler \
 tiny_gltf \
 vao \
 vbo \
 viewport

INCDIRS := ./include ../basetools/include ../../ExternalLibs/glew/include/GL /usr/include/SDL2

.PHONY: all clean DEBUG RELEASE

# Steuerung ohne Rekursion
TARGET ?= RELEASE

ifneq (,$(filter DEBUG,$(MAKECMDGOALS)))
  TARGET := DEBUG
else ifneq (,$(filter RELEASE,$(MAKECMDGOALS)))
  TARGET := RELEASE
endif

SRCDIR := src

INCLUDES := $(addprefix -I,$(INCDIRS))

DEFINES := -DUSE_STD=1 -DUSE_GLM=1 -DLINUX -DFMT_UNICODE=0
#SANITIZEFLAGS := -fsanitize=address,undefined
SANITIZEFLAGS := 
OPTFLAGS := -O3
DBGFLAGS := -g3 -O0 -fno-omit-frame-pointer $(SANITIZEFLAGS) -D_GLIBCXX_ASSERTIONS

ifeq ($(TARGET),DEBUG)
  CXXFLAGS := -std=c++20 $(DBGFLAGS) $(DEFINES) -D_DEBUG $(INCLUDES) -MMD -MP -fPIE
  OBJDIR := obj/debug
  LIB_SUFFIX := d
else ifeq ($(TARGET),RELEASE)
  CXXFLAGS := -std=c++20 $(OPTFLAGS) $(DEFINES) -DNDEBUG $(INCLUDES) -MMD -MP -fPIE
  OBJDIR := obj/release
  LIB_SUFFIX :=
else
  $(error TARGET must be DEBUG or RELEASE)
endif

SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(FILES)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

LIB := librendertools$(LIB_SUFFIX).a

DEBUG: all
RELEASE: all

all: $(LIB)

$(LIB): $(OBJECTS)
>$(AR) rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
>rm -rf $(OBJDIR) $(LIB)

-include $(OBJECTS:.o=.d)
