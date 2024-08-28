CC=i686-w64-mingw32-gcc
#SANITIZE=-fsanitize=address
CFLAGS=`pkg-config --cflags vulkan` -g -Wall -Iinclude -fPIC $(SANITIZE)
LDFLAGS=-g $(SANITIZE)
DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

DEPDIR=.deps
OBJDIR=obj
SRCS=src/buffer.c src/draw.c src/drift.c src/frame.c src/fx.c src/mesh.c src/pipeline.c src/sst.c src/tex.c src/util.c src/vulkan.c

OBJS=$(SRCS:src/%.c=$(OBJDIR)/%.o)

all: glide2x.dll
#all: libglide2x.so dll/glide2x.dll.so

glide2x.dll: $(OBJS)

glide2x.dll: $(OBJS)
	$(CC) $(LDFLAGS) -shared -o $@ $^ drift.def $(LDLIBS)

dll/glide2x.dll.so: dll/glide2x.c dll/glide2x.spec
	winegcc -g -m32 -Iinclude -o $@ $^ -shared -L. -lglide2x -lX11

$(OBJDIR)/%.o: src/%.c
$(OBJDIR)/%.o: src/%.c $(DEPDIR)/%.d | $(DEPDIR) $(OBJDIR)
	$(CC) $(DEPFLAGS) $(CFLAGS) -c -o $@ $<

$(DEPDIR): ; @mkdir -p $@

$(OBJDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:src/%.c=$(DEPDIR)/%.d)

$(DEPFILES):

include $(wildcard $(DEPFILES))

.PHONY: clean

clean:
	rm -rf glide2x.dll $(OBJS) dll/glide2x.dll.so
