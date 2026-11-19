QT += core gui widgets

CONFIG += console c++11
CONFIG -= app_bundle
TEMPLATE = app
TARGET = uicheck

QMAKE_CXXFLAGS += /utf-8
QMAKE_CXXFLAGS_RELEASE += -O2

INCLUDEPATH += ../src ../src/core ../src/app ../src/ui

SOURCES += uicheck.cpp \
    ../src/core/seedengine.cpp \
    ../src/core/gpuhook.cpp \
    ../src/app/presetstore.cpp \
    ../src/app/searchworker.cpp \
    ../src/ui/banddialog.cpp \
    ../src/ui/conddialog.cpp \
    ../src/ui/detaildialog.cpp \
    ../src/ui/detailview.cpp \
    ../src/ui/helppages.cpp \
    ../src/ui/mainwindow.cpp \
    ../src/ui/style.cpp \
    ../src/ui/uikit.cpp

HEADERS += ../src/ui/mainwindow.h \
    ../src/app/searchworker.h \
    ../src/app/presetstore.h \
    ../src/ui/banddialog.h \
    ../src/ui/conddialog.h \
    ../src/ui/detaildialog.h \
    ../src/ui/detailview.h \
    ../src/ui/helppages.h \
    ../src/ui/style.h \
    ../src/ui/uikit.h

RESOURCES += ../resources/res.qrc
