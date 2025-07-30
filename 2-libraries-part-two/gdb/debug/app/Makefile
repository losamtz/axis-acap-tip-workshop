# Name of the final program
PROG := pulse_send_stless_debug

# Debug output directory
DEBUG_DIR := debug

# Source and object files
SRCS := $(PROG).c
OBJS := $(SRCS:.c=.o)

# Required pkg-config packages
PKGS := glib-2.0 axevent

# Compilation flags
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

CFLAGS += -W \
          -Wformat=2 \
          -Wpointer-arith \
          -Wbad-function-cast \
          -Wstrict-prototypes \
          -Wmissing-prototypes \
          -Winline \
          -Wdisabled-optimization \
          -Wfloat-equal \
          -Wall \
          -Werror

CFLAGS_DEBUG := -g3 -O0

# Default target
all: $(DEBUG_DIR)/$(PROG)

# Link step
$(DEBUG_DIR)/$(PROG): $(OBJS)
	@mkdir -p $(DEBUG_DIR)
	$(CC) $(CFLAGS) $(CFLAGS_DEBUG) -o $@ $^ $(LDLIBS)
	cp $@ .

# Compile .c to .o
# %.o: %.c
# 	$(CC) $(CFLAGS) $(CFLAGS_DEBUG) -c $< -o $@

# Clean everything
clean:
	rm -rf $(PROG) *.o *.eap* *_LICENSE.txt package.conf* param.conf tmp* $(DEBUG_DIR) core
