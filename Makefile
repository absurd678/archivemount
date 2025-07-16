# SPDX-License-Identifier: 0BSD СТАРЫЙ ЕГО НАДО ЗАНОВО СДЕЛАТЬ

CC ?= cc
CXX ?= c++
PKG_CONFIG ?= pkg-config
FUSES ?= fuse3 fuse
VERSION ?= $(shell git describe)
PREFIX ?= /usr/local

# Определение целевого бинарного файла
TARGET = AuroraService

# Флаги для libarchive и FUSE
ADD_L := $(shell $(PKG_CONFIG) --cflags libarchive 2>/dev/null) \
         $(shell for p in $(FUSES); do $(PKG_CONFIG) --cflags $$p && exit; done 2>/dev/null) \
         -O3 -g -Wall -Wextra

# Флаги препроцессора и компилятора
CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D__STDC_FORMAT_MACROS -DVERSION='"$(VERSION)"' $(if $(DEBUG),,-DNDEBUG)
CXXFLAGS += $(ADD_L) -fno-exceptions -fno-rtti -Wno-missing-field-initializers -std=c++2b

# Флаги линковщика
LDLIBS += $(shell $(PKG_CONFIG) --libs libarchive 2>/dev/null || echo -larchive) \
          $(shell for p in $(FUSES); do $(PKG_CONFIG) --libs $$p && exit; done 2>/dev/null; echo -l$(firstword $(FUSES)))

# Исходные файлы
SRCS = AuroraService.cpp
OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.cpp archivemount.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(TARGET) $(OBJS)

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin