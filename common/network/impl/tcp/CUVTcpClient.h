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
    // 回调函数定义
    static void onConnect(uv_connect_t* req, int status);
    static void onDisconnect(uv_handle_t* handle);
    static void onSend(uv_write_t* req, int status);
    static void onReceive(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);

    // 重连定时器控制
    void startReconnectTimer();
    void stopReconnectTimer();

    // 接收超时定时器控制
    void startReceiveTimeoutTimer();
    void stopReceiveTimeoutTimer();

    // 辅助函数
    template<typename Func>
    void postTask(Func&& func) const;
    inline bool isLoopValid() const { return m_loop != nullptr; }
    
    // 通用定时器管理函数
    template<typename CallbackFunc>
    bool startTimer(uv_timer_t** timer_ptr, int interval_ms, CallbackFunc callback,
                   const std::string& init_error_msg, const std::string& start_error_msg,
                   std::function<void(const std::string&)>& timer_callback);

public:
    // 连接状态
    enum class ConnectState { DISCONNECTED, CONNECTING, CONNECTED };

    // 回调类型定义
    using ConnectCallback = std::function<void(bool success, const std::string& error)>; // 连接回调
    using DisconnectCallback = std::function<void(bool success, const std::string& error)>; // 断开回调
    using ReceiveCallback = std::function<void(const char* data, size_t length)>; // 接收数据回调
    using SendCallback = std::function<void(bool success, const std::string& error)>; // 发送回调
    using ReconnectCallback = std::function<void(const std::string& error)>;          // 重连回调
    using TimeoutCallback = std::function<void(const std::string& error)>;            // 超时回调

    /**
     * @brief 构造函数
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
    void setReceiveCallback(ReceiveCallback&& callback);
    void setConnectCallback(ConnectCallback&& callback);
    void setDisconnectCallback(DisconnectCallback&& callback);
    void setReconnectCallback(ReconnectCallback&& callback);

    // 配置重连机制
    void setReconnectInterval(int initialIntervalMs = 1000, int maxIntervalMs = 30000);

    // 设置接收超时
    void setReceiveTimeout(int timeoutMs, TimeoutCallback callback);

private:
    CUVLoop* m_loop;       // 事件循环
    uv_tcp_t* m_tcpHandle; // TCP句柄

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
    ReconnectCallback m_reconnectCallback;    // 重连回调
};

} // namespace Network
} // namespace Common

#endif // CUVTCPCLIENT_H
