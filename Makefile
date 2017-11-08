
#include ../make.include

NPOOL_DIR = ./netpool/
XTUIL_DIR = ./xutil/

CPPFLAGS  =   -ggdb -DCORE_DUMP  -pthread -Wall
CPPFLAGS += -fno-strict-aliasing


CPPFLAGS += -I${NPOOL_DIR} -I${NPOOL_DIR}/src/ -I${NPOOL_DIR}/include -I${NPOOL_DIR}/src/outclass/ -I${NPOOL_DIR}/src/threadcomm/
CPPFLAGS += -I${XTUIL_DIR}/include

CPPFLAGS += -rdynamic -fPIC
LDFLAGS += -lpthread 

SOURCES = $(wildcard ${XTUIL_DIR}/src/*.cpp)
SOURCES += $(wildcard ${NPOOL_DIR}/src/*.cpp ${NPOOL_DIR}/src/outclass/*.cpp ${NPOOL_DIR}/src/threadcomm/*.cpp)
OBJECTS = $(patsubst %.cpp,%.o,$(SOURCES))
DEPENDS = $(patsubst %.cpp,%.d,$(SOURCES))
ASMFILE = $(patsubst %.cpp,%.s,$(SOURCES))

.PHONY: all clean

#target = libnetpool.so
target = libnetpool.a
all: $(target)

$(target): $(OBJECTS)	
#	g++ -shared -o $(target)  $(OBJECTS)  $(LDFLAGS)
	ar -crs $(target) $(OBJECTS)

clean:
	@rm -fr $(OBJECTS) $(DEPENDS) $(ASMFILE) $(target)
	@rm -fr *.d *.o *.s 


install:
	@cp $(target)  /usr/local/lib/
uninstall:
	@rm  /usr/local/lib/$(target)