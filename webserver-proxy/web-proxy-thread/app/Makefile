PROGS     = web_proxy_thread
SRCS      = $(wildcard *.c)
OBJS      = $(SRCS:.c=.o)

PKGS = glib-2.0 gio-2.0 axparameter jansson civetweb

CFLAGS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --cflags $(PKGS))
LDLIBS += $(shell PKG_CONFIG_PATH=$(PKG_CONFIG_PATH) pkg-config --libs $(PKGS))

LDLIBS += -lcivetweb
CFLAGS += -I/opt/build/civetweb/include

LDFLAGS += -L$(LIBDIR) -Wl,--no-as-needed,-rpath,'$$ORIGIN/lib'

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
          -Werror

all:	$(PROGS)

$(PROG1): $(OBJS1)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean:
	rm -rf $(PROGS) *.o *.eap* *_LICENSE.txt package.conf* param.conf tmp* $(LIBDIR)