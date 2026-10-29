# Build environment for Buildroot for the DSL-2542B.
#
# Buildroot builds the cross-toolchain (gcc, binutils, uClibc-ng) and most host
# tools (host-*) itself, so from the system it only needs the basic set from
# the "System requirements" list in the manual (checked by
# support/dependencies/dependencies.sh on every make).
FROM debian:13-slim

ARG USERNAME=builder
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        # --- required by Buildroot ---
        build-essential binutils gcc g++ make \
        bash sed gawk findutils diffutils patch which file \
        gzip bzip2 xz-utils unzip tar cpio rsync bc perl \
        wget ca-certificates \
        # --- optional but useful ---
        # python3: Buildroot scripts (graph-size, pkg-stats) and some packages
        # libncurses-dev: make menuconfig / linux-menuconfig
        # git: fetching sources from git repositories and handling the submodule
        python3 libncurses-dev git \
        # --- convenience inside the container ---
        sudo less vim-tiny bash-completion \
    && rm -rf /var/lib/apt/lists/*

# Buildroot should not run as root (host-tar and other configure scripts
# refuse, and a path mistake could overwrite the system). UID/GID 1000 = the
# typical first user in WSL, so files in output/ have the same owner on both
# sides. Dev Containers adjusts the UID at startup anyway
# (updateRemoteUserUID).
RUN groupadd --gid "$USER_GID" "$USERNAME" \
    && useradd --uid "$USER_UID" --gid "$USER_GID" -m -s /bin/bash "$USERNAME" \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/$USERNAME" \
    && chmod 0440 "/etc/sudoers.d/$USERNAME"

# Directory for downloaded tarballs (BR2_DL_DIR). A Docker volume is mounted
# here; on first mount Docker copies the owner from the image, so the volume
# belongs to $USERNAME rather than root.
RUN mkdir -p "/home/$USERNAME/br-dl" && chown "$USER_UID:$USER_GID" "/home/$USERNAME/br-dl"

# Some packages (and Buildroot itself when checking dependencies) assume
# a UTF-8 locale; C.UTF-8 is built into Debian's glibc.
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8

USER $USERNAME
