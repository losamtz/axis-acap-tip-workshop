FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc pkg-config libglib2.0-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /test
COPY app/zone.c app/zone.h ./
COPY tests/zone_test.c ./
RUN gcc -std=c11 -Wall -Wextra -Werror -I. zone_test.c zone.c $(pkg-config --cflags --libs glib-2.0) -lm -o zone-test && ./zone-test
