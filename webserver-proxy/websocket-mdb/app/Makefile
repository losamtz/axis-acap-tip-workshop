# ---- Program ----
PROG  := websocket_mdb
SRCS  := $(wildcard *.c)
OBJS  := $(SRCS:.c=.o)

# ---- CivetWeb paths (match your Dockerfile) ----
# Header stays in /opt/build/civetweb/include
# Static lib is copied to ./lib by the Dockerfile
CIVETWEB_INC ?= /opt/build/civetweb/include
CIVETWEB_LIB ?= ./lib

# ---- SDK packages via pkg-config ----
PKGS := glib-2.0 jansson mdb

CFLAGS  += $(shell pkg-config --cflags $(PKGS)) \
           -I$(CIVETWEB_INC)

LDFLAGS += -L$(CIVETWEB_LIB)
LDLIBS  += $(shell pkg-config --libs $(PKGS)) \
           -lcivetweb -lpthread

# If you enabled TLS/zlib in CivetWeb, add:
# LDLIBS += -lssl -lcrypto -ldl -lz
#
# If you plan to use WebSockets, ensure CivetWeb was built with:
#   make lib COPT='-DUSE_WEBSOCKET'
# and compile with:
CFLAGS += -DUSE_WEBSOCKET

# ---- Warnings / hardening ----
CFLAGS += -Wall -Wextra -Wformat=2 -Werror -MMD -MP

# ---- Build rules ----
all: $(PROG)

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(PROG) *.o *.d *.eap* *_LICENSE.txt package.conf* param.conf tmp*

-include $(OBJS:.o=.d)