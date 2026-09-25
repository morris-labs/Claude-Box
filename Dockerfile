FROM ubuntu:26.04

ARG USER_ID=1000
ARG GROUP_ID=1000
ARG USER_NAME=user

# Install curl first so the GitHub CLI apt repo block below can run.
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# GitHub CLI apt repo (curl is now available).
# -o rather than piping through dd: the default /bin/sh (dash) has no
# pipefail, so a failed curl in a pipe still lets dd "succeed" writing an
# empty/corrupt keyring, and the build would only fail later and more
# confusingly at `apt-get install gh`. Writing straight to a file makes a
# curl failure fail this RUN step immediately, at the actual problem.
RUN curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
      -o /usr/share/keyrings/githubcli-archive-keyring.gpg && \
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
# the nvm-managed version). Installs whatever the current LTS release is at
# build time. A "current" symlink is created alongside nvm's own versioned
# install directory so the PATH entry below can reference a fixed path
# without needing to know which version that resolves to.
RUN curl -fsSL https://raw.githubusercontent.com/nvm-sh/nvm/master/install.sh | bash && \
    bash -c '. "$HOME/.nvm/nvm.sh" && nvm install --lts && nvm alias default node && \
      ln -s "$NVM_DIR/versions/node/$(nvm version default)" "$NVM_DIR/current"'

# SDKMAN + Gradle (user-level install; SDKMAN requires an interactive-style
# shell init). Installs whatever the current default Gradle release is at
# build time.
RUN curl -fsSL https://get.sdkman.io | bash && \
    bash -c 'source "$HOME/.sdkman/bin/sdkman-init.sh" && sdk install gradle'

# nvm and SDKMAN only wire their PATH/init lines into ~/.bashrc, which a
# non-interactive `bash -c` -- how this image's entrypoint actually invokes
# claude -- never sources. Expose both toolchains via PATH directly so
# gradle and the nvm-managed node/npm resolve regardless of how the shell
# is invoked. Both paths rely on a "current" symlink maintained on disk --
# SDKMAN's own, and the one created above for nvm -- so neither needs a
# shell init to resolve.
ENV NVM_DIR=/home/${USER_NAME}/.nvm
ENV SDKMAN_DIR=/home/${USER_NAME}/.sdkman
ENV PATH=${NVM_DIR}/current/bin:${SDKMAN_DIR}/candidates/gradle/current/bin:${PATH}

# The directory is handled dynamically by the orchestration script
WORKDIR /home/${USER_NAME}/workspace

CMD ["claude"]
