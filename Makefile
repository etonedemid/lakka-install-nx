#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Application metadata
#---------------------------------------------------------------------------------
APP_TITLE	:=	Lakka Installer NX
APP_AUTHOR	:=	Community
APP_VERSION	:=	1.0.0

#---------------------------------------------------------------------------------
# Build configuration
#---------------------------------------------------------------------------------
TARGET		:=	lakka-install-nx
BUILD		:=	build
ROMFS		:=	romfs
BOREALIS_PATH	:=	lib/borealis

SOURCES		:=	source \
				source/util \
				source/views \
				lib/lzma

DATA		:=
INCLUDES	:=	include \
				lib/lzma

ICON		:=	icon.jpg

OUT_SHADERS	:=	shaders

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS	:=	-g -Wall -O2 -ffunction-sections \
			$(ARCH) $(DEFINES) \
			-DBOREALIS_RESOURCES="\"romfs:/\""

CFLAGS	+=	$(INCLUDE) -D__SWITCH__

CXXFLAGS	:= $(CFLAGS) -std=c++1z -O2 -Wno-volatile

ASFLAGS	:=	-g $(ARCH)

LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
			-Wl,-Map,$(notdir $*.map)

LIBS	:=	-lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -lnx

LIBDIRS	:=	$(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# Pull in borealis (adds its own SOURCES, INCLUDES, LIBS, CXXFLAGS)
#---------------------------------------------------------------------------------
include $(TOPDIR)/$(BOREALIS_PATH)/library/borealis.mk

#---------------------------------------------------------------------------------
# Build rules
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
GLSLFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.glsl)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

ifneq ($(strip $(ROMFS)),)
	ROMFS_TARGETS :=
	ROMFS_FOLDERS :=
	ifneq ($(strip $(OUT_SHADERS)),)
		ROMFS_SHADERS := $(ROMFS)/$(OUT_SHADERS)
		ROMFS_TARGETS += $(patsubst %.glsl, $(ROMFS_SHADERS)/%.dksh, $(GLSLFILES))
		ROMFS_FOLDERS += $(ROMFS_SHADERS)
	endif
	export ROMFS_DEPS := $(foreach file,$(ROMFS_TARGETS),$(CURDIR)/$(file))
endif

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean icon

all: icon $(ROMFS_TARGETS) | $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

ifneq ($(strip $(ROMFS_TARGETS)),)

$(ROMFS_TARGETS): | $(ROMFS_FOLDERS)

$(ROMFS_FOLDERS):
	@mkdir -p $@

$(ROMFS_SHADERS)/%_vsh.dksh: %_vsh.glsl
	@echo {vert} $(notdir $<)
	@uam -s vert -o $@ $<

$(ROMFS_SHADERS)/%_tcsh.dksh: %_tcsh.glsl
	@echo {tess_ctrl} $(notdir $<)
	@uam -s tess_ctrl -o $@ $<

$(ROMFS_SHADERS)/%_tesh.dksh: %_tesh.glsl
	@echo {tess_eval} $(notdir $<)
	@uam -s tess_eval -o $@ $<

$(ROMFS_SHADERS)/%_gsh.dksh: %_gsh.glsl
	@echo {geom} $(notdir $<)
	@uam -s geom -o $@ $<

$(ROMFS_SHADERS)/%_fsh.dksh: %_fsh.glsl
	@echo {frag} $(notdir $<)
	@uam -s frag -o $@ $<

$(ROMFS_SHADERS)/%.dksh: %.glsl
	@echo {comp} $(notdir $<)
	@uam -s comp -o $@ $<

endif

# Convert icon.png to icon.jpg (NRO requires JPEG, 256x256)
icon: icon.png
	@if [ ! -f icon.jpg ] || [ icon.png -nt icon.jpg ]; then \
		echo "Converting icon.png -> icon.jpg ..."; \
		convert icon.png -resize 256x256 icon.jpg 2>/dev/null || \
		ffmpeg -y -loglevel quiet -i icon.png -vf scale=256:256 icon.jpg 2>/dev/null || \
		python3 -c "from PIL import Image; Image.open('icon.png').resize((256,256)).convert('RGB').save('icon.jpg')" 2>/dev/null || \
		echo "[WARN] Could not convert icon.png to icon.jpg - install ImageMagick, ffmpeg, or Pillow"; \
	fi

clean:
	@echo Cleaning...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS	:=	$(OFILES:.o=.d)

all: $(OUTPUT).nro

$(OUTPUT).nro:	$(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf:	$(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
