FROM fedora:31

RUN dnf -y install \
    appstream \
    accountsservice-devel \
    clang \
    clang-analyzer \
    dbus-daemon \
    dbus-devel \
    desktop-file-utils \
    docbook-style-xsl \
    flatpak-devel \
    fwupd-devel \
    gcc \
    gettext \
    git \
    glib2-devel \
    gnome-online-accounts-devel \
    gobject-introspection \
    gobject-introspection-devel \
    gperf \
    gsettings-desktop-schemas-devel \
    gspell-devel \
    gtk-doc \
    gtk3-devel \
    json-glib-devel \
    itstool \
    lcov \
    libappstream-glib-devel \
    lmdb-devel \
    appstream-devel \
    libdnf-devel \
    libgudev-devel \
    liboauth-devel \
    libsecret-devel \
    libsoup-devel \
    libstemmer-devel \
    libxmlb-devel \
    libxslt \
    libyaml-devel \
    NetworkManager-libnm-devel \
    ninja-build \
    ostree-devel \
    PackageKit \
    PackageKit-glib-devel \
    pam-devel \
    pcre-devel \
    polkit-devel \
    python3 \
    python3-pip \
    python3-wheel \
    rpm-devel \
    rpm-ostree-devel \
    shared-mime-info \
    snapd-glib-devel \
    unzip \
    valgrind \
    valgrind-devel \
    wget \
    xz \
    zlib-devel \
 && dnf clean all

RUN pip3 install meson==0.50.0

# Enable sudo for wheel users
RUN sed -i -e 's/# %wheel/%wheel/' -e '0,/%wheel/{s/%wheel/# %wheel/}' /etc/sudoers

ARG HOST_USER_ID=5555
ENV HOST_USER_ID ${HOST_USER_ID}
RUN useradd -u $HOST_USER_ID -G wheel -ms /bin/bash user

USER user
WORKDIR /home/user

COPY cache-subprojects.sh .
RUN ./cache-subprojects.sh

ENV LANG C.UTF-8
