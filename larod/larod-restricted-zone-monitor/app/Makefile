PROG = larod_restricted_zone
SOURCES = main.c input_preview.c views.c stream.c model.c model_preprocessing.c img_util.c channel_util.c \
          argparse.c panic.c detections.c zone_policy.c monitor.c overlay.c overlay_render.c event.c status_server.c
PKGS = axoverlay2 cairo gio-2.0 gio-unix-2.0 liblarod vdostream axparameter axevent fcgi jansson
CFLAGS += $(shell pkg-config --cflags $(PKGS)) -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Wformat=2 -Werror
LDLIBS += $(shell pkg-config --libs $(PKGS)) -lm
.PHONY: all clean
all: $(PROG)
$(PROG): $(SOURCES) $(wildcard *.h)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SOURCES) $(LDLIBS) -o $@
clean:
	rm -f $(PROG) *.o *.eap *_LICENSE.txt package.conf param.conf
