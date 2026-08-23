FROM ubuntu:26.04

ARG USER_ID=1000
ARG GROUP_ID=1000

RUN apt-get update && apt-get install -y \
    git \
    curl \
    ca-certificates \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04+ base images ship a pre-existing uid/gid-1000 "ubuntu" user
# (like node:*-slim's "node"); rename it to match host naming/permissions.
RUN usermod -l user ubuntu && \
    groupmod -n user ubuntu && \
    usermod -d /home/user -m user

# Passwordless sudo: this is a disposable sandbox already run with
# --dangerously-skip-permissions, so gating sudo behind a password adds no safety.
RUN echo "user ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/user && \
    chmod 0440 /etc/sudoers.d/user

# Ubuntu's apt repos lag on Node.js versions, so install via NodeSource instead.
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - && \
    apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

RUN npm install -g @anthropic-ai/claude-code

USER user
# The directory is handled dynamically by the orchestration script
WORKDIR /home/user/workspace

CMD ["claude"]
