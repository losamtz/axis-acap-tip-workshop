FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc pkg-config libglib2.0-dev libjansson-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /test
COPY app/policy.c app/policy.h ./
COPY tests/policy_test.c ./
RUN gcc -std=c11 -Wall -Wextra -Werror -I. policy_test.c policy.c $(pkg-config --cflags --libs glib-2.0 jansson) -o policy-test && ./policy-test
