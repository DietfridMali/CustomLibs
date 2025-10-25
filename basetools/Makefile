.RECIPEPREFIX := >
# CXX := g++
CXX := clang++
AR  := ar

FILES := std_string glm_matrix

INCDIRS := ./include ../CustomLibs/apptools/include ../ExternalLibs/glew/include/GL include/GL /usr/include/SDL2

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

DEFINES := -DUSE_STD=1 -DUSE_GLM=1 -DCOMPILE_STRING -DCOMPILE_MATRIX
SANITIZEFLAGS := -fsanitize=address,undefined
OPTFLAGS := -O3
DBGFLAGS := -g3 -O0 -fno-omit-frame-pointer $(SANITIZEFLAGS) -D_GLIBCXX_ASSERTIONS

ifeq ($(TARGET),DEBUG)
  CXXFLAGS := -std=c++20 $(DBGFLAGS) $(DEFINES) -D_DEBUG $(INCLUDES) -MMD -MP
  OBJDIR := obj/debug
  LIB_SUFFIX := d
else ifeq ($(TARGET),RELEASE)
  CXXFLAGS := -std=c++20 $(OPTFLAGS) $(DEFINES) -DNDEBUG $(INCLUDES) -MMD -MP
  OBJDIR := obj/release
  LIB_SUFFIX :=
else
  $(error TARGET muss DEBUG oder RELEASE sein)
endif

SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(FILES)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

LIB := libbasetools$(LIB_SUFFIX).a

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
