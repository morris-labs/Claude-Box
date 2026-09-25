FROM ubuntu:26.04

ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=user

# Install curl first so the GitHub CLI apt repo block below can run.
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI apt repo (curl is now available).
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
      | dd of=/usr/share/keyrings/githubcli-archive-keyring.gpg && \
    chmod go+r /usr/share/keyrings/githubcli-archive-keyring.gpg && \
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/githubcli-archive-keyring.gpg] \
https://cli.github.com/packages stable main" \
      > /etc/apt/sources.list.d/github-cli.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    # core utilities
    git wget unzip sudo tmux jq \
    # GitHub CLI + git extras
    gh tig lazygit git-delta bat git-lfs git-filter-repo pre-commit \
    # C/C++ toolchain
    build-essential cmake ninja-build clang gdb valgrind pkg-config \
    # Java + Maven
    openjdk-21-jdk maven \
    # Python
    python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04+ base images ship a pre-existing uid/gid-1000 "ubuntu" user
# (like node:*-slim's "node"); rename it to match host naming/permissions.
RUN usermod -l "${USER_NAME}" ubuntu && \
    groupmod -n "${USER_NAME}" ubuntu && \
    usermod -d "/home/${USER_NAME}" -m "${USER_NAME}"

# Passwordless sudo: this is a disposable sandbox already run with
# --dangerously-skip-permissions, so gating sudo behind a password adds no safety.
# sudoers.d filenames must not contain a dot -- sudo(8) ignores files
# whose names match its DefaultIgnorePattern. Use a fixed name so this
# rule still works when USER_NAME contains a dot (e.g. "first.last").
RUN echo "${USER_NAME} ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/claude-box && \
    chmod 0440 /etc/sudoers.d/claude-box

# Ubuntu's apt repos lag on Node.js versions, so install via NodeSource instead.
RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - && \
    apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

RUN npm install -g @anthropic-ai/claude-code

USER ${USER_NAME}

# nvm + Node.js (user-level; the system Node.js above is only for the
# claude-code global install -- project work inside the container should use
# the nvm-managed version). Pinned to a concrete release, not --lts, so two
# builds of this Dockerfile from the same commit produce the same image;
# bump NODE_VERSION here by hand rather than tracking whatever LTS is
# current at build time.
ENV NODE_VERSION=22.13.0
RUN curl -fsSL https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash && \
    bash -c '. "$HOME/.nvm/nvm.sh" && nvm install '"${NODE_VERSION}"' && nvm alias default '"${NODE_VERSION}"

# SDKMAN + Gradle (user-level install; SDKMAN requires an interactive-style
# shell init). Gradle is pinned for the same reproducibility reason as
# Node above. get.sdkman.io has no versioned URL the way nvm's tagged
# install.sh does -- it always serves whatever the current stable installer
# is -- so the script is pinned by checksum instead: the build fails loudly
# if SDKMAN changes what it serves, rather than silently installing a
# different SDKMAN CLI version. Bump SDKMAN_INSTALL_SHA256 by hand (fetch
# https://get.sdkman.io and sha256sum it) for a deliberate upgrade.
ENV GRADLE_VERSION=8.10.2
ENV SDKMAN_INSTALL_SHA256=b6f4fb257b420c7291c35549ae714af6d5af290bb8d57c219918f1f3d3d3e052
RUN curl -fsSL https://get.sdkman.io -o /tmp/sdkman-install.sh && \
    echo "${SDKMAN_INSTALL_SHA256}  /tmp/sdkman-install.sh" | sha256sum -c - && \
    bash /tmp/sdkman-install.sh && \
    rm /tmp/sdkman-install.sh && \
    bash -c 'source "$HOME/.sdkman/bin/sdkman-init.sh" && sdk install gradle '"${GRADLE_VERSION}"

# nvm and SDKMAN only wire their PATH/init lines into ~/.bashrc, which a
# non-interactive `bash -c` -- how this image's entrypoint actually invokes
# claude -- never sources. Expose both toolchains via PATH directly so
# gradle and the nvm-managed node/npm resolve regardless of how the shell
# is invoked. The gradle path relies on SDKMAN's "current" symlink, which
# `sdk install` maintains on disk and needs no shell init to read.
ENV NVM_DIR=/home/${USER_NAME}/.nvm
ENV SDKMAN_DIR=/home/${USER_NAME}/.sdkman
ENV PATH=${NVM_DIR}/versions/node/v${NODE_VERSION}/bin:${SDKMAN_DIR}/candidates/gradle/current/bin:${PATH}

# The directory is handled dynamically by the orchestration script
WORKDIR /home/${USER_NAME}/workspace

CMD ["claude"]
