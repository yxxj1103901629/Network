#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>
#include <iostream>

#include "common/network/impl/mqttClient/CPahoMqttClient.h"
#include "common/network/impl/tcp/CUVTcpClient.h"

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
    mqttClient->setRecvCallback([/*mqttClient*/](const Common::Network::CMqttMessage &message) {
        std::string payload(message.payload, message.payload + message.payloadLen);
        std::cout << "\n=== Received MQTT Message ===" << std::endl;
        std::cout << "Topic: " << message.topic << std::endl;
        std::cout << "Payload: " << payload << std::endl;
        std::cout << "QoS: " << message.qos << std::endl;
        std::cout << "Retained: " << (message.retained ? "Yes" : "No") << std::endl;
        std::cout << "=============================\n" << std::endl;

        // // 取消订阅主题并断开连接（可选）
        // mqttClient->unsubscribe("test/topic");
        // std::cout << "Unsubscribed from topic: test/topic" << std::endl;

        // // 断开连接（可选）
        // if (mqttClient->disconnect()) {
        //     std::cout << "Disconnected from MQTT broker successfully." << std::endl;
        // } else {
        //     std::cout << "Failed to disconnect from MQTT broker." << std::endl;
        // }
    });

    std::cout << "Connecting to MQTT broker..." << std::endl;
    // 3. 连接到MQTT broker
    mqttClient->connect(
        [=](bool success) {
            if (success) {
                std::cout << "\n=== Connected to MQTT broker successfully ===" << std::endl;

                // 4. 订阅主题
                std::string topic = "test/topic";
                int qos = 1;
                mqttClient->subscribe(topic, qos);
                std::cout << "Subscribed to topic: " << topic << " (QoS: " << qos << ")"
                          << std::endl;

                // 发布多条测试消息
                // for (int i = 0; i < 30; ++i) {
                //     // 发布测试消息
                //     std::string payload = "Test message " + std::to_string(i + 1);
                //     if (mqttClient->publish(topic, payload, qos, false)) {
                //         std::cout << "Message published successfully: " << payload << std::endl;
                //     } else {
                //         std::cout << "Failed to publish message." << std::endl;
                //     }
                // }
            } else {
                std::cout << "Failed to connect to MQTT broker." << std::endl;
            }
        },
        "1", // 用户名（根据实际情况修改）
        "1"  // 密码（根据实际情况修改）
    );

    QTimer::singleShot(30 * 1000, qApp, [mqttClient]() {
        // 取消订阅主题并断开连接（可选）
        mqttClient->unsubscribe("test/topic");
        std::cout << "Unsubscribed from topic: test/topic" << std::endl;

        // 断开连接（可选）
        if (mqttClient->disconnect()) {
            std::cout << "Disconnected from MQTT broker successfully." << std::endl;
        } else {
            std::cout << "Failed to disconnect from MQTT broker." << std::endl;
        }
    });

    auto timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, qApp, [mqttClient]() {
        std::string payload = "Hello from Qt MQTT Client!";
        if (mqttClient->publish("test/topic", payload, 1, false)) {
            std::cout << "Message published successfully: " << payload << std::endl;
        } else {
            std::cout << "Failed to publish message." << std::endl;
        }
    });
    timer->start(100);

    return 0;
}

int testTcpClient()
{
    using namespace Common::Network;

    // 创建TCP客户端实例
    auto tcpClient = new CUVTcpClient();

    // 设置接收数据回调
    tcpClient->setReceiveCallback([](const char *data, size_t length) {
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
    tcpClient->setDisconnectCallback([]() { std::cout << "TCP client disconnected" << std::endl; });

    // 设置接收超时回调（默认为0，表示不启用接收超时）
    tcpClient->setReceiveTimeout(5000, []() {
        std::cout << "TCP receive timeout occurred!" << std::endl;
    });

    // 设置重连间隔（初始1秒，最大10秒）(可选)
    tcpClient->setReconnectInterval(1000, 10000);

    // 设置连接回调（可选）
    tcpClient->setConnectCallback([=](bool success, const std::string &) {
        if (success) {
            std::cout << "TCP client connected successfully!" << std::endl;

            // 发送多条测试数据
            // for (int i = 0; i < 50; ++i) {
            //     std::string testData = "Hello from TCP client! Message " + std::to_string(i + 1);
            //     tcpClient->send(testData, [i](bool success, const std::string &err) {
            //         if (success) {
            //             std::cout << "Time: "
            //                       << QDateTime::currentDateTime()
            //                              .toString("yyyy-MM-dd HH:mm:ss.zzz")
            //                              .toStdString()
            //                       << std::endl;
            //             std::cout << "Data sent successfully for message " << (i + 1) << "!"
            //                       << std::endl;
            //         } else {
            //             // std::cout << "Failed to send data: " << err << std::endl;
            //             (void) err;
            //         }
            //     });
            // }
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

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication a(argc, argv);
    int ret = 0;

    // 运行MQTT测试
    // testMQTT();

    // 运行TCP客户端测试
    testTcpClient();

    // QTimer::singleShot(5 * 1000, &a, &QCoreApplication::quit);
    ret = a.exec();
    return ret;
}
