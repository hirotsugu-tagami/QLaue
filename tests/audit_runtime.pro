include(image_import.pro)
TARGET = audit-runtime-check
SOURCES -= $$PWD/image_import.cpp
SOURCES += $$PWD/audit_runtime.cpp
