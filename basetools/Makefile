.RECIPEPREFIX := >
# CXX := g++
CXX := clang++
AR  := ar

FILES := std_string glm_matrix

INCDIRS := ./include ../CustomLibs/apptools/include ../ExternalLibs/glew/include/GL include/GL /usr/include/SDL2

.PHONY: all clean DEBUG RELEASE

MODE ?= release

DEBUG:
> $(MAKE) MODE=debug --no-print-directory all

RELEASE:
> $(MAKE) MODE=release --no-print-directory all

SRCDIR := src
OBJDIR := obj

INCLUDES := $(addprefix -I,$(INCDIRS))

SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(Files)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

DEFINES := -D_DEBUG -DUSE_STD=1 -DUSE_GLM=1 -DCOMPILE_STRING -DCOMPILE_MATRIX
SANITIZEFLAGS := -fsanitize=address,undefined
OPTFLAGS := -O3
DBGFLAGS := -g3 -O0 -fno-omit-frame-pointer $(SANITIZEFLAGS) -D_GLIBCXX_ASSERTIONS

ifeq ($(MODE),debug)
  CXXFLAGS := -std=c++20 $(DBGFLAGS) $(DEFINES) -D_DEBUG $(INCLUDES) -MMD -MP
  LDFLAGS  := $(SANITIZEFLAGS)
else
  CXXFLAGS := -std=c++20 $(OPTFLAGS) $(DEFINES) -DNDEBUG $(INCLUDES) -MMD -MP
  LDFLAGS  :=
endif

LIB := libbasetools.a

all: $(LIB)

$(LIB): $(OBJECTS)
> $(AR) rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(CXXFLAGS) $(DBGFLAGS) -c $< -o $@

basetools.lib: $(LIB)
> cp $(LIB) $@

clean:
> rm -rf $(OBJDIR) $(LIB) libbasetools.a

-include $(OBJECTS:.o=.d)
