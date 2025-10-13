.RECIPEPREFIX := >
CXX := g++
AR  := ar
FILES := std_string glm_vector glm_matrix
SRCDIR := src
INCDIR := include
OBJDIR := obj
LIB := libbasetools.a

CXXFLAGS := -std=c++20 -O3 -DNDEBUG -DUSE_STD=1 -DUSE_GLM=1 -I$(INCDIR) -DCOMPILE_STRING -DCOMPILE_VECTOR -DCOMPILE_MATRIX -I../../ExternalLibs/glm

SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(Files)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJECTS)
> $(AR) rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(CXXFLAGS) -c $< -o $@

basetools.lib: $(LIB)
> cp $(LIB) $@

clean:
> rm -rf $(OBJDIR) $(LIB) basetools.lib
