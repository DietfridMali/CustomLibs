.RECIPEPREFIX := >
ifeq ($(strip $(CXX)),)
  CXX := g++
endif
ifeq ($(strip $(CC)),)
  CC := gcc
endif
#CXX := clang++
AR  := ar

# Vulkan-spezifische Quelldateien (src/ relativ zu diesem Makefile)
VULKAN_SRCDIR := ./src

EXTERNAL_LIBS_DIR := ../../../ExternalLibs

VULKAN_SDK_DIR := $(EXTERNAL_LIBS_DIR)/VulkanSDK

VULKAN_INC_DIR := $(VULKAN_SDK_DIR)/include

# API-neutrale gemeinsame Quelldateien (../src/ = rendertools/src/)
SRCDIR := ../src

# SPIRV-Reflect-Quelle (aus dem Vulkan SDK), wird als C in die Lib mitkompiliert
SPIRV_REFLECT_DIR := $(VULKAN_INC_DIR)/SPIRV_Reflect

VULKAN_FILES := \
 base_displayhandler \
 base_shader_funcs \
 base_shadercode \
 base_texture_shaders \
 blur_shader \
 cbv_allocator \
 commandlist \
 cubemap \
 descriptor_pool_handler \
 gfx_buffer \
 gfxdatabuffer \
 gfxdatalayout \
 gfxrenderer \
 gfxstates \
 image_layout_tracker \
 noisetexture \
 outline_shader \
 pipeline_cache \
 primitive_shaders \
 renderstates \
 rendertarget \
 resource_chunkhandler \
 resource_handler \
 sampler_cache \
 shader \
 shader_compiler \
 skybox \
 swapchain \
 texture \
 texturebuffer \
 texturehandler \
 vkcontext \
 vkupload

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
 prerenderedtexture \
 projector \
 rendermatrices \
 shadowmap \
 texteffects \
 textrenderer \
 textureatlas \
 textureprocessing \
 tiny_gltf \
 viewport

INCDIRS := \
	./include ../include ../src ../../basetools/include \
	$(VULKAN_INC_DIR) \
	$(VULKAN_INC_DIR)/vma \
	$(VULKAN_INC_DIR)/SPIRV_reflect \
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

DEFINES := -DVULKAN -DUSE_STD=1 -DUSE_GLM=1 -DLINUX -DFMT_UNICODE=0
#SANITIZEFLAGS := -fsanitize=address,undefined
SANITIZEFLAGS :=
OPTFLAGS := -O3
DBGFLAGS := -g3 -O0 -fno-omit-frame-pointer $(SANITIZEFLAGS) -D_GLIBCXX_ASSERTIONS

ifeq ($(TARGET),DEBUG)
  CXXFLAGS := -std=c++20 $(DBGFLAGS) $(DEFINES) -D_DEBUG $(INCLUDES) -MMD -MP -fPIE
  CFLAGS := $(DBGFLAGS) -D_DEBUG $(INCLUDES) -MMD -MP -fPIE
  OBJDIR := obj/debug
  LIB_SUFFIX := d
else ifeq ($(TARGET),RELEASE)
  CXXFLAGS := -std=c++20 $(OPTFLAGS) $(DEFINES) -DNDEBUG $(INCLUDES) -MMD -MP -fPIE
  CFLAGS := $(OPTFLAGS) -DNDEBUG $(INCLUDES) -MMD -MP -fPIE
  OBJDIR := obj/release
  LIB_SUFFIX :=
else
  $(error TARGET must be DEBUG or RELEASE)
endif

VULKAN_SOURCES := $(addprefix $(VULKAN_SRCDIR)/,$(addsuffix .cpp,$(VULKAN_FILES)))
COMMON_SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(COMMON_FILES)))
VULKAN_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(VULKAN_FILES)))
COMMON_OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(COMMON_FILES)))
SPIRV_REFLECT_OBJECT := $(OBJDIR)/spirv_reflect.o
OBJECTS := $(VULKAN_OBJECTS) $(COMMON_OBJECTS) $(SPIRV_REFLECT_OBJECT)

LIB := librendertools$(LIB_SUFFIX).a

DEBUG: all
RELEASE: all

all: $(LIB)

$(LIB): $(OBJECTS)
>$(AR) rcs $@ $^

$(VULKAN_OBJECTS): $(OBJDIR)/%.o: $(VULKAN_SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(COMMON_OBJECTS): $(OBJDIR)/%.o: $(SRCDIR)/%.cpp
>@mkdir -p $(OBJDIR)
>$(CXX) $(CXXFLAGS) -c $< -o $@

$(SPIRV_REFLECT_OBJECT): $(SPIRV_REFLECT_DIR)/spirv_reflect.c
>@mkdir -p $(OBJDIR)
>$(CC) $(CFLAGS) -c $< -o $@

clean:
>rm -rf $(OBJDIR) $(LIB)

-include $(OBJECTS:.o=.d)
