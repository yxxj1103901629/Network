#ifndef CUVTCPCLIENT_H
#define CUVTCPCLIENT_H

#include "common/network/base/CUVLoop.h"
#include <functional>
#include <string>

namespace Common {
namespace Network {

class CUVTcpClient
{
private:
    class TcpClientCallback
    {
    public:
        TcpClientCallback(CUVTcpClient* parent)
            : m_parent(parent)
        {}
        // 回调函数定义
        void onConnect(uv_connect_t* req, int status);
        void onDisconnect(uv_handle_t* handle);
        void onSend(uv_write_t* req, int status);
        void onReceive(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

    private:
        CUVTcpClient* m_parent;
    };

    // 重连定时器控制
    void startReconnectTimer();
    void stopReconnectTimer();

    // 接收超时定时器控制
    void startReceiveTimeoutTimer();
    void stopReceiveTimeoutTimer();

public:
    // 连接状态
    enum class ConnectState { DISCONNECTED, CONNECTING, CONNECTED };

    // 回调类型定义
    using ConnectCallback = std::function<void(bool success, const std::string& error)>; // 连接回调
    using DisconnectCallback = std::function<void()>;                                    // 断开回调
    using ReceiveCallback = std::function<void(const char* data, size_t length)>; // 接收数据回调
    using SendCallback = std::function<void(bool success, const std::string& error)>; // 发送回调
    using TimeoutCallback = std::function<void()>;                                    // 超时回调

    /**
     * @brief 构造函数
     * @param loop CUVLoop实例（可选，默认使用全局单例）
     */
    explicit CUVTcpClient();
    ~CUVTcpClient();

    // 禁止拷贝和移动
    CUVTcpClient(const CUVTcpClient&) = delete;
    CUVTcpClient& operator=(const CUVTcpClient&) = delete;
    CUVTcpClient(CUVTcpClient&&) = delete;
    CUVTcpClient& operator=(CUVTcpClient&&) = delete;

public:
    // 连接服务器
    void connect(const std::string& host, int port);
    // 断开连接
    void disconnect();
    // 获取连接状态
    ConnectState getState() const;

    // 发送数据
    void send(const char* data, size_t length, SendCallback&& callback = nullptr);
    void send(const std::string& data, SendCallback&& callback = nullptr);

    // 设置回调
    void setReceiveCallback(ReceiveCallback callback);
    void setConnectCallback(ConnectCallback callback);
    void setDisconnectCallback(DisconnectCallback callback);

    // 配置重连机制
    void setReconnectInterval(int initialIntervalMs = 1000, int maxIntervalMs = 30000);

    // 设置接收超时
    void setReceiveTimeout(int timeoutMs, TimeoutCallback callback);

private:
    CUVLoop* m_loop;              // 事件循环
    uv_tcp_t* m_tcpHandle;        // TCP句柄
    TcpClientCallback m_callback; // 内部回调处理

    std::atomic<ConnectState> m_state; // 连接状态
    std::string m_host;                // 服务器地址
    int m_port;                        // 服务器端口

    uv_timer_t* m_reconnectTimer;   // 重连定时器
    int m_reconnectInterval;        // 当前重连间隔
    int m_initialReconnectInterval; // 初始重连间隔
    int m_maxReconnectInterval;     // 最大重连间隔

    uv_timer_t* m_receiveTimeoutTimer; // 接收超时定时器
    int m_receiveTimeoutInterval;      // 接收超时间隔

    ConnectCallback m_connectCallback;        // 连接回调
    DisconnectCallback m_disconnectCallback;  // 断开回调
    ReceiveCallback m_receiveCallback;        // 接收数据回调
    TimeoutCallback m_receiveTimeoutCallback; // 接收超时回调
};

} // namespace Network
} // namespace Common

#endif // CUVTCPCLIENT_H
