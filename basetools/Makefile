.RECIPEPREFIX := >
CXX := g++
AR  := ar

SRCDIR := src
OBJDIR := obj

FILES := std_string glm_matrix

INCDIRS := ./include ../CustomLibs/apptools/include ../ExternalLibs/glew/include/GL include/GL /usr/include/SDL2
INCLUDES := $(addprefix -I,$(INCDIRS))

SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(Files)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

DEFINES := -D_DEBUG -DUSE_STD=1 -DUSE_GLM=1 -DCOMPILE_STRING -DCOMPILE_MATRIX
SANITIZE := -fsanitize=address,undefined
DBGFLAGS := -g3 -Og -fno-omit-frame-pointer $(SANITIZE) -D_GLIBCXX_ASSERTIONS
LDFLAGS := -fsanitize=address,undefined
CXXFLAGS := -std=c++20 -O3 $(DEFINES) $(INCLUDES)

LIB := libbasetools.a

.PHONY: all clean

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
