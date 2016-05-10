rm -rf repo

# build test application
rm chiron/export -rf
flatpak build-finish chiron
flatpak build-export repo chiron
appstream-compose --origin=flatpak --basename=org.test.Chiron --prefix=chiron/files --output-dir=chiron/files/share/app-info/xmls org.test.Chiron
flatpak build-export repo chiron --update-appstream

# build test runtime
rm org.test.Runtime/export -rf
flatpak build-finish org.test.Runtime
flatpak build-export repo org.test.Runtime --runtime
