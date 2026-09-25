PROG = vapix_weather_overlay
PKGS = gio-unix-2.0 axparameter libcurl jansson fcgi
CFLAGS += $(shell pkg-config --cflags $(PKGS)) -std=c11 -Wall -Wextra -Wformat=2 -Wstrict-prototypes -Werror
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
.PHONY: all clean
all: $(PROG)
$(PROG): main.c weather.c weather.h overlay_protocol.c overlay_protocol.h http.c http.h status_server.c status_server.h
	$(CC) $(CFLAGS) $(LDFLAGS) $(filter %.c,$^) $(LDLIBS) -o $@
clean:
	rm -f $(PROG) *.o *.eap *_LICENSE.txt package.conf param.conf
