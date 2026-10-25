TEMPLATE = app
TARGET = tst_energylogger
CONFIG += testcase console link_pkgconfig
QT = core sql network testlib
PKGCONFIG += nymea
include(../../../config.pri)
INCLUDEPATH += ../../../plugin ../../../libnymea-energy
HEADERS += ../../../plugin/energylogger.h ../../../libnymea-energy/energylogs.h
SOURCES += tst_energylogger.cpp ../../../libnymea-energy/energylogs.cpp
