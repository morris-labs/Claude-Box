FROM ubuntu:26.04

ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=user

RUN apt-get update && apt-get install -y \
    git \
    curl \
    ca-certificates \
    sudo \
    tmux \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04+ base images ship a pre-existing uid/gid-1000 "ubuntu" user
# (like node:*-slim's "node"); rename it to match host naming/permissions.
RUN usermod -l ${USER_NAME} ubuntu && \
    groupmod -n ${USER_NAME} ubuntu && \
    usermod -d /home/${USER_NAME} -m ${USER_NAME}

# Passwordless sudo: this is a disposable sandbox already run with
# --dangerously-skip-permissions, so gating sudo behind a password adds no safety.
RUN echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/${USER_NAME} && \
    chmod 0440 /etc/sudoers.d/${USER_NAME}

# Ubuntu's apt repos lag on Node.js versions, so install via NodeSource instead.
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - && \
    apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

RUN npm install -g @anthropic-ai/claude-code

USER ${USER_NAME}
# The directory is handled dynamically by the orchestration script
WORKDIR /home/${USER_NAME}/workspace

CMD ["claude"]
