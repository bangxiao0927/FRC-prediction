# ---- Stage 1: build C++ CLI ----
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake git curl zip unzip tar pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install vcpkg
RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git /vcpkg \
    && /vcpkg/bootstrap-vcpkg.sh
ENV VCPKG_ROOT=/vcpkg

WORKDIR /app
COPY vcpkg.json CMakeLists.txt ./
COPY src/ ./src/

# Build the CLI binary
RUN cmake -B build -S . \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel "$(nproc)"

# ---- Stage 2: runtime ----
FROM python:3.12-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy CLI binary from builder
COPY --from=builder /app/build/frc_prediction /app/build/frc_prediction

# Python dependencies
COPY requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt gunicorn

# App code
COPY app.py config.example.json ./
COPY web/ ./web/
COPY tests/ ./tests/

# Default config (override TBA_AUTH_KEY via env)
RUN mkdir -p /app/data/cache /app/data/predictions

EXPOSE 8000
ENV PORT=8000

CMD ["gunicorn", "--bind", "0.0.0.0:8000", "--workers", "2", "--timeout", "120", "app:app"]
