PROG = event_conveyor_monitor
PKGS = gio-unix-2.0 axevent axparameter fcgi jansson
CFLAGS += $(shell pkg-config --cflags $(PKGS)) -std=c11 -Wall -Wextra -Wformat=2 -Wstrict-prototypes -Werror
LDLIBS += $(shell pkg-config --libs $(PKGS))

.PHONY: all clean
all: $(PROG)
$(PROG): $(PROG).c conveyor.c conveyor.h status_server.c status_server.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@
clean:
	rm -f $(PROG) *.o *.eap *_LICENSE.txt package.conf param.conf
