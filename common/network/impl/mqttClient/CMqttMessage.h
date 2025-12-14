#ifndef CMQTTMESSAGE_H
#define CMQTTMESSAGE_H

#include <cstdint>
#include <string>

namespace Common {
namespace Network {

struct CMqttMessage
{
    std::string topic;   // MQTT主题
    const char* payload; // 消息载荷
    uint32_t payloadLen; // 载荷长度
    int qos;             // QoS等级
    bool retained;       // 保留位
};

} // namespace Network
} // namespace Common

#endif // CMQTTMESSAGE_H
