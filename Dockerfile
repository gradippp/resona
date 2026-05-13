# Build stage
FROM alpine:latest AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    openssl-dev \
    mariadb-dev \
    linux-headers \
    pkgconf \
    mariadb-connector-c-dev

WORKDIR /app

# Copy source
COPY . .

# Build
RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DMARIADB_NO_DATATYPES=ON \
    -DCMAKE_C_FLAGS="-DMARIADB_NO_DATATYPES -D_GNU_SOURCE -DMC_SKIP_BOOL_TYPEDEF" \
    -DCMAKE_CXX_FLAGS="-DMARIADB_NO_DATATYPES"

# Patch ma_global.h to prevent bool redefinition
RUN sed -i 's/typedef char[[:space:]]\+bool;/\/* typedef char bool; *\//g' third_party/mariadb-connector-c/include/ma_global.h || true

# Build
RUN cmake --build build --config Release --target resona -j$(nproc)

# Collect all shared libraries required by the build
# We search deeper and follow symlinks
RUN mkdir /app/libs && \
    find build -name "*.so*" -type f -exec cp -v {} /app/libs/ \; && \
    find build -name "*.so*" -type l -exec cp -av {} /app/libs/ \;

# Runtime stage
FROM alpine:latest

RUN apk add --no-cache \
    ca-certificates \
    libstdc++ \
    libgcc \
    mariadb-connector-c \
    openssl

WORKDIR /app

# Copy binary and libraries
COPY --from=builder /app/build/resona /app/resona
COPY --from=builder /app/libs/ /usr/lib/

ENV HOSTNAME=0.0.0.0
ENV PORT=8080

EXPOSE 8080

CMD ["./resona"]
