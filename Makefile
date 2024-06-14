# SPDX-License-Identifier: 0BSD


CC ?= cc
CXX ?= c++
PKG_CONFIG ?= pkg-config
VERSION ?= $(shell git describe)
DATE_EPOCH = date $(shell date -d @0 > /dev/null 2>&1 && echo "-d @" || echo "-r ")
SOURCE_DATE_EPOCH ?= $(shell git log -1 --no-show-signature --format=%at "archivemount.1.in")
MANUAL_DATE ?= $(shell $(DATE_EPOCH)$(SOURCE_DATE_EPOCH) +"%B %e, %Y")
PREFIX ?= /usr/local


ADD_L := $(shell for p in fuse libarchive; do $(PKG_CONFIG) --cflags $$p; done 2>/dev/null) -O3 -g -Wall -Wextra

CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -DVERSION='"$(VERSION)"' $(if $(DEBUG),,-DNDEBUG)
CFLAGS   += $(ADD_L)
#CXXFLAGS += $(ADD_L) -fno-exceptions -fno-rtti -std=c++2b  # c++23 isn't understood by everyone
LDLIBS   += $(shell for p in fuse libarchive; do $(PKG_CONFIG) --libs $$p || echo -l$${p#lib}; done 2>/dev/null)


.PHONY: all check clean install

all: archivemount archivemount.1
clean:
	rm -rf *.o archivemount archivemount.1


install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/share/man/man1
	cp archivemount $(DESTDIR)$(PREFIX)/bin
	cp archivemount.1 $(DESTDIR)$(PREFIX)/share/man/man1

archivemount.1: archivemount.1.in
	awk '{ gsub(/ \^/, " \\(ha"); gsub(/ ~/, " \\(ti"); if($$1 == ".Dd") $$2 = "$(MANUAL_DATE)"; if($$1 == ".Dt") print ".ds doc-volume-operating-system"; if($$1 == ".Os") $$2 = "urlview-ng $(VERSION)"; print}' < $< > $@

archivemount:   archivemount.o
archivemount.o: archivemount.c uthash.h archivecomp.h
