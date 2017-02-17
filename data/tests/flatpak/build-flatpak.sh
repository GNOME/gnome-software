rm -rf repo

# build test application
rm chiron/export -rf
flatpak build-finish chiron
appstream-compose --origin=flatpak --basename=org.test.Chiron --prefix=chiron/files --output-dir=chiron/files/share/app-info/xmls org.test.Chiron
flatpak build-export repo chiron --update-appstream

# build test runtime
rm org.test.Runtime/export -rf
flatpak build-finish org.test.Runtime
appstream-compose --origin=flatpak --basename=org.test.Runtime --prefix=org.test.Runtime/files --output-dir=org.test.Runtime/files/share/app-info/xmls org.test.Runtime
flatpak build-export repo org.test.Runtime --runtime --update-appstream
