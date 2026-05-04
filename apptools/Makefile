.RECIPEPREFIX := >
ifeq ($(strip $(CXX)),)
  CXX := g++
endif
#CXX := clang++
AR  := ar

FILES := \
 arghandler \
 base_soundhandler \
 filelist \
 internetservices \
 networkendpoint \
 networkmessage \
 ntpclient \
 platformhandler \
 textfileloader \
 udp

CUSTOM_LIBS_DIR = ../CustomLibs/

EXTERNAL_LIBS_DIR = ../ExternalLibs/

STEAM_SDK = $(EXTERNAL_LIBS_DIR)steamworks_sdk_164/sdk

STEAM_INCLUDE = $(STEAM_SDK)/public/steam

ifeq ($(UNAME),Darwin)
    SYS_INC_DIRS := /opt/homebrew/include /opt/homebrew/include/gl /opt/homebrew/include/SDL2 
else
    SYS_INC_DIRS := /usr/include/ /usr/include/GL /usr/include/SDL2
endif

INCDIRS := \
	./include ../basetools/include ../rendertools/include \
    $(STEAM_INCLUDE) \
    $(SYS_INC_DIRS)

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

DEFINES := -DUSE_STD=1 -DUSE_GLM=1 -DLINUX -DCOMPILE_STRING -DCOMPILE_MATRIX -DFMT_UNICODE=0
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

LIB := libapptools$(LIB_SUFFIX).a

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
