#include "CPahoMqttClient.h"
#include <iostream>

using namespace Common::Network;

// MQTT回调处理类实现
void CPahoMqttClient::MqttCallback::connected(const std::string &)
{
    std::cout << "MQTT connected callback triggered." << std::endl;
    m_parent->updateConnected(true);
    std::cout << "MQTT client connected successfully." << std::endl;

    // 调用用户提供的连接回调，无需持有锁
    if (m_parent->m_connectCallback) {
        m_parent->m_connectCallback(true);
    }
}

void CPahoMqttClient::MqttCallback::connection_lost(const std::string &)
{
    std::cout << "MQTT connection lost callback triggered." << std::endl;
    m_parent->updateConnected(false);

    // 调用用户提供的连接回调，无需持有锁
    if (m_parent->m_connectCallback) {
        m_parent->m_connectCallback(false);
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

void CPahoMqttClient::MqttCallback::delivery_complete(mqtt::delivery_token_ptr)
{
    std::cout << "MQTT delivery complete callback triggered." << std::endl;
    // 消息发送完成，无需特殊处理
}

// CPahoMqttClient类实现
CPahoMqttClient::CPahoMqttClient()
    : m_client(nullptr)
    , m_callback(this)
    , m_recvCallback(nullptr)
    , m_connectCallback(nullptr)
    , m_connected(false)
{}

CPahoMqttClient::~CPahoMqttClient()
{
    disconnect();
    if (m_client) {
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
                delete m_client;
            }
            m_client = newClient;
        }
        return true;
    } catch (const mqtt::exception &exc) {
        std::cerr << "Failed to initialize MQTT client: " << exc.what() << std::endl;
        return false;
    }
}

void CPahoMqttClient::connect(ConnectCallback callback,
                              const std::string &username,
                              const std::string &password)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connectCallback = callback;
        m_username = username;
        m_password = password;
    }

    if (!m_client) {
        std::cerr << "MQTT client not initialized, please call init() first" << std::endl;
        if (callback) {
            callback(false);
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
        std::cerr << "Failed to connect to MQTT server: " << exc.what() << std::endl;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connected = false;
        }

        if (callback) {
            callback(false);
        }
    }
}

bool CPahoMqttClient::disconnect()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_client && m_connected) {
        try {
            // 使用异步断开连接方式
            m_client->disconnect();
            m_connected = false;
            return true;
        } catch (const mqtt::exception &exc) {
            std::cerr << "Failed to disconnect MQTT: " << exc.what() << std::endl;
            return false;
        }
    } else {
        return true;
    }
}

bool CPahoMqttClient::isConnected() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connected;
}

void CPahoMqttClient::updateConnected(bool connected)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_connected = connected;
}

void CPahoMqttClient::subscribe(const std::string &topic, int qos)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_client) {
        std::cerr << "MQTT client not initialized, please call init() first" << std::endl;
        return;
    }
    if (!m_connected) {
        std::cerr << "MQTT client is not connected, cannot subscribe to topic" << std::endl;
        return;
    }
    try {
        // 使用异步订阅方式
        m_client->subscribe(topic, qos);
    } catch (const mqtt::exception &exc) {
        std::cerr << "Failed to subscribe to MQTT topic: " << exc.what() << std::endl;
    }
}

void CPahoMqttClient::unsubscribe(const std::string &topic)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_client) {
        std::cerr << "MQTT client not initialized, please call init() first" << std::endl;
        return;
    }
    if (!m_connected) {
        std::cerr << "MQTT client is not connected, cannot unsubscribe from topic" << std::endl;
        return;
    }
    try {
        // 使用异步取消订阅方式
        m_client->unsubscribe(topic);
    } catch (const mqtt::exception &exc) {
        std::cerr << "Failed to unsubscribe from MQTT topic: " << exc.what() << std::endl;
    }
}

bool CPahoMqttClient::publish(const std::string &topic,
                              const std::string &payload,
                              int qos,
                              bool retained)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_client) {
        std::cerr << "MQTT client not initialized, please call init() first" << std::endl;
        return false;
    }
    if (!m_connected) {
        std::cerr << "MQTT client is not connected, cannot publish message" << std::endl;
        return false;
    }

    try {
        // 使用异步发布方式
        mqtt::message_ptr pubmsg = mqtt::make_message(topic, payload, qos, retained);
        m_client->publish(pubmsg);
        return true;
    } catch (const mqtt::exception &exc) {
        std::cerr << "Failed to publish MQTT message: " << exc.what() << std::endl;
        return false;
    }
}

void CPahoMqttClient::setRecvCallback(RecvCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_recvCallback = std::move(callback);
}
