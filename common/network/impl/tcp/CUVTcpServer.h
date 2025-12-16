#ifndef CUVTCPSERVER_H
#define CUVTCPSERVER_H

#include "common/network/base/CUVLoop.h"
#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>

/**
 * @brief 客户端地址结构体
 */
struct Address
{
    std::string ip = "";
    int port = 0;

    std::string toString() const { return ip + ":" + std::to_string(port); }

    // 比较运算符，用于unordered_map的键
    bool operator==(const Address& other) const { return ip == other.ip && port == other.port; }
};

// 在全局命名空间中特化 std::hash 用于 Address 结构体
namespace std {
template<>
struct hash<Address>
{
    size_t operator()(const Address& addr) const noexcept
    {
        size_t seed = 0;
        seed ^= std::hash<std::string>{}(addr.ip) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= std::hash<int>{}(addr.port) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};
} // namespace std

namespace Common {
namespace Network {

class CUVTcpServer
{
private:
    // 回调函数定义
    static void onNewConnection(uv_stream_t* server, int status);
    static void onClientDisconnect(uv_handle_t* handle);
    static void onClientRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf);
    static void onSend(uv_write_t* req, int status);
    static void onReceiveTimeout(uv_timer_t* handle);

    // 辅助函数
    template<typename Func>
    void postTask(Func&& func) const;
    bool isLoopValid() const;
    void closeClientConnection(uv_tcp_t* clientHandle) const;
    static void deleteClientHandle(uv_tcp_t* clientHandle);
    static void deleteTimeoutTimer(uv_timer_t* timeoutTimer);

public:
    // 服务器状态枚举
    enum class ServerState {
        STOPPED,  // 已停止
        STARTING, // 启动中
        RUNNING,  // 运行中
        STOPPING  // 停止中
    };

    // 客户端上下文结构体，用于存储在libuv句柄的data字段中
    struct ClientContext
    {
        CUVTcpServer* server;
        Address addr;
        uv_tcp_t* clientHandle;
        uv_timer_t* timeoutTimer;
    };

    // 客户端信息结构体
    using ClientInfo = struct
    {
        uv_tcp_t* handle;
        uv_timer_t* timeoutTimer;
    };

    // 回调类型定义
    using ServerStartCallback
        = std::function<void(bool success, const std::string& info)>;        // 服务器启动回调
    using ServerStopCallback = std::function<void(const std::string& info)>; // 服务器停止回调
    using ClientConnectCallback = std::function<
        void(const Address& clientAddr, bool success, const std::string& error)>; // 客户端连接回调
    using ClientDisconnectCallback = std::function<void(const Address& clientAddr)>; // 客户端断开回调
    using ClientReceiveCallback = std::function<void(const Address& clientAddr,
                                                     const std::string& data)>; // 客户端接收数据回调
    using SendCallback = std::function<
        void(const Address& clientAddr, bool success, const std::string& error)>; // 发送回调

    using ReceiveTimeoutCallback = std::function<void(const Address& clientAddr)>; // 接收超时回调

public:
    /**
     * @brief 构造函数
     */
    explicit CUVTcpServer();

    /**
     * @brief 析构函数
     */
    ~CUVTcpServer();

    // 禁止拷贝和移动
    CUVTcpServer(const CUVTcpServer&) = delete;
    CUVTcpServer& operator=(const CUVTcpServer&) = delete;
    CUVTcpServer(CUVTcpServer&&) = delete;
    CUVTcpServer& operator=(CUVTcpServer&&) = delete;

public:
    /**
     * @brief 启动服务器并监听指定地址和端口
     * @param host 监听地址
     * @param port 监听端口
     */
    void listen(const std::string& host, int port);

    /**
     * @brief 停止服务器
     */
    void stop();

    /**
     * @brief 发送数据到指定客户端
     * @param clientAddr 客户端地址
     * @param data 要发送的数据
     */
    void send(const Address& clientAddr, const std::string& data);

    /**
     * @brief 获取服务器当前状态
     * @return 服务器状态
     */
    ServerState getState() const;

    /**
     * @brief 设置接收超时间隔
     * @param intervalMs 超时间隔，单位毫秒
     */
    void setReceiveTimeoutInterval(int intervalMs);

    void setStartCallback(ServerStartCallback&& callback);             // 设置服务器启动回调
    void setStopCallback(ServerStopCallback&& callback);               // 设置服务器停止回调
    void setConnectCallback(ClientConnectCallback&& callback);         // 设置客户端连接回调
    void setDisconnectCallback(ClientDisconnectCallback&& callback);   // 设置客户端断开回调
    void setReceiveCallback(ClientReceiveCallback&& callback);         // 设置客户端接收数据回调
    void setSendCallback(SendCallback&& callback);                     // 设置发送回调
    void setReceiveTimeoutCallback(ReceiveTimeoutCallback&& callback); // 设置接收超时回调

private:
    CUVLoop* m_loop;                  // 事件循环
    uv_tcp_t* m_serverHandle;         // 服务器TCP句柄
    std::atomic<ServerState> m_state; // 服务器状态

    Address m_listenAddress; // 监听地址
    size_t m_maxConnections; // 最大连接数

    int m_receiveTimeoutInterval; // 接收超时间隔

    std::unordered_map<Address, ClientInfo> m_clients; // 客户端列表

    // 回调函数
    ServerStartCallback m_serverStartCallback;           // 服务器启动回调
    ServerStopCallback m_serverStopCallback;             // 服务器停止回调
    ClientConnectCallback m_clientConnectCallback;       // 客户端连接回调
    ClientDisconnectCallback m_clientDisconnectCallback; // 客户端断开回调
    ClientReceiveCallback m_clientReceiveCallback;       // 客户端接收数据回调
    SendCallback m_sendCallback;                         // 发送回调
    ReceiveTimeoutCallback m_receiveTimeoutCallback;     // 接收超时回调
};

} // namespace Network
} // namespace Common

#endif // CUVTCPSERVER_H
