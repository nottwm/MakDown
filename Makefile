# SPDX-License-Identifier: MIT
# Copyright (c) 2026 nottwm
# ============================================================================
#  MakDown - top-level Makefile  (kbuild-style: each directory contributes
#  its objects through its own Makefile fragment)
#
#     make unx        build for Linux / macOS / BSD    (links arch/unx)
#     make win        build for Windows (MSYS2/MinGW)  (links arch/win)
#     make deploy     build for Windows and copy the needed DLLs next to the exe
#     make run        build + launch
#     make debug      build + launch with --debug (compiler/date/arch banner)
#     make test       run the test-suite (python3 tests/run_tests.py; pass options via ARGS="--fast")
#     make clean      remove build/<target>
#     make mrproper   remove build/ entirely
#     make help
#     make V=1 ...    show the full compiler command lines (default: one "[ 42%] Compiling ..." line per step;
#                     errors, warnings and notes from the compiler are always shown)
#
#  Variables:  TARGET=unx|win  QT=Qt6|Qt5  CXX=...  CROSS=prefix-  PKG_CONFIG=...  MOC=/path/to/moc
#              MULTIMEDIA=auto|yes|no  audio preview via QtMultimedia (Qt6 only)
#              WEBENGINE=auto|yes|no   browser tab via QtWebEngine (auto = use it if pkg-config finds it)
#  See Documentation/building.md for per-OS dependency install commands.
# ============================================================================

# NOTE: no trailing comments on assignments - make would keep the spaces in the value.
TARGET ?= unx
QT     ?= Qt6
APP    := MakDown

# ---- toolchain selection ------------------------------------------------------
# TARGET=win on Linux/WSL needs the MinGW-w64 cross-compiler (it provides windows.h).
# On MSYS2 (native Windows) the normal g++ already targets Windows, so no prefix is used.
# Override with CROSS=prefix- , CXX=... or PKG_CONFIG=... if your toolchain is named differently.
HOST := $(shell uname -s)
ifeq ($(TARGET),win)
  ifeq ($(findstring MINGW,$(HOST))$(findstring MSYS,$(HOST)),)
    CROSS ?= x86_64-w64-mingw32-
  endif
endif
ifeq ($(origin CXX),default)
  CXX := $(CROSS)g++
endif
# A CXX inherited from the environment (e.g. CXX=clang++ in your shell profile) is a HOST compiler: with MinGW headers it dies on
# __declspec. When cross-compiling, use the MinGW compiler instead - unless CXX was given on the make command line.
ifeq ($(TARGET),win)
  ifneq ($(CROSS),)
    ifeq ($(filter command line override,$(origin CXX)),)
      ifeq ($(findstring mingw,$(CXX)),)
        $(info TARGET=win: ignoring CXX=$(CXX) from the environment, using $(CROSS)g++ (pass CXX=... on the make command line to override))
        CXX := $(CROSS)g++
      endif
    endif
  endif
endif
PKG_CONFIG ?= $(CROSS)pkg-config
WINDRES    ?= $(CROSS)windres

ifneq ($(filter $(TARGET),unx win),$(TARGET))
$(error TARGET must be "unx" or "win", got "$(TARGET)")
endif

BUILD := build/$(TARGET)

# ---- per-target flags ------------------------------------------------------
ifeq ($(TARGET),win)
  EXE     := $(APP).exe
  DEFS    := -DMAKDOWN_WIN -DMAKDOWN_TARGET=\"win\"
  # GUI subsystem: no console window (arch/win/platform.cpp re-attaches one for --debug); dwmapi: custom window frame
  LDEXTRA := -mwindows -ldwmapi
else
  EXE     := $(APP)
  DEFS    := -DMAKDOWN_UNX -DMAKDOWN_TARGET=\"unx\"
  LDEXTRA :=
endif

# ---- Qt discovery ------------------------------------------------------------
# (moc always comes from the HOST Qt, so its libexecdir is queried with the plain host pkg-config)
# ---- optional QtWebEngine (the browser tab) --------------------------------------
# auto (default): enabled when pkg-config finds <Qt>WebEngineWidgets.  yes: fail if missing.  no: never.
# Without it MakDown still builds; "open in browser tab" then hands the page to the system browser.
# ---- optional QtMultimedia (audio preview) ---------------------------------------
# auto (default): enabled for Qt6 when pkg-config finds Qt6Multimedia.  yes: fail if missing.  no: never.
# Without it images still preview; audio files are handed to the system player.
MULTIMEDIA ?= auto
MM_FOUND   := $(if $(filter-out no,$(MULTIMEDIA)),$(if $(filter Qt6,$(QT)),$(shell $(PKG_CONFIG) --exists $(QT)Multimedia >/dev/null 2>&1 && echo yes)))
ifeq ($(MULTIMEDIA),yes)
  ifeq ($(MM_FOUND),)
    $(error MULTIMEDIA=yes but Qt6Multimedia was not found by $(PKG_CONFIG) - see Documentation/building.md)
  endif
endif

WEBENGINE ?= auto
WE_FOUND  := $(if $(filter-out no,$(WEBENGINE)),$(shell $(PKG_CONFIG) --exists $(QT)WebEngineWidgets >/dev/null 2>&1 && echo yes))
ifeq ($(WEBENGINE),yes)
  ifeq ($(WE_FOUND),)
    $(error WEBENGINE=yes but $(QT)WebEngineWidgets was not found by $(PKG_CONFIG) - see Documentation/building.md)
  endif
endif

PKGS       := $(QT)Widgets
ifneq ($(WE_FOUND),)
  PKGS += $(QT)WebEngineWidgets
  DEFS += -DMAKDOWN_WEBENGINE
endif
ifneq ($(MM_FOUND),)
  PKGS += $(QT)Multimedia
  DEFS += -DMAKDOWN_MULTIMEDIA
endif
QT_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(PKGS))
QT_LIBS    := $(shell $(PKG_CONFIG) --libs   $(PKGS))
QT_LIBEXEC := $(shell pkg-config --variable=libexecdir $(QT)Core 2>/dev/null)
MOC ?= $(firstword $(wildcard $(QT_LIBEXEC)/moc) $(shell command -v moc-qt6 2>/dev/null) $(shell command -v moc 2>/dev/null))

CXXFLAGS ?= -O2 -Wall -Wextra
CXXFLAGS += -std=c++17 -fPIC -MMD -MP $(DEFS) $(QT_CFLAGS) -Iinclude

# ---- collect objects from every directory's Makefile fragment ---------------
#   obj-y : objects to link, e.g. core/theme.o
#   moc-y : headers (include/makdown/ui/<name>.h) containing Q_OBJECT
obj-y  :=
moc-y  :=
SUBDIRS := init core ui arch/$(TARGET)
include $(foreach d,$(SUBDIRS),$(d)/Makefile)

OBJS := $(addprefix $(BUILD)/,$(obj-y)) $(patsubst %,$(BUILD)/moc/moc_%.o,$(moc-y))

# header dependencies (-MMD) so editing a .h rebuilds everything that includes it
-include $(OBJS:.o=.d)

# ---- quiet output ----------------------------------------------------------------
# One progress line per step ("[ 42%] Compiling core/theme.cpp"); V=1 gives the raw command lines instead.
# Only the lines the tools print themselves (errors, warnings, notes) appear between the progress lines.
# The percentage is the step's position in the full build (moc, objects, link), like CMake's.
MOCCPP := $(patsubst %,$(BUILD)/moc/moc_%.cpp,$(moc-y))
STEPS  := $(MOCCPP) $(OBJS) $(BUILD)/$(EXE)
_n     :=
$(foreach s,$(STEPS),$(eval _n += x)$(eval STEP_$(s) := $(words $(_n))))
STEPS_TOTAL := $(words $(STEPS))
ifeq ($(V),1)
  Q :=
  define say
  endef
else
  Q := @
  # $(call say,Verb,target,shown-name)
  define say
  @pct=$$(( $(STEP_$(2)) * 100 / $(STEPS_TOTAL) )); \
  if [ -t 1 ]; then printf '\033[32m[%3d%%]\033[0m %s \033[1m%s\033[0m\n' $$pct '$(1)' '$(3)'; \
  else printf '[%3d%%] %s %s\n' $$pct '$(1)' '$(3)'; fi
  endef
endif

# ---- rules ---------------------------------------------------------------------
.PHONY: all unx win deploy run debug test clean mrproper help FORCE
.SECONDARY:
all: $(BUILD)/$(EXE)
unx:
	@$(MAKE) --no-print-directory all TARGET=unx
win:
	@$(MAKE) --no-print-directory all TARGET=win

$(BUILD)/$(EXE): $(OBJS)
	$(call say,Linking,$@,$@)
	$(Q)$(CXX) -o $@ $^ $(QT_LIBS) $(LDEXTRA)
	@echo "Built $@"

# build-configuration stamp: changing DEFS / flags (e.g. WEBENGINE=no) rebuilds every object
CFG := $(BUILD)/.cfg
$(CFG): FORCE
	@mkdir -p $(@D)
	@printf '%s\n' '$(DEFS) $(CXXFLAGS) $(QT_LIBS)' | cmp -s - $@ || printf '%s\n' '$(DEFS) $(CXXFLAGS) $(QT_LIBS)' > $@

$(BUILD)/%.o: %.cpp $(CFG)
	@mkdir -p $(@D)
	$(call say,Compiling,$@,$<)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

# Windows resources (exe icon): arch/win/makdown.rc -> COFF object, linked into MakDown.exe
$(BUILD)/%.rc.o: %.rc assets/icon.ico
	@mkdir -p $(@D)
	$(call say,Resource,$@,$<)
	$(Q)$(WINDRES) -O coff -i $< -o $@

# moc: Qt meta-object code for every header listed in moc-y (all live in include/makdown/ui/)
$(BUILD)/moc/moc_%.cpp: include/makdown/ui/%.h
	@mkdir -p $(@D)
	$(call say,Generating,$@,$@)
	$(Q)$(MOC) -Iinclude $< -o $@
$(BUILD)/moc/moc_%.o: $(BUILD)/moc/moc_%.cpp $(CFG)
	$(call say,Compiling,$@,$<)
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

# copy all needed DLLs + Qt platform plugin next to MakDown.exe (Windows builds)
deploy:
	@$(MAKE) --no-print-directory all TARGET=win
	sh scripts/deploy-win.sh build/win/MakDown.exe

run: all
	./$(BUILD)/$(EXE)
debug: all
	./$(BUILD)/$(EXE) --debug
test:
	python3 tests/run_tests.py $(ARGS)
clean:
	$(Q)rm -rf $(BUILD)
mrproper:
	$(Q)rm -rf build
help:
	@sed -n '4,18p' Makefile
