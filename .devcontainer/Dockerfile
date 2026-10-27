# Środowisko do budowy Buildroota dla DSL-2542B.
#
# Buildroot sam buduje cross-toolchain (gcc, binutils, uClibc-ng) i większość
# narzędzi hosta (host-*), więc z systemu potrzebuje tylko podstawowego zestawu
# z listy "System requirements" w podręczniku (sprawdza go
# support/dependencies/dependencies.sh przy każdym make).
FROM debian:13-slim

ARG USERNAME=builder
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        # --- wymagane przez Buildroota ---
        build-essential binutils gcc g++ make \
        bash sed gawk findutils diffutils patch which file \
        gzip bzip2 xz-utils unzip tar cpio rsync bc perl \
        wget ca-certificates \
        # --- opcjonalne, ale przydatne ---
        # python3: skrypty Buildroota (graph-size, pkg-stats) i niektóre pakiety
        # libncurses-dev: make menuconfig / linux-menuconfig
        # git: pobieranie źródeł z repozytoriów git i obsługa submodułu
        python3 libncurses-dev git \
        # --- wygoda w kontenerze ---
        sudo less vim-tiny bash-completion \
    && rm -rf /var/lib/apt/lists/*

# Buildroot nie powinien działać jako root (host-tar i inne skrypty configure
# odmawiają, a błąd w ścieżce mógłby nadpisać system). UID/GID 1000 = typowy
# pierwszy użytkownik w WSL, więc pliki w output/ mają tego samego właściciela
# po obu stronach. Dev Containers i tak dopasowuje UID przy starcie
# (updateRemoteUserUID).
RUN groupadd --gid "$USER_GID" "$USERNAME" \
    && useradd --uid "$USER_UID" --gid "$USER_GID" -m -s /bin/bash "$USERNAME" \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" > "/etc/sudoers.d/$USERNAME" \
    && chmod 0440 "/etc/sudoers.d/$USERNAME"

# Katalog na pobrane tarballe (BR2_DL_DIR). Montowany jest tu wolumen Dockera;
# przy pierwszym montowaniu Docker kopiuje właściciela z obrazu, dzięki czemu
# wolumen należy do $USERNAME, a nie do roota.
RUN mkdir -p "/home/$USERNAME/br-dl" && chown "$USER_UID:$USER_GID" "/home/$USERNAME/br-dl"

# Część pakietów (i sam Buildroot przy sprawdzaniu zależności) zakłada
# locale z UTF-8; C.UTF-8 jest wbudowane w glibc Debiana.
ENV LANG=C.UTF-8 LC_ALL=C.UTF-8

USER $USERNAME
