QT += core gui widgets network
CONFIG += c++11
TARGET = rudp_host
TEMPLATE = app
SOURCES += \
    functional_test.cpp \
    main.cpp \
    rudp_host.cpp \
    logger.cpp \
    test_runner.cpp

HEADERS += \
    functional_test.h \
    rudp_host.h \
    logger.h \
    rudp_types.h \
    test_runner.h
