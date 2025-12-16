#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include "common/network/impl/mqttClient/CPahoMqttClient.h"
#include "common/network/impl/tcp/CUVTcpClient.h"
#include "common/network/impl/tcp/CUVTcpServer.h"

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

int testMQTT()
{
    auto mqttClient = new Common::Network::CPahoMqttClient();

    // 1. 初始化MQTT客户端
    if (mqttClient->init("192.168.110.23:2882", "QtMqttClientTest")) {
        std::cout << "MQTT client initialized successfully." << std::endl;
    } else {
        std::cout << "Failed to initialize MQTT client." << std::endl;
        return -1;
    }

    std::cout << "Starting MQTT client..." << std::endl;
    // 2. 设置接收消息回调
    mqttClient->setRecvCallback([/*mqttClient*/](const Common::Network::CMqttMessage& message) {
        std::string payload(message.payload, message.payload + message.payloadLen);
        std::cout << "\n=== Received MQTT Message ===" << std::endl;
        std::cout << "Topic: " << message.topic << std::endl;
        std::cout << "Payload: " << payload << std::endl;
        std::cout << "QoS: " << message.qos << std::endl;
        std::cout << "Retained: " << (message.retained ? "Yes" : "No") << std::endl;
        std::cout << "=============================\n" << std::endl;

        // // 取消订阅主题并断开连接（可选）
        // mqttClient->unsubscribe("test/topic");

        // // 断开连接（可选）
        // mqttClient->disconnect();
    });

    // 设置断开连接回调
    mqttClient->setDisconnectCallback([](bool success, const std::string& info) {
        std::cout << "\n=== Disconnected from MQTT broker ===" << std::endl;
        std::cout << "Success: " << (success ? "Yes" : "No") << std::endl;
        std::cout << "Info: " << info << std::endl;
        std::cout << "========================================\n" << std::endl;
    });

    // 设置发布消息回调
    mqttClient->setPublishCallback([](bool success, const std::string& info) {
        std::cout << "\n=== Publish Result ===" << std::endl;
        std::cout << "Success: " << (success ? "Yes" : "No") << std::endl;
        std::cout << "Info: " << info << std::endl;
        std::cout << "=====================\n" << std::endl;
    });

    // 设置订阅主题回调
    mqttClient->setSubscribeCallback([](bool success, const std::string& info) {
        std::cout << "\n=== Subscribe Result ===" << std::endl;
        std::cout << "Success: " << (success ? "Yes" : "No") << std::endl;
        std::cout << "Info: " << info << std::endl;
        std::cout << "======================\n" << std::endl;
    });

    // 设置取消订阅主题回调
    mqttClient->setUnsubscribeCallback([](bool success, const std::string& info) {
        std::cout << "\n=== Unsubscribe Result ===" << std::endl;
        std::cout << "Success: " << (success ? "Yes" : "No") << std::endl;
        std::cout << "Info: " << info << std::endl;
        std::cout << "========================\n" << std::endl;
    });

    std::cout << "Connecting to MQTT broker..." << std::endl;
    // 3. 连接到MQTT broker
    mqttClient->connect(
        [=](bool success, const std::string& info) {
            if (success) {
                std::cout << "\n=== Connected to MQTT broker successfully ===" << std::endl;
                std::cout << "Info: " << info << std::endl;

                // 4. 订阅主题
                std::string topic = "test/7001";
                int qos = 1;
                mqttClient->subscribe(topic, qos);
                std::cout << "Subscribed to topic: " << topic << " (QoS: " << qos << ")"
                          << std::endl;

                // 发布测试消息
                std::string payload = "Hello from Qt MQTT Client!";
                mqttClient->publish(topic, payload, qos, false);
            } else {
                std::cout << "\n=== Failed to connect to MQTT broker ===" << std::endl;
                std::cout << "Info: " << info << std::endl;
                std::cout << "======================================\n" << std::endl;
            }
        },
        "1", // 用户名（根据实际情况修改）
        "1"  // 密码（根据实际情况修改）
    );

    // auto timer = new QTimer(qApp);
    // QObject::connect(timer, &QTimer::timeout, qApp, [mqttClient]() {
    //     std::string payload = "Hello from Qt MQTT Client!";
    //     if (mqttClient->publish("test/topic", payload, 1, false)) {
    //         std::cout << "Message published successfully: " << payload << std::endl;
    //     } else {
    //         std::cout << "Failed to publish message." << std::endl;
    //     }
    // });
    // timer->start(100);


    QTimer::singleShot(2 * 1000, qApp, [mqttClient]() {
        // 取消订阅主题并断开连接（可选）
        mqttClient->unsubscribe("test/topic");

        // 断开连接（可选）
        mqttClient->disconnect();
    });

    QTimer::singleShot(3 * 1000, qApp, [mqttClient]() {
        std::cout << "Cleaning up MQTT client resources..." << std::endl;
        // 清理资源
        delete mqttClient;
    });

    return 0;
}

int testTcpClient()
{
    using namespace Common::Network;

    // 创建TCP客户端实例
    auto tcpClient = new CUVTcpClient();

    // 设置接收数据回调
    tcpClient->setReceiveCallback([](const char* data, size_t length) {
        std::string received(data, length);
        std::cout << "\n=== Received TCP Data ===" << std::endl;
        std::cout << "Time: "
                  << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz").toStdString()
                  << std::endl;
        // std::cout << "Data: " << received << std::endl;
        (void) received;
        std::cout << "Length: " << length << " bytes" << std::endl;
        std::cout << "=========================\n" << std::endl;
    });

    // 设置断开连接回调（可选）
    tcpClient->setDisconnectCallback([](bool success, const std::string& error) {
        if (success) {
            std::cout << "TCP client disconnected successfully." << std::endl;
        } else {
            std::cout << "TCP client disconnected with error: " << error << std::endl;
        }
    });

    // 设置接收超时回调（默认为0，表示不启用接收超时）
    tcpClient->setReceiveTimeout(0, [](const std::string& info) {
        std::cout << "Receive timeout occurred: " << info << std::endl;
    });

    // 设置重连回调（可选）
    tcpClient->setReconnectCallback([](const std::string &info) {
        std::cout << "Reconnecting due to: " << info << std::endl;
    });
    // 设置重连间隔（初始1秒，最大10秒）(可选)
    tcpClient->setReconnectInterval(1000, 10000);

    // 设置连接回调（可选）
    tcpClient->setConnectCallback([=](bool success, const std::string&) {
        if (success) {
            std::cout << "TCP client connected successfully!" << std::endl;

            // 发送多条测试数据
            for (int i = 0; i < 50; ++i) {
                std::string testData = "Hello from TCP client! Message " + std::to_string(i + 1);
                tcpClient->send(testData, [i](bool success, const std::string &err) {
                    if (success) {
                        std::cout << "Time: "
                                  << QDateTime::currentDateTime()
                                         .toString("yyyy-MM-dd HH:mm:ss.zzz")
                                         .toStdString()
                                  << std::endl;
                        std::cout << "Data sent successfully for message " << (i + 1) << "!"
                                  << std::endl;
                    } else {
                        // std::cout << "Failed to send data: " << err << std::endl;
                        (void) err;
                    }
                });
            }
        }
    });
    // 连接到服务器
    tcpClient->connect("192.168.110.23", 12345);

    // 连接成功后发送多条测试数据
    // auto timer = new QTimer(qApp);
    // QObject::connect(timer, &QTimer::timeout, qApp, [=]() {
    //     std::string testData = "Hello from TCP client! Message ";
    //     tcpClient->send(testData, [](bool sendSuccess, const std::string &err) {
    //         if (sendSuccess) {
    //             std::cout << "Time: "
    //                       << QDateTime::currentDateTime()
    //                              .toString("yyyy-MM-dd HH:mm:ss.zzz")
    //                              .toStdString()
    //                       << std::endl;
    //             std::cout << "Data sent successfully for message !" << std::endl;
    //         } else {
    //             // std::cout << "Failed to send data: " << err << std::endl;
    //             (void)err;
    //         }
    //     });
    // });
    // timer->start(1000); // 每1000毫秒发送一条消息
    // QTimer::singleShot(10 * 1000, timer, &QTimer::stop); // 10秒后停止发送消息

    // 添加定时器，断开连接
    // QTimer::singleShot(2 * 1000, qApp, [tcpClient]() {
    //     std::cout << "Disconnecting TCP client..." << std::endl;
    //     tcpClient->disconnect();
    // });

    // 添加定时器，清理资源并退出
    // QTimer::singleShot(3 * 1000, qApp, [tcpClient]() {
    //     std::cout << "Cleaning up TCP client resources..." << std::endl;
    //     delete tcpClient;
    // });

    return 0;
}

int testTcpServer()
{
    using namespace Common::Network;

    // 创建TCP服务器实例
    auto tcpServer = new CUVTcpServer();
    // 设置服务器启动回调
    tcpServer->setStartCallback([](bool success, const std::string& info) {
        if (success) {
            std::cout << info << std::endl;
        } else {
            std::cout << "Failed to start TCP server: " << info << std::endl;
        }
    });
    // 设置服务器停止回调
    tcpServer->setStopCallback([](std::string info) { std::cout << "TCP server stopped: " << info << std::endl; });
    // 设置客户端连接回调
    tcpServer->setConnectCallback([=](const Address& addr, bool success, const std::string& error) {
        if (success) {
            std::cout << "Client connected: " << addr.toString() << std::endl;

            // 发送欢迎消息
            std::string welcomeMsg = addr.toString() + " Welcome to the TCP server!"
                                     + std::string("\n");
            tcpServer->send(addr, welcomeMsg);
        } else {
            std::cout << "Failed to accept client connection from " << addr.toString() << ": "
                      << error << std::endl;
        }
    });
    // 设置客户端断开连接回调
    // tcpServer->setDisconnectCallback([](const Address& addr) {
    //     std::cout << "Client disconnected: " << addr.toString() << std::endl;
    // });
    // 设置客户端接收数据回调
    tcpServer->setReceiveCallback([](const Address& addr, const std::string& data) {
        std::cout << "Received data from " << addr.toString() << ": " << data << std::endl;
    });
    // 设置发送数据回调
    tcpServer->setSendCallback([](const Address& addr, bool success, const std::string& error) {
        if (success) {
            std::cout << "Data sent successfully to " << addr.toString() << std::endl;
        } else {
            std::cout << "Failed to send data to " << addr.toString() << ": " << error << std::endl;
        }
    });
    // 设置超时回调（可选）
    tcpServer->setReceiveTimeoutCallback([](const Address &addr) {
        std::cout << "Receive timeout from client: " << addr.toString() << std::endl;
    });
    // 设置超时间隔（可选，默认不启用超时）
    tcpServer->setReceiveTimeoutInterval(1000);
    // 启动服务器并监听指定地址和端口
    tcpServer->listen("192.168.110.23", 40004);

    // 添加定时器，停止服务器
    // QTimer::singleShot(2 * 1000, qApp, [tcpServer]() {
    //     std::cout << "Stopping TCP server..." << std::endl;
    //     tcpServer->stop();
    // });

    // 添加定时器，清理资源并退出
    QTimer::singleShot(4 * 1000, qApp, [tcpServer]() {
        std::cout << "Cleaning up TCP server resources..." << std::endl;
        delete tcpServer;
    });

    return 0;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication a(argc, argv);
    int ret = 0;

    // 运行MQTT测试
    testMQTT();

    // 运行TCP客户端测试
    // testTcpClient();

    // 运行TCP服务器测试
    // testTcpServer();

    QTimer::singleShot(5 * 1000, &a, &QCoreApplication::quit);
    ret = a.exec();
    return ret;
}
