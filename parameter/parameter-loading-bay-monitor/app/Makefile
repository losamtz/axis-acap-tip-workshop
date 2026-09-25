PROG = parameter_loading_bay
PKGS = gio-2.0 gio-unix-2.0 axparameter fcgi jansson

CFLAGS += $(shell pkg-config --cflags $(PKGS))
CFLAGS += -std=c11 -Wall -Wextra -Wformat=2 -Wstrict-prototypes -Werror
LDLIBS += $(shell pkg-config --libs $(PKGS))

.PHONY: all clean
all: $(PROG)

$(PROG): $(PROG).c status_server.c status_server.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@

clean:
	rm -f $(PROG) *.o *.eap *_LICENSE.txt package.conf param.conf
