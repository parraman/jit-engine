.PHONY: clean

CXXFLAGS+= -I/usr/lib/llvm-9/include -std=c++14 -fno-exceptions -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -g
CXXFLAGS+= -I../jit
LDFLAGS+=$(shell llvm-config-9 --ldflags)
LIBS:=$(shell llvm-config-9 --libs)

all: simple coro

simple: simple.o JitEngine.o JitOptimizer.o
	g++ $(CXXFLAGS) -o simple simple.o JitEngine.o JitOptimizer.o $(LDFLAGS) $(LIBS)

coro: coro.o JitEngine.o JitOptimizer.o
	g++ $(CXXFLAGS) -o coro coro.o JitEngine.o JitOptimizer.o $(LDFLAGS) $(LIBS)

JitEngine.o: ../jit/JitEngine.cpp ../jit/JitEngine.h
	g++ $(CXXFLAGS) -c -o JitEngine.o ../jit/JitEngine.cpp

JitOptimizer.o: ../jit/JitOptimizer.cpp ../jit/JitOptimizer.h
	g++ $(CXXFLAGS) -c -o JitOptimizer.o ../jit/JitOptimizer.cpp

clean:
	rm *.o simple coro
