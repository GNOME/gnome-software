FROM debian:trixie

RUN apt-get update -qq && apt-get install --no-install-recommends -qq -y \
    appstream \
    clang \
    clang-tools \
    dbus \
    desktop-file-utils \
    docbook-xsl \
    gcc \
    g++ \
    gettext \
    gi-docgen \
    git \
    gnome-pkg-tools \
    gobject-introspection \
    gperf \
    gsettings-desktop-schemas-dev \
    gtk-doc-tools \
    itstool \
    lcov \
    libaccountsservice-dev \
    libadwaita-1-dev \
    libappstream-dev \
    libcairo2-dev \
    libcairo-gobject2 \
    libcurl4-gnutls-dev \
    libepoxy-dev \
    libgtk-4-1 \
    libflatpak-dev \
    libfontconfig-dev \
    libfwupd-dev \
    libgdk-pixbuf-2.0-dev \
    libgirepository1.0-dev \
    libglib2.0-dev \
    libglib-testing-0-dev \
    libgoa-1.0-dev \
    libgraphene-1.0-dev \
    libgstreamer-plugins-bad1.0-dev \
    libgudev-1.0-dev \
    libjpeg62-turbo-dev \
    libjson-glib-dev \
    liblmdb-dev \
    libmalcontent-0-dev \
    libpackagekit-glib2-dev \
    libpam0g-dev \
    libpango1.0-dev \
    libpolkit-gobject-1-dev \
    librsvg2-common \
    libsoup-3.0-dev \
    libstemmer-dev \
    libxcursor-dev \
    libxdamage-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxinerama-dev \
    libxkbcommon-dev \
    libxmlb-dev \
    libxml2-utils \
    libxrandr-dev \
    libyaml-dev \
    ninja-build \
    packagekit \
    pkg-config \
    python3 \
    python3-pip \
    python3-setuptools \
    python3-wheel \
    sassc \
    shared-mime-info \
    sudo \
    sysprof \
    unzip \
    valgrind \
    wayland-protocols \
    wget \
    xsltproc \
    xz-utils \
 && rm -rf /usr/share/doc/* /usr/share/man/*

RUN pip3 install --break-system-packages meson==1.6.1

# Enable passwordless sudo for sudo users
RUN sed -i -e '/%sudo/s/ALL$/NOPASSWD: ALL/' /etc/sudoers

ARG HOST_USER_ID=5555
ENV HOST_USER_ID ${HOST_USER_ID}
RUN useradd -u $HOST_USER_ID -G sudo -ms /bin/bash user

USER user
WORKDIR /home/user

COPY subprojects.meson.zip .
COPY cache-subprojects.sh .
RUN ./cache-subprojects.sh

ENV LANG=C.UTF-8 LANGUAGE=C.UTF-8 LC_ALL=C.UTF-8
