TEMPLATE = app
TARGET = tst_energylogdatabase
CONFIG += testcase console c++11
QT = core sql testlib
INCLUDEPATH += ../../../plugin
SOURCES += tst_energylogdatabase.cpp
HEADERS += ../../../plugin/energylogdatabase.h
