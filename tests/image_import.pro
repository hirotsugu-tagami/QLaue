include(../QLaue.pro)

TARGET = image-import-check
CONFIG -= app_bundle
CONFIG += console c++11
QMAKE_INFO_PLIST =
ICON =
RC_FILE =
SOURCES -= main.cpp
SOURCES += tests/image_import.cpp

for(entry, SOURCES): resolvedSources += $$absolute_path($$entry, $$PWD/..)
for(entry, HEADERS): resolvedHeaders += $$absolute_path($$entry, $$PWD/..)
for(entry, FORMS): resolvedForms += $$absolute_path($$entry, $$PWD/..)
for(entry, RESOURCES): resolvedResources += $$absolute_path($$entry, $$PWD/..)
SOURCES = $$resolvedSources
HEADERS = $$resolvedHeaders
FORMS = $$resolvedForms
RESOURCES = $$unique(resolvedResources)
INCLUDEPATH += $$PWD/..
