.RECIPEPREFIX := >
ifeq ($(strip $(CXX)),)
  CXX := g++
endif
#CXX := clang++
AR  := ar

# OpenGL-spezifische Quelldateien (src/ relativ zu diesem Makefile)
OPENGL_SRCDIR := ./src

# API-neutrale gemeinsame Quelldateien (../src/ = rendertools/src/)
SRCDIR := ../src

OPENGL_FILES := \
 base_displayhandler \
 base_shader_funcs \
 base_shadercode \
 base_texture_shaders \
 blur_shader \
 cubemap \
 gfxdatabuffer \
 gfxdatalayout \
 gfxstates \
 gfxrenderer \
 noisetexture \
 outline_shader \
 prerenderedtexture \
 primitive_shaders \
 rendertarget \
 shader \
 skybox \
 texture \
 texturebuffer \
 texturehandler

COMMON_FILES := \
 base_quad \
 base_renderer \
 base_shaderhandler \
 billboard \
 coplanar_rectangle \
 cube \
 drawbufferhandler \
 fonthandler \
 framecounter \
 glbloader \
 icosphere \
 linesegment \
 mesh \
 noise \
 projector \
 rendermatrices \
 shadowmap \
 texteffects \
 textrenderer \
 textureatlas \
 textureprocessing \
 tiny_gltf \
 vertexdatabuffers \
 viewport

INCDIRS := \
	./include ../include ../src ../../basetools/include \
	/usr/include/ /usr/include/GL /usr/include/SDL2 \
	/opt/homebrew/include /opt/homebrew/include/gl /opt/homebrew/include/SDL2

.PHONY: all clean DEBUG RELEASE

# Steuerung ohne Rekursion
TARGET ?= RELEASE

ifneq (,$(filter DEBUG,$(MAKECMDGOALS)))
  TARGET := DEBUG
else ifneq (,$(filter RELEASE,$(MAKECMDGOALS)))
  TARGET := RELEASE
endif

INCLUDES := $(addprefix -I,$(INCDIRS))

DEFINES := -DOPENGL -DUSE_STD=1 -DUSE_GLM=1 -DLINUX -DFMT_UNICODE=0
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

OPENGL_SOURCES := $(addprefix $(OPENGL_SRCDIR)/,$(addsuffix .cpp,$(OPENGL_FILES)))
COMMON_SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(COMMON_FILES)))
OPENGL_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(OPENGL_FILES)))
COMMON_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(COMMON_FILES)))
OBJECTS := $(OPENGL_OBJECTS) $(COMMON_OBJECTS)

LIB := librendertools$(LIB_SUFFIX).a

DEBUG: all
RELEASE: all

all: $(LIB)

$(LIB): $(OBJECTS)
>$(AR) rcs $@ $^

$(OPENGL_OBJECTS): $(OBJDIR)/%.o: $(OPENGL_SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(COMMON_OBJECTS): $(OBJDIR)/%.o: $(SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
>rm -rf $(OBJDIR) $(LIB)

-include $(OBJECTS:.o=.d)
