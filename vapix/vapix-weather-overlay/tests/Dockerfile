FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc pkg-config libglib2.0-dev libjansson-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /test
COPY app/weather.c app/weather.h app/overlay_protocol.c app/overlay_protocol.h ./
COPY tests/weather_test.c ./
RUN gcc -std=c11 -Wall -Wextra -Werror -I. weather_test.c weather.c overlay_protocol.c $(pkg-config --cflags --libs glib-2.0 jansson) -lm -o weather-test && ./weather-test
