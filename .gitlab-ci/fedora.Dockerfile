FROM fedora:41

RUN dnf -y install \
    appstream \
    accountsservice-devel \
    cairo-devel \
    cairo-gobject-devel \
    clang \
    clang-analyzer \
    dbus-daemon \
    dbus-devel \
    desktop-file-utils \
    docbook-style-xsl \
    flatpak-devel \
    fwupd-devel \
    gcc \
    gdk-pixbuf2-devel \
    gettext \
    gi-docgen \
    git \
    glib2-devel \
    glibc-locale-source \
    gnome-desktop4 \
    gobject-introspection \
    gobject-introspection-devel \
    gperf \
    graphene-devel \
    gsettings-desktop-schemas-devel \
    gstreamer1-plugins-bad-free-devel \
    gtk-doc \
    json-glib-devel \
    itstool \
    lcov \
    lmdb-devel \
    appstream-devel \
    libadwaita-devel \
    libcurl-devel \
    libepoxy-devel \
    libglib-testing-devel \
    libgudev-devel \
    libjpeg-turbo-devel \
    liboauth-devel \
    libsecret-devel \
    libsoup3-devel \
    libstemmer-devel \
    libXcursor-devel \
    libXdamage-devel \
    libXext-devel \
    libXfixes-devel \
    libXi-devel \
    libXinerama-devel \
    libxkbcommon-devel \
    libxmlb-devel \
    libXrandr-devel \
    libxslt \
    libyaml-devel \
    malcontent-devel \
    NetworkManager-libnm-devel \
    ninja-build \
    ostree-devel \
    PackageKit \
    PackageKit-glib-devel \
    pam-devel \
    pango-devel \
    pcre-devel \
    polkit-devel \
    python3 \
    python3-gobject \
    python3-pip \
    python3-wheel \
    rpm-devel \
    rpm-ostree-devel \
    sassc \
    shared-mime-info \
    snapd-glib-devel \
    sysprof-capture-devel \
    unzip \
    valgrind \
    wayland-protocols-devel \
    wget \
    xz \
    yelp-tools \
    zlib-devel \
 && dnf clean all

RUN pip3 install meson==1.6.1

# Enable sudo for wheel users
RUN sed -i -e 's/# %wheel/%wheel/' -e '0,/%wheel/{s/%wheel/# %wheel/}' /etc/sudoers

ARG HOST_USER_ID=5555
ENV HOST_USER_ID ${HOST_USER_ID}
RUN useradd -u $HOST_USER_ID -G wheel -ms /bin/bash user

USER user
WORKDIR /home/user

COPY subprojects.meson.zip .
COPY cache-subprojects.sh .
RUN ./cache-subprojects.sh

ENV LANG C.UTF-8
