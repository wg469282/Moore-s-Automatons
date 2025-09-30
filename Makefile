# ============================================================================
# Makefile for Moore Automata Library (libma)
# ============================================================================

# Project metadata
PROJECT = libma
VERSION = 1.0.0
DESCRIPTION = "Moore finite state automata simulation library"

# Compiler and tools
CC = gcc
AR = ar
INSTALL = install
PKG_CONFIG = pkg-config

# Directories
SRCDIR = src
INCDIR = include
LIBDIR = lib
BUILDDIR = build
TESTDIR = tests
DOCDIR = docs

# Installation directories (respects PREFIX)
PREFIX ?= /usr/local
DESTDIR ?=
INSTALL_LIBDIR = $(DESTDIR)$(PREFIX)/lib
INSTALL_INCDIR = $(DESTDIR)$(PREFIX)/include
INSTALL_PKGCONFIGDIR = $(DESTDIR)$(PREFIX)/lib/pkgconfig

# Compiler flags for different build types
COMMON_CFLAGS = -Wall -Wextra -Wno-implicit-fallthrough -std=gnu17
DEBUG_CFLAGS = $(COMMON_CFLAGS) -g -O0 -DDEBUG -fsanitize=address
RELEASE_CFLAGS = $(COMMON_CFLAGS) -O2 -DNDEBUG -fPIC
LDFLAGS_SHARED = -shared
LDFLAGS_DEBUG = -fsanitize=address

# Build type (default: release)
BUILD_TYPE ?= release
ifeq ($(BUILD_TYPE),debug)
    CFLAGS = $(DEBUG_CFLAGS)
    LDFLAGS = $(LDFLAGS_DEBUG)
else
    CFLAGS = $(RELEASE_CFLAGS)
    LDFLAGS = $(LDFLAGS_SHARED)
endif

# Source files and targets
SRCS = ma.c
HEADERS = ma.h
OBJS = $(SRCS:.c=.o)
TARGET_SHARED = libma.so.$(VERSION)
TARGET_SHARED_LINK = libma.so
TARGET_STATIC = libma.a

# Default target
all: shared

# Build targets
shared: $(TARGET_SHARED)
static: $(TARGET_STATIC)  
both: shared static

# Shared library
$(TARGET_SHARED): $(OBJS)
	$(CC) $(LDFLAGS) -Wl,-soname,$(TARGET_SHARED_LINK).1 -o $@ $^
	ln -sf $(TARGET_SHARED) $(TARGET_SHARED_LINK)
	@echo "✅ Shared library $(TARGET_SHARED) built successfully"

# Static library
$(TARGET_STATIC): $(OBJS)
	$(AR) rcs $@ $^
	@echo "✅ Static library $(TARGET_STATIC) built successfully"

# Object files with header dependency
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build
debug:
	$(MAKE) BUILD_TYPE=debug

# Testing
test: shared
	@if [ -d "$(TESTDIR)" ]; then \
		echo "🧪 Running tests..."; \
		$(MAKE) -C $(TESTDIR) || exit 1; \
	else \
		echo "⚠️  No tests directory found"; \
	fi

# Installation
install: shared
	@echo "📦 Installing $(PROJECT)..."
	$(INSTALL) -d $(INSTALL_LIBDIR) $(INSTALL_INCDIR) $(INSTALL_PKGCONFIGDIR)
	$(INSTALL) -m 755 $(TARGET_SHARED) $(INSTALL_LIBDIR)/
	ln -sf $(TARGET_SHARED) $(INSTALL_LIBDIR)/$(TARGET_SHARED_LINK)
	$(INSTALL) -m 644 $(HEADERS) $(INSTALL_INCDIR)/
	@echo "prefix=$(PREFIX)" > $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "exec_prefix=\$${prefix}" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "libdir=\$${exec_prefix}/lib" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "includedir=\$${prefix}/include" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "Name: $(PROJECT)" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "Description: $(DESCRIPTION)" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "Version: $(VERSION)" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "Libs: -L\$${libdir} -lma" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	@echo "Cflags: -I\$${includedir}" >> $(INSTALL_PKGCONFIGDIR)/libma.pc
	ldconfig 2>/dev/null || true
	@echo "✅ Installation completed"

# Uninstallation  
uninstall:
	@echo "🗑️  Uninstalling $(PROJECT)..."
	rm -f $(INSTALL_LIBDIR)/$(TARGET_SHARED)
	rm -f $(INSTALL_LIBDIR)/$(TARGET_SHARED_LINK)
	rm -f $(INSTALL_INCDIR)/ma.h
	rm -f $(INSTALL_PKGCONFIGDIR)/libma.pc
	ldconfig 2>/dev/null || true
	@echo "✅ Uninstallation completed"

# Cleaning
clean:
	rm -f $(OBJS) $(TARGET_SHARED) $(TARGET_SHARED_LINK) $(TARGET_STATIC)
	@echo "🧹 Build artifacts cleaned"

distclean: clean
	rm -rf $(BUILDDIR) *.tmp *~ *.log
	@echo "🧹 Distribution files cleaned"

# Code quality checks
check-syntax:
	$(CC) $(CFLAGS) -fsyntax-only $(SRCS)
	@echo "✅ Syntax check passed"

check-format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "🔍 Checking code formatting..."; \
		clang-format --dry-run --Werror $(SRCS) $(HEADERS) || exit 1; \
		echo "✅ Code formatting is correct"; \
	else \
		echo "⚠️  clang-format not found, skipping format check"; \
	fi

# Documentation
docs:
	@if command -v doxygen >/dev/null 2>&1; then \
		echo "📚 Generating documentation..."; \
		doxygen Doxyfile || exit 1; \
		echo "✅ Documentation generated in $(DOCDIR)/"; \
	else \
		echo "⚠️  Doxygen not found, please install it to generate docs"; \
	fi

# Package creation
package: distclean
	@echo "📦 Creating source package..."
	tar -czf $(PROJECT)-$(VERSION).tar.gz \
		--exclude='.git*' --exclude='*.tar.gz' --exclude='$(BUILDDIR)' \
		--transform 's,^,$(PROJECT)-$(VERSION)/,' \
		*
	@echo "✅ Package $(PROJECT)-$(VERSION).tar.gz created"

# Development tools
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		echo "🎨 Formatting code..."; \
		clang-format -i $(SRCS) $(HEADERS); \
		echo "✅ Code formatted"; \
	else \
		echo "⚠️  clang-format not found"; \
	fi

# Information and help
info:
	@echo "📋 Project Information:"
	@echo "   Name: $(PROJECT)"
	@echo "   Version: $(VERSION)"
	@echo "   Description: $(DESCRIPTION)"
	@echo "   Build type: $(BUILD_TYPE)"
	@echo "   Compiler: $(CC)"
	@echo "   Flags: $(CFLAGS)"
	@echo "   Install prefix: $(PREFIX)"

help:
	@echo "📖 Available targets:"
	@echo "   all          - Build shared library (default)"
	@echo "   shared       - Build shared library (.so)"
	@echo "   static       - Build static library (.a)"
	@echo "   both         - Build both shared and static"
	@echo "   debug        - Build debug version"
	@echo "   test         - Run tests"
	@echo "   install      - Install library system-wide"
	@echo "   uninstall    - Remove installed files"
	@echo "   clean        - Remove build artifacts"
	@echo "   distclean    - Deep clean including temp files"
	@echo "   check-syntax - Check code syntax"
	@echo "   check-format - Check code formatting"
	@echo "   docs         - Generate documentation"
	@echo "   format       - Format source code"
	@echo "   package      - Create source package"
	@echo "   info         - Show project information"
	@echo "   help         - Show this help"
	@echo ""
	@echo "📋 Build options:"
	@echo "   BUILD_TYPE=debug    - Build with debug symbols"
	@echo "   PREFIX=/path        - Set installation prefix"

# CI/CD targets
ci-build: both test check-syntax

ci-test: test

# Phony targets
.PHONY: all shared static both debug test install uninstall clean distclean \
        check-syntax check-format docs format package info help ci-build ci-test

# Include dependency tracking
-include $(SRCS:.c=.d)
