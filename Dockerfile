# Wire in a container: a small Alpine image with a static `wire` binary and
# its standard library.
#
#   docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/wire adder.wire
#   docker run --rm -v "${PWD}:/work" ghcr.io/husseinabdulameer11/wire -o page.html adder.wire
#
# Files are read from and written to the folder you mount at /work; open the
# .html Wire writes in any browser.
#
# Building it yourself needs gram. Either hand Docker your gram folder:
#
#   docker build --build-context gram=../gram -t wire .
#
# or let it clone gram from git:
#
#   docker build --build-arg GRAM_REPO=https://github.com/Husseinabdulameer11/gram.git -t wire .

ARG ALPINE=alpine:3.20

# ---- gram, when no `--build-context gram=...` is given: clone it ------------
FROM ${ALPINE} AS gram-git
ARG GRAM_REPO=
ARG GRAM_REF=main
RUN if [ -z "$GRAM_REPO" ]; then \
        echo "wire needs gram: build with --build-context gram=path/to/gram"; \
        echo "or --build-arg GRAM_REPO=https://github.com/<owner>/gram.git"; \
        exit 1; \
    fi \
 && apk add --no-cache git \
 && git clone --depth 1 --branch "$GRAM_REF" "$GRAM_REPO" /gram

# A stage named `gram` holding just gram's files. `--build-context gram=...`
# replaces this whole stage with the given folder.
FROM scratch AS gram
COPY --from=gram-git /gram /

# ---- build a static wire and run its tests ---------------------------------
# Node is there so the simulator tests run too (WebAssembly vs JavaScript on
# every example, and the SAP-1 computing 28).
FROM ${ALPINE} AS build
RUN apk add --no-cache build-base nodejs
COPY --from=gram / /src/gram
COPY . /src/wire
WORKDIR /src/wire
ARG VERSION=dev
RUN make clean \
 && make test GRAM=/src/gram VERSION="$VERSION" LDFLAGS=-static \
 && strip build/wire

# ---- the image people run ---------------------------------------------------
# wire is in /usr/local/bin and finds its standard library in
# /usr/local/share/wire/lib, next to it.
FROM ${ALPINE}
LABEL org.opencontainers.image.title="wire" \
      org.opencontainers.image.description="Describe a digital circuit as text, get an interactive simulated circuit page in the browser" \
      org.opencontainers.image.licenses="MIT"
COPY --from=build /src/wire/build/wire /usr/local/bin/wire
COPY --from=build /src/wire/lib /usr/local/share/wire/lib
COPY --from=build /src/wire/examples /usr/local/share/wire/examples
WORKDIR /work
ENTRYPOINT ["wire"]
CMD ["--help"]
