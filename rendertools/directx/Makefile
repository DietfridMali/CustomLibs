.RECIPEPREFIX := >
ifeq ($(strip $(CXX)),)
  CXX := g++
endif
#CXX := clang++
AR  := ar

DIRECTX_SRCDIR := ./src

EXTERNAL_LIBS_DIR := ../../../ExternalLibs

SRCDIR := ../src

HLSL_SRCDIR := ../src/common-hlsl

DIRECTX_FILES := \
 acceleration_structure \
 base_displayhandler \
 base_shadercode \
 cbv_allocator \
 commandlist \
 compute_shader \
 cubemap \
 descriptor_heap \
 dx12context \
 dx12upload \
 gfxdatabuffer \
 gfxdatalayout \
 gfxrenderer \
 gfxstates \
 meshhandler \
 noisetexture \
 readtarget \
 renderstates \
 rendertarget \
 resource_chunkhandler \
 resource_handler \
 resource_view \
 sampler_cache \
 shader \
 texture \
 texturearray \
 texturebuffer \
 texturehandler

HLSL_FILES := \
 base_shader_funcs \
 base_texture_shaders \
 blackhole_shader \
 blur_shader \
 lightning_shader \
 line_shader \
 outline_shader \
 primitive_shaders \
 skybox_shader \
 tonemap_shader

COMMON_FILES := \
 base_noisetexture \
 base_quadmesh \
 base_texturearray \
 base_renderer \
 base_shaderhandler \
 billboard \
 cube \
 drawbufferhandler \
 ddsloader \
 fonthandler \
 framecounter \
 glbloader \
 icosphere \
 kuwaharafilter \
 lightningnoise \
 lightningbolt \
 lightningemitter \
 lightningsystem \
 linerenderer \
 linesegment \
 mesh \
 noise \
 prerenderedtexture \
 projector \
 quad \
 rendermatrices \
 shadowmap \
 skybox \
 texteffects \
 textrenderer \
 texture_mips \
 textureatlas \
 variabletextureatlas \
 textureprocessing \
 tiny_gltf \
 viewport

INCDIRS := \
	./include ../include ../src ../../basetools/include \
	$(EXTERNAL_LIBS_DIR)/DXC/include \
	/usr/include/ /usr/include/SDL2 \
	/opt/homebrew/include /opt/homebrew/include/SDL2

.PHONY: all clean DEBUG RELEASE

# Steuerung ohne Rekursion
TARGET ?= RELEASE

ifneq (,$(filter DEBUG,$(MAKECMDGOALS)))
  TARGET := DEBUG
else ifneq (,$(filter RELEASE,$(MAKECMDGOALS)))
  TARGET := RELEASE
endif

INCLUDES := $(addprefix -I,$(INCDIRS))

DEFINES := -DDIRECTX -DUSE_STD=1 -DUSE_GLM=1 -DGLM_FORCE_DEPTH_ZERO_TO_ONE -DLINUX -DFMT_UNICODE=0
#SANITIZEFLAGS := -fsanitize=address,undefined
SANITIZEFLAGS := 
OPTFLAGS := -O3
DBGFLAGS := -g3 -O0 -fno-omit-frame-pointer $(SANITIZEFLAGS) -D_GLIBCXX_ASSERTIONS

ifeq ($(TARGET),DEBUG)
  CXXFLAGS := -std=c++20 $(DBGFLAGS) $(DEFINES) -D_DEBUG -DDBG_DIRECTX=1 $(INCLUDES) -MMD -MP -fPIE
  OBJDIR := obj/debug
  LIB_SUFFIX := d
else ifeq ($(TARGET),RELEASE)
  CXXFLAGS := -std=c++20 $(OPTFLAGS) $(DEFINES) -DNDEBUG -DDBG_DIRECTX=0 $(INCLUDES) -MMD -MP -fPIE
  OBJDIR := obj/release
  LIB_SUFFIX :=
else
  $(error TARGET must be DEBUG or RELEASE)
endif

DIRECTX_SOURCES := $(addprefix $(DIRECTX_SRCDIR)/,$(addsuffix .cpp,$(DIRECTX_FILES)))
HLSL_SOURCES := $(addprefix $(HLSL_SRCDIR)/,$(addsuffix .cpp,$(HLSL_FILES)))
COMMON_SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(COMMON_FILES)))
DIRECTX_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(DIRECTX_FILES)))
HLSL_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(HLSL_FILES)))
COMMON_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(COMMON_FILES)))
OBJECTS := $(DIRECTX_OBJECTS) $(HLSL_OBJECTS) $(COMMON_OBJECTS)

LIB := librendertools$(LIB_SUFFIX).a

DEBUG: all
RELEASE: all

all: $(LIB)

$(LIB): $(OBJECTS)
>$(AR) rcs $@ $^

$(DIRECTX_OBJECTS): $(OBJDIR)/%.o: $(DIRECTX_SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(HLSL_OBJECTS): $(OBJDIR)/%.o: $(HLSL_SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(COMMON_OBJECTS): $(OBJDIR)/%.o: $(SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
>rm -rf $(OBJDIR) $(LIB)

-include $(OBJECTS:.o=.d)
