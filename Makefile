#---------------------------------------------------------------------------------
# Lakka Installer NX - Makefile
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR		?=	$(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Application metadata
#---------------------------------------------------------------------------------
APP_TITLE	:=	Lakka Installer NX
APP_AUTHOR	:=	Community
APP_VERSION	:=	1.0.0

#---------------------------------------------------------------------------------
# Build paths
#---------------------------------------------------------------------------------
TARGET		:=	lakka-install-nx
BUILD		:=	build
ROMFS		:=	romfs
BOREALIS	:=	lib/borealis/library

SOURCES		:=	source \
				source/util \
				source/views \
				$(BOREALIS)/lib \
				$(BOREALIS)/lib/extern/nanovg \
				lib/lzma

DATA		:=

INCLUDES	:=	include \
				$(BOREALIS)/include \
				$(BOREALIS)/include/borealis/extern \
				$(BOREALIS)/lib/extern/fmt/include \
				lib/lzma

#---------------------------------------------------------------------------------
# Compiler flags
#---------------------------------------------------------------------------------
ARCH		:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS		:=	-g -Wall -O2 -ffunction-sections \
				$(ARCH) $(DEFINES) \
				-D__SWITCH__ \
				-DFMT_HEADER_ONLY \
				-DBOREALIS_RESOURCES="\"romfs:/\""

CFLAGS		+=	$(INCLUDE)

CXXFLAGS	:=	$(CFLAGS) -std=gnu++17 -fno-rtti -fno-exceptions

ASFLAGS		:=	-g $(ARCH)

LDFLAGS		=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
				-Wl,-Map,$(notdir $*.map)

LIBS		:=	-lcurl -lmbedtls -lmbedx509 -lmbedcrypto \
				-lglfw3 -lEGL -lglapi -ldrm_nouveau \
				-lfreetype -lpng16 -lbz2 -lz \
				-lnx -lm

LIBDIRS		:=	$(PORTLIBS) $(LIBNX)

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

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifneq ($(strip $(ROMFS)),)
	export NROFLAGS	+=	--romfsdir=$(CURDIR)/$(ROMFS)
endif

ifeq ($(strip $(APP_ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else ifneq (,$(findstring icon.jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/icon.jpg
	endif
else
	export APP_ICON := $(TOPDIR)/$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS	+=	--nacp=$(CURDIR)/$(TARGET).nacp
endif

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

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
