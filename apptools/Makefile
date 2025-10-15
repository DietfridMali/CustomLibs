.RECIPEPREFIX := >
CXX := g++
AR  := ar

FILES := \
 arghandler \
 base_soundhandler \
 networkendpoint \
 networkmessage \
 stunclient \
 textfileloader \
 udp

SRCDIR := src
OBJDIR := obj
INCDIRS := ./include ../basetools/include ../rendertools/include ../../ExternalLibs/glew/include/GL /usr/include/SDL2
INCLUDES := $(addprefix -I,$(INCDIRS))

SOURCES := $(addprefix $(SRCDIR)/,$(addsuffix .cpp,$(FILES)))
OBJECTS := $(addprefix $(OBJDIR)/,$(addsuffix .o,$(FILES)))

DEFINES := -D_DEBUG -DUSE_STD=1 -DUSE_GLM=1
SANITIZE := -fsanitize=address,undefined
DBGFLAGS := -g3 -Og -fno-omit-frame-pointer $(SANITIZE) -D_GLIBCXX_ASSERTIONS
CXXFLAGS := -std=c++20 -O0 $(DEFINES) $(INCLUDES) -MMD -MP

LIB := libapptools.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJECTS)
> $(AR) rcs $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
> @mkdir -p $(OBJDIR)
> $(CXX) $(CXXFLAGS) $(DBGFLAGS) -c $< -o $@

rendertools.lib: $(LIB)
> cp $(LIB) $@

clean:
> rm -rf $(OBJDIR) $(LIB) libapptools.a
