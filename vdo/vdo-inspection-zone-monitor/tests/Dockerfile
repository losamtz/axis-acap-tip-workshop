FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc pkg-config libglib2.0-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /test
COPY app/inspection.c app/inspection.h ./
COPY tests/inspection_test.c ./
RUN gcc -std=c11 -Wall -Wextra -Werror -I. inspection_test.c inspection.c $(pkg-config --cflags --libs glib-2.0) -o inspection-test && ./inspection-test
