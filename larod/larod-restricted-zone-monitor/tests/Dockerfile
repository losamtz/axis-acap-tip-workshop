FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc nodejs clang-format npm && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y --no-install-recommends libc6-dev libglib2.0-dev libcairo2-dev pkg-config && rm -rf /var/lib/apt/lists/*
WORKDIR /work
COPY app/zone_policy.c app/zone_policy.h app/detections.c app/detections.h ./app/
COPY app/input_preview.c app/input_preview.h ./app/
COPY app/html ./app/html
COPY tests ./tests
RUN gcc -std=c11 -Wall -Wextra -Werror -I app tests/test_policy.c app/zone_policy.c app/detections.c -lm -o /tmp/zone-tests && /tmp/zone-tests && node --test tests/*.test.cjs

RUN gcc -std=c11 -Wall -Wextra -Werror -I tests/stubs -I app tests/input_preview_test.c app/input_preview.c $(pkg-config --cflags --libs glib-2.0) -o /tmp/preview-tests && /tmp/preview-tests

COPY app/overlay_render.c app/overlay_render.h ./app/
RUN gcc -std=c11 -Wall -Wextra -Werror -I app tests/overlay_render_test.c app/overlay_render.c $(pkg-config --cflags --libs cairo) -lm -o /tmp/overlay-tests && /tmp/overlay-tests
