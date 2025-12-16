#include "CPahoMqttClient.h"

#include <atomic>

using namespace Common::Network;

// MQTT回调处理类实现
void CPahoMqttClient::MqttCallback::connected(const std::string &cause)
{
    m_parent->m_connected.store(true);

    // 调用用户提供的连接回调，无需持有锁
    if (m_parent->m_connectCallback) {
        m_parent->m_connectCallback(true, cause);
    }
}

void CPahoMqttClient::MqttCallback::connection_lost(const std::string &cause)
{
    m_parent->m_connected.store(false);

    // 调用用户提供的断开连接回调，无需持有锁
    if (m_parent->m_disconnectCallback) {
        m_parent->m_disconnectCallback(true, cause);
    }
}

void CPahoMqttClient::MqttCallback::message_arrived(mqtt::const_message_ptr msg)
{
    CMqttMessage message;
    message.topic = msg->get_topic();
    message.payload = msg->get_payload().data();
    message.payloadLen = static_cast<uint32_t>(msg->get_payload().size());
    message.qos = msg->get_qos();
    message.retained = msg->is_retained();

    if (m_parent->m_recvCallback) {
        m_parent->m_recvCallback(message);
    }
}

void CPahoMqttClient::MqttCallback::delivery_complete(mqtt::delivery_token_ptr token)
{
    bool success = token && token->is_complete();
    std::string info = success ? "Delivery complete" : "Delivery failed";

    if (m_parent->m_publishCallback) {
        m_parent->m_publishCallback(success, info);
    }
}

// CPahoMqttClient类实现
CPahoMqttClient::CPahoMqttClient()
    : m_client(nullptr)
    , m_callback(this)
    , m_connected(false)
{}

CPahoMqttClient::~CPahoMqttClient()
{
    // 直接清理资源
    if (m_client) {
        try {
            // 尝试同步断开连接
            m_client->disconnect()->wait_for(std::chrono::milliseconds(100));
        } catch (...) {
            // 忽略任何异常
        }
        delete m_client;
        m_client = nullptr;
    }
}

bool CPahoMqttClient::init(const std::string &broker, const std::string &clientId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_broker = broker;
        m_clientId = clientId;
    }

    try {
        mqtt::async_client *newClient = new mqtt::async_client(broker, clientId);
        newClient->set_callback(m_callback);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_client) {
                try {
                    m_client->disconnect()->wait_for(std::chrono::milliseconds(100));
                } catch (...) {
                    // 忽略任何异常
                }
                delete m_client;
            }
            m_client = newClient;
        }
        return true;
    } catch (const mqtt::exception &exc) {
        if (m_publishCallback) {
            m_publishCallback(false, std::string("Failed to initialize MQTT client: ") + exc.what());
        }
        return false;
    }
}

void CPahoMqttClient::connect(ConnectCallback callback,
                              const std::string &username,
                              const std::string &password)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connectCallback = std::move(callback);
        m_username = username;
        m_password = password;
    }

    if (!m_client) {
        const std::string info = "MQTT client not initialized, please call init() first";
        if (m_connectCallback) {
            m_connectCallback(false, info);
        }
        return;
    }

    try {
        mqtt::connect_options connOpts;
        connOpts.set_clean_session(true);
        connOpts.set_automatic_reconnect(true); // 默认启用自动重连

        if (!username.empty()) {
            connOpts.set_user_name(username);
            if (!password.empty()) {
                connOpts.set_password(password);
            }
        }

        // 使用异步连接方式
        m_client->connect(connOpts);

    } catch (const mqtt::exception &exc) {
        m_connected.store(false);
        const std::string info = std::string("Failed to connect MQTT: ") + exc.what();
        if (m_connectCallback) {
            m_connectCallback(false, info);
        }
    }
}

bool CPahoMqttClient::disconnect()
{
    if (!m_client) {
        return true;
    }

    try {
        // 使用异步断开连接方式
        m_client->disconnect();
        m_connected.store(false);
        return true;
    } catch (const mqtt::exception &exc) {
        if (m_disconnectCallback) {
            m_disconnectCallback(false, std::string("Failed to disconnect MQTT: ") + exc.what());
        }
        return false;
    }
}

bool CPahoMqttClient::isConnected() const
{
    return m_connected.load();
}

void CPahoMqttClient::subscribe(const std::string &topic, int qos)
{
    if (!m_client) {
        if (m_subscribeCallback) {
            m_subscribeCallback(false, "MQTT client not initialized");
        }
        return;
    }

    if (!m_connected.load()) {
        if (m_subscribeCallback) {
            m_subscribeCallback(false, "MQTT client is not connected");
        }
        return;
    }

    try {
        // 使用异步订阅方式
        m_client->subscribe(topic, qos);

        if (m_subscribeCallback) {
            m_subscribeCallback(true, "Subscribe successful");
        }
    } catch (const mqtt::exception &exc) {
        if (m_subscribeCallback) {
            m_subscribeCallback(false,
                                std::string("Failed to subscribe to MQTT topic: ") + exc.what());
        }
    }
}

void CPahoMqttClient::unsubscribe(const std::string &topic)
{
    if (!m_client) {
        if (m_unsubscribeCallback) {
            m_unsubscribeCallback(false, "MQTT client not initialized");
        }
        return;
    }

    if (!m_connected.load()) {
        if (m_unsubscribeCallback) {
            m_unsubscribeCallback(false, "MQTT client is not connected");
        }
        return;
    }

    try {
        // 使用异步取消订阅方式
        m_client->unsubscribe(topic);

        if (m_unsubscribeCallback) {
            m_unsubscribeCallback(true, "Unsubscribe successful");
        }
    } catch (const mqtt::exception &exc) {
        if (m_unsubscribeCallback) {
            m_unsubscribeCallback(false,
                                  std::string("Failed to unsubscribe from MQTT topic: ")
                                      + exc.what());
        }
    }
}

bool CPahoMqttClient::publish(const std::string &topic,
                              const std::string &payload,
                              int qos,
                              bool retained)
{
    if (!m_client) {
        if (m_publishCallback) {
            m_publishCallback(false, "MQTT client not initialized");
        }
        return false;
    }

    if (!m_connected.load()) {
        if (m_publishCallback) {
            m_publishCallback(false, "MQTT client is not connected");
        }
        return false;
    }

    try {
        // 使用异步发布方式
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload, qos, retained);
        m_client->publish(pubmsg);
        return true;
    } catch (const mqtt::exception &exc) {
        if (m_publishCallback) {
            m_publishCallback(false, std::string("Failed to publish MQTT message: ") + exc.what());
        }
        return false;
    }
}

// 设置回调函数的实现
void CPahoMqttClient::setConnectCallback(ConnectCallback &&callback)
{
    m_connectCallback = std::move(callback);
}

void CPahoMqttClient::setDisconnectCallback(DisconnectCallback &&callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_disconnectCallback = std::move(callback);
}

void CPahoMqttClient::setPublishCallback(PublishCallback &&callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_publishCallback = std::move(callback);
}

void CPahoMqttClient::setSubscribeCallback(SubscribeCallback &&callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subscribeCallback = std::move(callback);
}

void CPahoMqttClient::setUnsubscribeCallback(UnsubscribeCallback &&callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_unsubscribeCallback = std::move(callback);
}

void CPahoMqttClient::setRecvCallback(RecvCallback &&callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recvCallback = std::move(callback);
}
