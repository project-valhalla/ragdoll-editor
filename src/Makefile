CXXOPTFLAGS= -O3 -fomit-frame-pointer
INCLUDES= -I. -I/usr/X11R6/include `sdl-config --cflags`
CXXFLAGS= -Wall -fsigned-char $(CXXOPTFLAGS) $(INCLUDES)

LIBS= -L/usr/X11R6/lib `sdl-config --libs` -lGL
OBJS= \
	command.o \
	console.o \
	main.o \
	font.o \
	texture.o

default: all

all: ragdoll

clean:
	-$(RM) $(OBJS) ragdoll

ragdoll:	$(OBJS)
	$(CXX) $(CXXFLAGS) -o ragdoll $(OBJS) $(LIBS)

