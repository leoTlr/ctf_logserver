# universal makefile from https://stackoverflow.com/a/28663974/9986282

appname := server

CXX := g++
CXXFLAGS := -Wall -Wextra
LDFLAGS :=
LDLIBS :=

srcext := cpp
srcfiles := $(shell find . -name "*.$(srcext)")
objects  := $(patsubst %.$(srcext), %.o, $(srcfiles))

all: $(appname)

$(appname): $(objects)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(appname) $(objects) $(LDLIBS)

depend: .depend

.depend: $(srcfiles)
	rm -f ./.depend
	$(CXX) $(CXXFLAGS) -MM $^>>./.depend;

clean:
	rm -f $(objects) $(appname)

#dist-clean: clean
#	rm -f *~ .depend

include .depend
