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
# 容器内实际文件名为 /opt/devkitpro/libnx/switch.specs
NX_SPECS_FILE := $(LIBNX)/switch.specs
ifeq ($(wildcard $(NX_SPECS_FILE)),)
NX_SPECS :=
else
NX_SPECS := -specs=$(NX_SPECS_FILE)
endif

# mesa(EGL) 静态链接需要 nir/gallium/spirv 等内部库, pkg-config 未带全; 用 find 自动定位真实文件名
MESA_LIBS   := $(shell find $(PORTLIBS)/lib -type f \( -name 'libnir*.a' -o -name 'libgallium*.a' -o -name 'libspirv*.a' -o -name 'libmesa*.a' \) 2>/dev/null | sed 's,.*/lib\(.*\)\.a,-\1,')

# 用 --start-group 让 ld 循环解析静态库间相互引用
LIBS        := $(NX_SPECS) -Wl,--start-group $(MPV_LIBS) $(CURL_LIBS) $(MESA_LIBS) -lnx -Wl,--end-group

OBJS        := $(SOURCES:source/%.cpp=$(BUILD)/%.o)

# --- 诊断(编译失败时在日志里能看到真实路径/库名) ---
$(info NX_SPECS=[$(NX_SPECS)])
$(info MESA_LIBS=[$(MESA_LIBS)])
$(info $(shell find /opt/devkitpro/portlibs/switch/lib -name 'lib*.a' 2>/dev/null | grep -iE 'nir|gallium|spirv|mesa' | sed 's,.*/,,;s/^/DIAG_MESA_A: /'))

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
