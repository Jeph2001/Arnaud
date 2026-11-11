# Pharmacy Management System - build file
# Works on Linux and on Windows inside an MSYS2 UCRT64 / MINGW64 shell.

CC      ?= gcc
PKG     ?= pkg-config
PKGS     = gtk+-3.0 sqlite3
TARGET   = pharmacy
SRC      = $(wildcard src/*.c)
OBJ      = $(SRC:src/%.c=build/%.o)

CFLAGS  += -std=gnu11 -Wall -Wextra -O2 $(shell $(PKG) --cflags $(PKGS))
LDLIBS  += $(shell $(PKG) --libs $(PKGS))

ifeq ($(OS),Windows_NT)
  TARGET  := $(TARGET).exe
  LDFLAGS += -mwindows          # GUI app: no console window
endif

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

run: $(TARGET)
	./$(TARGET)

# Headless tests of the business rules (models + store + database, no GUI)
test: build/test_store
	./build/test_store

build/test_store: tests/test_store.c src/store.c src/database.c src/models.c src/*.h | build
	$(CC) -std=gnu11 -Wall -Wextra -g $(shell $(PKG) --cflags glib-2.0 sqlite3) -o $@ \
	    tests/test_store.c src/store.c src/database.c src/models.c $(shell $(PKG) --libs glib-2.0 sqlite3)

clean:
	rm -rf build $(TARGET) pharmacy.exe dist

# Windows only: copy the .exe and every DLL/resource it needs into dist/
dist: $(TARGET)
	sh tools/package-windows.sh

.PHONY: all run test clean dist
