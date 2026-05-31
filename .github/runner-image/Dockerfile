FROM ghcr.io/actions/actions-runner:latest

# Pre-install the full omega build toolchain so CI jobs don't pay
# the apt-get update + install cost (~90-180s) on every run.
USER root

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-12 \
    g++-12 \
    clang-18 \
    clang-format-18 \
    clang-tidy-18 \
    libclang-rt-18-dev \
    cmake \
    ninja-build \
    libasound2-dev \
    gcovr \
    && rm -rf /var/lib/apt/lists/*

USER runner
