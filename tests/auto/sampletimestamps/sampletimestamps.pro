TEMPLATE = app
TARGET = tst_sampletimestamps
CONFIG += testcase console c++11 link_pkgconfig
QT = core testlib
PKGCONFIG += nymea
INCLUDEPATH += ../../../plugin ../../../libnymea-energy
SOURCES += tst_sampletimestamps.cpp
