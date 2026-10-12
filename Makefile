# juku.nro 极简播放器 —— 交叉编译 Makefile (devkitA64)
# 在装有 devkitPro 的机器上: pacman -S switch-dev switch-libmpv switch-libcurl
# 然后在本目录: make
TARGET      := juku
BUILD       := build
SOURCES     := source/main.cpp
NRO         := $(TARGET).nro

PREFIX      := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
CC          := $(PREFIX)gcc
CXX         := $(PREFIX)g++
STRIP       := $(PREFIX)strip

# libnx 必备
ARCH        := -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS      := -g -Wall -O2 -ffunction-sections -fdata-sections \
               $(ARCH) -D__SWITCH__
CXXFLAGS    := $(CFLAGS)

# libcurl 为可选: 安装了 switch-libcurl 才开启 HTTP 拉取列表功能
CURL_AVAIL  := $(shell $(PKGCONFIG) --exists libcurl 2>/dev/null && echo yes)
ifeq ($(CURL_AVAIL),yes)
CFLAGS      += -DHAVE_CURL
CURL_LIBS   := -lcurl
endif

PORTLIBS    := $(DEVKITPRO)/portlibs/switch
LIBNX       := $(DEVKITPRO)/libnx
PKGCONFIG  := $(PORTLIBS)/bin/aarch64-none-elf-pkg-config
export PKG_CONFIG_PATH := $(PORTLIBS)/lib/pkgconfig

INCLUDES    := -I$(LIBNX)/include -I$(PORTLIBS)/include
LIBDIRS     := -L$(LIBNX)/lib -L$(PORTLIBS)/lib

# 优先用 pkg-config 取 mpv 全量静态依赖(含 ffmpeg 等), 失败则回退到 -lmpv
MPV_LIBS    := $(shell $(PKGCONFIG) --static --libs mpv 2>/dev/null)
ifeq ($(MPV_LIBS),)
MPV_LIBS    := -lmpv -lz
endif
# pkg-config 可能已带 -lnx/-fPIE, 这里去掉:
#  - -lnx 必须留到最后, 放中间会导致 _start/TLS 符号多重定义/未定义
#  - -fPIE 会令 ld 选 PIE 版 crt0, 与 libnx 自带 _start 冲突
MPV_LIBS    := $(subst -lnx,,$(MPV_LIBS))
MPV_LIBS    := $(subst -fPIE,,$(MPV_LIBS))
MPV_LIBS    := $(subst -pie,,$(MPV_LIBS))

# libnx 的 specs 提供正确的 crt0/链接脚本(__tls_start/_DYNAMIC/__got_start__ 等符号)
# 不同 devkitPro 版本路径不一, 自动查找
NX_SPECS_FILE := $(shell find $(LIBNX) $(DEVKITPRO)/devkitA64 -name libnx.specs 2>/dev/null | head -1)
ifeq ($(NX_SPECS_FILE),)
NX_SPECS :=
else
NX_SPECS := -specs=$(NX_SPECS_FILE)
endif

# mesa(EGL) 静态链接需要 nir/gallium 等内部库, pkg-config 未带全; 自动探测存在的 .a
MESA_LIBS   := $(shell for l in nir gallium mesa_gallium mesa_nir spirv compiler; do \
                      f=$(PORTLIBS)/lib/lib$$l.a; [ -f $$f ] && echo -l$$l; done)

# 用 --start-group 让 ld 循环解析静态库间相互引用
LIBS        := $(NX_SPECS) -Wl,--start-group $(MPV_LIBS) $(CURL_LIBS) $(MESA_LIBS) -lnx -Wl,--end-group

OBJS        := $(SOURCES:source/%.cpp=$(BUILD)/%.o)

all: $(NRO)
$(NRO): $(OBJS)
	$(CXX) $(OBJS) $(LIBDIRS) $(LIBS) -o $(TARGET).elf
	@$(DEVKITPRO)/tools/bin/elf2nro $(TARGET).elf $(NRO) --nro-dir $(BUILD)/nrodir >/dev/null 2>&1 || \
	 $(DEVKITPRO)/devkitA64/bin/elf2nro $(TARGET).elf $(NRO) --nro-dir $(BUILD)/nrodir
	@$(STRIP) $(TARGET).elf 2>/dev/null || true

$(BUILD)/%.o: source/%.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD) $(TARGET).elf $(NRO)

.PHONY: all clean
