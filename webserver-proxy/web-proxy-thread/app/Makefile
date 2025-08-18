PROG1  = web_proxy_thread
SRCS1  = $(PROG1).c
OBJS1  = $(SRCS1:.c=.o)
PROGS  = $(PROG1)

# SDK sysroot (set by environment-setup, but we default it here too)
SYSROOT ?= /opt/axis/acapsdk/sysroots/aarch64

# Only pkg-config packages that actually have .pc files in the SDK
PKGS = glib-2.0 gio-2.0 jansson axparameter

# CivetWeb is provided without pkg-config; point these to where you staged it
CIVETWEB_PREFIX ?= /opt/build/civetweb

# Project-local lib dir (only used if present)
LIBDIR ?= lib

# ----- Flags from pkg-config (adds GLib/GIO include dirs so glib.h resolves) -----
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs   $(PKGS))

# ----- Axis SDK headers/libs that don’t need pkg-config -----
# axparameter headers live under axsdk
CFLAGS  += -I$(SYSROOT)/usr/include/axsdk
LDLIBS  += -laxparameter

# ----- CivetWeb: headers & libs -----
CFLAGS  += -I$(CIVETWEB_PREFIX)/include
LDFLAGS += -L$(CIVETWEB_PREFIX)/lib
LDLIBS  += -lcivetweb -lpthread
# If CivetWeb was built with HTTPS:
# LDLIBS += -lssl -lcrypto
# If static libcivetweb.a, you may also need:
# LDLIBS += -ldl -lm -lz

# ----- Only add project lib dir if it exists/is set -----
ifneq ($(strip $(LIBDIR)),)
LDFLAGS += -L$(LIBDIR) -Wl,--no-as-needed,-rpath,'$$ORIGIN/lib'
endif

# ----- Warnings / hardening -----
CFLAGS += -Wall \
          -Wextra \
          -Wformat=2 \
          -Wpointer-arith \
          -Wbad-function-cast \
          -Wstrict-prototypes \
          -Wmissing-prototypes \
          -Winline \
          -Wdisabled-optimization \
          -Wfloat-equal \
          -W \
          -Werror \
          -MMD -MP

all: $(PROGS)

# Build objects first (clearer errors and proper dep handling)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(PROG1): $(OBJS1)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -rf $(PROGS) *.o *.d *.eap* *_LICENSE.txt package.conf* param.conf tmp* $(LIBDIR)

# Auto-rebuild on header changes
-include $(OBJS1:.o=.d)
