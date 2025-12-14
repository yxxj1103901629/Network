QT = core network

CONFIG += c++17

QMAKE_CXXFLAGS += /utf-8 /wd4200

INCLUDEPATH += \
    $$PWD/include \
    $$PWD/include/paho-mqtt \
    $$PWD/include/concurrentqueue \
    $$PWD/include/libuv-1.51.0/include \
    # $$PWD/include/libuv-1.46.0/include

win32:CONFIG(release, debug|release): DESTDIR = $$PWD/bin/release
else:win32:CONFIG(debug, debug|release): DESTDIR = $$PWD/bin/debug


win32:CONFIG(release, debug|release): LIBS += -L$$PWD/bin/release
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/bin/debug

LIBS += -luv

DEFINES += PAHO_MQTTPP_IMPORTS
LIBS += -lpaho-mqtt3as -lpaho-mqtt3a -lpaho-mqttpp3


HEADERS += \
    common/network/base/CUVLoop.h \
    common/network/base/INetworkManager.h \
    common/network/base/NetworkType.h \
    common/network/impl/CNetworkManager.h \
    common/network/impl/mqttClient/CMqttMessage.h \
    common/network/impl/mqttClient/CPahoMqttClient.h \
    common/network/impl/tcp/CUVTcpClient.h \
    common/network/impl/tcp/CUVTcpSever.h

SOURCES += \
        common/network/base/CUVLoop.cpp \
        common/network/base/INetworkManager.cpp \
        common/network/impl/CNetworkManager.cpp \
        common/network/impl/mqttClient/CPahoMqttClient.cpp \
        common/network/impl/tcp/CUVTcpClient.cpp \
        common/network/impl/tcp/CUVTcpSever.cpp \
        main.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
