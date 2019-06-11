QT += testlib
QT -= gui

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

SOURCES +=  tst_moodbar_test.cpp \
    moodbarbuilder.cpp

HEADERS += \
    moodbarbuilder.h
