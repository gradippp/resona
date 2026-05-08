# Build stage
FROM alpine:latest AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    openssl-dev \
    mariadb-dev \
    linux-headers \
    pkgconf

WORKDIR /app

# Copy source
COPY . .

# Build
RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --config Release --target resona -j$(nproc)

# Collect all shared libraries required by the build
RUN mkdir /app/libs && find build -name "*.so*" -type f -exec cp {} /app/libs/ \;

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

EXPOSE 8080

CMD ["./resona"]
