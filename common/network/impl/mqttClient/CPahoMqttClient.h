#ifndef CPAHOMQTTCLIENT_H
#define CPAHOMQTTCLIENT_H

#include "mqtt/async_client.h"
#include "mqtt/message.h"

#include <atomic>
#include <mutex>

#include "CMqttMessage.h"

namespace Common {
namespace Network {

class CPahoMqttClient
{
private:
    /**
     * @brief 内部回调处理类
     * @details 继承自paho-mqtt的callback类，实现回调函数
     */
    class MqttCallback : public virtual mqtt::callback
    {
    public:
        /**
         * @brief 构造函数
         * @param parent 指向父类的指针
         */
        MqttCallback(CPahoMqttClient *parent)
            : m_parent(parent)
        {}
        /**
         * @brief 覆盖基类的虚函数，实现连接成功回调
         * @param cause 连接原因
         */
        void connected(const std::string &cause) override;
        /**
         * @brief 覆盖基类的虚函数，实现连接丢失回调
         * @param cause 断开原因
         */
        void connection_lost(const std::string &cause) override;
        /**
         * @brief 覆盖基类的虚函数，实现消息到达回调
         * @param msg 到达的消息指针
         */
        void message_arrived(mqtt::const_message_ptr msg) override;
        /**
         * @brief 覆盖基类的虚函数，实现消息发送完成回调
         * @param token 发送令牌指针
         */
        void delivery_complete(mqtt::delivery_token_ptr token) override;

    private:
        CPahoMqttClient *m_parent;
    };

public:
    /**
     * @brief 连接结果回调函数类型定义
     * @param success 连接是否成功
     */
    using ConnectCallback = std::function<void(bool success, const std::string &info)>;
    /**
     * @brief 断开连接回调函数类型定义
     * @param success 断开是否成功
     * @param info 相关信息
     */
    using DisconnectCallback = std::function<void(bool success, const std::string &info)>;
    /**
     * @brief 发布结果回调函数类型定义
     * @param success 发布是否成功
     * @param info 相关信息
     */
    using PublishCallback = std::function<void(bool success, const std::string &info)>;
    /**
     * @brief 订阅结果回调函数类型定义
     * @param success 订阅是否成功
     * @param info 相关信息
     */
    using SubscribeCallback = std::function<void(bool success, const std::string &info)>;
    /**
     * @brief 取消订阅结果回调函数类型定义
     * @param success 取消订阅是否成功
     * @param info 相关信息
     */
    using UnsubscribeCallback = std::function<void(bool success, const std::string &info)>;
    /**
     * @brief 接收消息回调函数类型定义
     * @param message 接收到的MQTT消息
     */
    using RecvCallback = std::function<void(const CMqttMessage &message)>;

public:
    explicit CPahoMqttClient();
    ~CPahoMqttClient();

    /**
     * @brief 初始化MQTT客户端
     * @param broker MQTT代理地址
     * @param clientId 客户端标识
     * @return 初始化是否成功
     */
    bool init(const std::string &broker, const std::string &clientId);

    /**
     * @brief 连接到MQTT代理服务器
     * @param callback 连接结果回调函数（可选）
     * @param username 用户名（可选）
     * @param password 密码（可选）
     */
    void connect(ConnectCallback callback = nullptr,
                 const std::string &username = "",
                 const std::string &password = "");
    /**
     * @brief 断开与MQTT代理服务器的连接
     */
    bool disconnect();
    /**
     * @brief 检查是否已连接到MQTT代理服务器
     * @return 是否已连接
     */
    bool isConnected() const;

    /**
     * @brief 订阅指定主题
     * @param topic 主题名称
     * @param qos 服务质量等级
     */
    void subscribe(const std::string &topic, int qos = 0);
    /**
     * @brief 取消订阅指定主题
     * @param topic 主题名称
     */
    void unsubscribe(const std::string &topic);

    /**
     * @brief 发布消息到指定主题
     * @param topic 主题名称
     * @param payload 消息载荷
     * @param qos 服务质量等级
     * @param retained 是否为保留消息
     * @return 发布是否成功
     */
    bool publish(const std::string &topic,
                 const std::string &payload,
                 int qos = 0,
                 bool retained = false);

    /**
     * @brief 设置连接结果回调函数
     * @param callback 连接结果回调函数
     */
    void setConnectCallback(ConnectCallback &&callback);

    /**
     * @brief 设置断开连接回调函数
     * @param callback 断开连接回调函数
     */
    void setDisconnectCallback(DisconnectCallback &&callback);

    /**
     * @brief 设置发布结果回调函数
     * @param callback 发布结果回调函数
     */
    void setPublishCallback(PublishCallback &&callback);

    /**
     * @brief 设置订阅结果回调函数
     * @param callback 订阅结果回调函数
     */
    void setSubscribeCallback(SubscribeCallback &&callback);

    /**
     * @brief 设置取消订阅结果回调函数
     * @param callback 取消订阅结果回调函数
     */
    void setUnsubscribeCallback(UnsubscribeCallback &&callback);

    /**
     * @brief 设置接收消息回调函数
     * @param callback 接收消息回调函数
     */
    void setRecvCallback(RecvCallback &&callback);

private:
    std::string m_broker;         // MQTT代理地址
    std::string m_clientId;       // 客户端标识
    mqtt::async_client *m_client; // Paho MQTT异步客户端实例
    MqttCallback m_callback;      // 内部回调处理实例

    // 连接选项
    std::string m_username; // 用户名
    std::string m_password; // 密码

    mutable std::mutex m_mutex; // 线程安全互斥锁
    std::atomic<bool> m_connected; // 连接状态标志（原子变量确保线程安全）

    // 回调函数
    ConnectCallback m_connectCallback;         // 连接结果回调函数
    DisconnectCallback m_disconnectCallback;   // 断开连接回调函数
    PublishCallback m_publishCallback;         // 发布结果回调函数
    SubscribeCallback m_subscribeCallback;     // 订阅结果回调函数
    UnsubscribeCallback m_unsubscribeCallback; // 取消订阅结果回调函数
    RecvCallback m_recvCallback;               // 接收消息回调函数
};

} // namespace Network
} // namespace Common

#endif // CPAHOMQTTCLIENT_H
