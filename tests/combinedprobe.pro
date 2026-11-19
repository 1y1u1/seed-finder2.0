QT += core
QT -= gui

CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = combinedprobe

QMAKE_CXXFLAGS += /utf-8
QMAKE_CXXFLAGS_RELEASE += -O2

INCLUDEPATH += ../src/core ref12

SOURCES += combinedprobe.cpp \
    ../src/core/seedengine.cpp \
    ../src/core/gpuhook.cpp

HEADERS += seedengine.h \
    gpuhook.h \
    zombiedata.h \
    ref12/worker.h
