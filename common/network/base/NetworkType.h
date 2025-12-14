#ifndef NETWORKTYPE_H
#define NETWORKTYPE_H

// 网络类型枚举
enum class NetworkType {
    TCP, // TCP
    MQTT // MQTT
};

// 连接状态枚举
enum class NetworkState {
    DISCONNECTED, // 未连接
    CONNECTING,   // 连接中
    CONNECTED,    // 已连接
    DISCONNECTING // 断开中
};

// 网络错误码枚举
enum class NetworkErrorCode {
    SUCCESS,           // 成功
    CONNECTION_FAILED, // 连接失败
    SEND_FAILED,       // 发送失败
    RECEIVE_FAILED,    // 接收失败
    NOT_CONNECTED,     // 未连接
    INVALID_PARAMETER, // 参数无效
    INTERNAL_ERROR     // 内部错误
};

#endif // NETWORKTYPE_H
