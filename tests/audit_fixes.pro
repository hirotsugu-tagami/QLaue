include(image_import.pro)
TARGET = audit-fixes-check
SOURCES -= $$PWD/image_import.cpp
SOURCES += $$PWD/audit_fixes.cpp
