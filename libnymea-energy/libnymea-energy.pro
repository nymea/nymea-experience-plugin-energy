TEMPLATE = lib
TARGET = $$qtLibraryTarget(nymea-energy)

include(../config.pri)
NYMEA_ENERGY_VERSION_STRING = "0.0.1"

CONFIG += link_pkgconfig
PKGCONFIG += nymea

HEADERS += \
    energylogs.h \
    energymanager.h \
    energyplugin.h

SOURCES += \
    energylogs.cpp \
    energymanager.cpp \
    energyplugin.cpp

target.path = $$[QT_INSTALL_LIBS]
INSTALLS += target

for(header, HEADERS) {
    path = $$[QT_INSTALL_PREFIX]/include/nymea-energy/$${dirname(header)}
    eval(headers_$${path}.files += $${header})
    eval(headers_$${path}.path = $${path})
    eval(INSTALLS *= headers_$${path})
}

CONFIG += create_pc create_prl no_install_prl
QMAKE_PKGCONFIG_NAME = libnymea-energy
QMAKE_PKGCONFIG_DESCRIPTION = nymea energy library
QMAKE_PKGCONFIG_PREFIX = $$[QT_INSTALL_PREFIX]
QMAKE_PKGCONFIG_INCDIR = $$[QT_INSTALL_PREFIX]/include/nymea-energy/
QMAKE_PKGCONFIG_LIBDIR = $$target.path
QMAKE_PKGCONFIG_VERSION = $$NYMEA_ENERGY_VERSION_STRING
QMAKE_PKGCONFIG_FILE = nymea-energy
QMAKE_PKGCONFIG_DESTDIR = pkgconfig
