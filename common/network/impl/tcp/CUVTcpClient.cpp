#include "CUVTcpClient.h"
#include "common/network/base/CUVLoop.h"
#include <cstring>
// #include <iostream>
#include <uv.h>

using namespace Common::Network;

// 连接请求数据结构
struct ConnectRequest
{
    CUVTcpClient* client;
    std::string host;
    int port;
};

// 发送请求数据结构
struct SendRequest
{
    CUVTcpClient* client;
    std::string data;
    CUVTcpClient::SendCallback callback;
};

// 删除定时器
void deleteTimer(uv_timer_t* timer)
{
    if (timer && !uv_is_closing(reinterpret_cast<uv_handle_t*>(timer))) {
        uv_timer_stop(timer);
        uv_close(reinterpret_cast<uv_handle_t*>(timer),
                 [](uv_handle_t* handle) { delete reinterpret_cast<uv_timer_t*>(handle); });
    }
}

// 构造函数
CUVTcpClient::CUVTcpClient()
    : m_loop(CUVLoop::getInstance())
    , m_tcpHandle(nullptr)
    , m_state(ConnectState::DISCONNECTED)
    , m_host("")
    , m_port(0)
    , m_reconnectTimer(nullptr)
    , m_reconnectInterval(1000)
    , m_initialReconnectInterval(1000)
    , m_maxReconnectInterval(30000)
    , m_receiveTimeoutTimer(nullptr)
    , m_receiveTimeoutInterval(0)
{}

// 析构函数
CUVTcpClient::~CUVTcpClient()
{
    // 清理TCP句柄
    if (m_tcpHandle) {
        auto tcpHandle = m_tcpHandle;
        m_tcpHandle = nullptr;

        if (isLoopValid()) {
            postTask([tcpHandle]() {
                uv_read_stop(reinterpret_cast<uv_stream_t*>(tcpHandle));
                uv_close(reinterpret_cast<uv_handle_t*>(tcpHandle),
                         [](uv_handle_t* handle) { delete reinterpret_cast<uv_tcp_t*>(handle); });
            });
        } else {
            delete tcpHandle;
        }
    }
    // 清理重连定时器
    if (m_reconnectTimer) {
        auto reconnectTimer = m_reconnectTimer;
        m_reconnectTimer = nullptr;

        if (isLoopValid()) {
            postTask([reconnectTimer]() {
                deleteTimer(reconnectTimer);
            });
        } else {
            delete reconnectTimer;
        }
    }
    // 清理接收超时定时器
    if (m_receiveTimeoutTimer) {
        auto receiveTimeoutTimer = m_receiveTimeoutTimer;
        m_receiveTimeoutTimer = nullptr;

        if (isLoopValid()) {
            postTask([receiveTimeoutTimer]() {
                deleteTimer(receiveTimeoutTimer);
            });
        } else {
            delete receiveTimeoutTimer;
        }
    }

    // std::cout << "CUVTcpClient destroyed." << std::endl;
}

// 连接服务器
void CUVTcpClient::connect(const std::string& host, int port)
{
    if (!isLoopValid()) {
        if (m_connectCallback) {
            m_connectCallback(false, "Invalid loop");
        }
        return;
    }

    // 确保在事件循环线程中执行连接操作
    postTask([this, host, port]() {
        // 检查当前状态
        if (m_state.load() != ConnectState::DISCONNECTED) {
            if (m_connectCallback) {
                m_connectCallback(false, "Client is not in disconnected state");
            }
            return;
        }

        // 更新状态和连接信息
        m_state.store(ConnectState::CONNECTING);
        m_host = host;
        m_port = port;

        // 创建并初始化TCP句柄
        if (!m_tcpHandle) {
            m_tcpHandle = new uv_tcp_t;
            std::memset(m_tcpHandle, 0, sizeof(uv_tcp_t));
            m_tcpHandle->data = this;

            if (uv_tcp_init(m_loop->getLoop(), m_tcpHandle) != 0) {
                m_state.store(ConnectState::DISCONNECTED);
                if (m_connectCallback) {
                    m_connectCallback(false, "Failed to initialize TCP handle");
                }
                delete m_tcpHandle;
                m_tcpHandle = nullptr;
                return;
            }
        }

        // 创建连接请求
        ConnectRequest* req_data = new ConnectRequest;
        req_data->client = this;
        req_data->host = host;
        req_data->port = port;

        // 解析地址
        struct sockaddr_in addr;
        int result = uv_ip4_addr(host.c_str(), port, &addr);
        if (result != 0) {
            m_state.store(ConnectState::DISCONNECTED);
            delete req_data;
            if (m_connectCallback) {
                m_connectCallback(false,
                                  "Failed to parse address: " + std::string(uv_strerror(result)));
            }
            return;
        }

        // 创建连接请求
        uv_connect_t* req = new uv_connect_t;
        req->data = req_data;

        // 开始连接
        result = uv_tcp_connect(req,
                                m_tcpHandle,
                                reinterpret_cast<const struct sockaddr*>(&addr),
                                CUVTcpClient::onConnect);
        if (result != 0) {
            delete req;
            delete req_data;

            // 更新状态为DISCONNECTED
            m_state.store(ConnectState::DISCONNECTED);

            if (m_connectCallback) {
                m_connectCallback(false, "Failed to connect: " + std::string(uv_strerror(result)));
            }

            // 启动重连定时器
            startReconnectTimer();
            return;
        }
    });
}

// 断开连接
void CUVTcpClient::disconnect()
{
    // 确保在事件循环线程中执行断开操作
    if (!isLoopValid()) {
        if (m_disconnectCallback) {
            m_disconnectCallback(false, "Invalid loop");
        }
        return;
    }

    // 检查当前状态
    if (m_state.load() == ConnectState::DISCONNECTED) {
        if (m_disconnectCallback) {
            m_disconnectCallback(true, "Client already disconnected");
        }
        return;
    }
    m_state.store(ConnectState::DISCONNECTED);

    // 保存当前句柄和定时器指针，避免在回调中被修改
    auto tcpHandle = m_tcpHandle;
    m_tcpHandle = nullptr;
    auto receiveTimeoutTimer = m_receiveTimeoutTimer;
    m_receiveTimeoutTimer = nullptr;
    auto reconnectTimer = m_reconnectTimer;
    m_reconnectTimer = nullptr;
    // 在事件循环线程中执行断开操作
    postTask([tcpHandle, receiveTimeoutTimer, reconnectTimer]() {
        // 关闭接收超时定时器
        deleteTimer(receiveTimeoutTimer);

        // 停止重连定时器，避免重复连接
        deleteTimer(reconnectTimer);

        // 关闭TCP连接
        if (tcpHandle) {
            uv_read_stop(reinterpret_cast<uv_stream_t*>(tcpHandle));
            uv_close(reinterpret_cast<uv_handle_t*>(tcpHandle), onDisconnect);
        }
    });
}

// 获取连接状态
CUVTcpClient::ConnectState CUVTcpClient::getState() const
{
    return m_state.load();
}

// 发送数据（char*版本）
void CUVTcpClient::send(const char* data, size_t length, SendCallback&& callback)
{
    if (!data || length == 0) {
        callback(false, "Invalid data");
        return;
    }

    send(std::string(data, length), std::move(callback));
}

// 发送数据（string版本）
void CUVTcpClient::send(const std::string& data, SendCallback&& sendCallback)
{
    if (data.empty()) {
        if (sendCallback) {
            sendCallback(false, "Empty data");
        }
        return;
    }

    if (!isLoopValid()) {
        if (sendCallback) {
            sendCallback(false, "Invalid loop");
        }
        return;
    }

    // 确保在事件循环线程中执行发送操作
    postTask([this, data, callback = std::move(sendCallback)]() {
        if (m_state.load() != ConnectState::CONNECTED) {
            if (callback) {
                callback(false, "Client is not connected");
            }
            return;
        }

        // 检查写队列大小
        if (uv_stream_get_write_queue_size(reinterpret_cast<uv_stream_t*>(m_tcpHandle))
            > 1024 * 1024) {
            std::string error_msg = "Write queue is too large, reconnecting...";
            if (callback) {
                callback(false, error_msg);
            }

            // 断开连接并启动重连定时器
            disconnect();
            startReconnectTimer();
            return;
        }

        // 创建发送请求
        SendRequest* req_data = new SendRequest;
        req_data->client = this;
        req_data->data = data;
        req_data->callback = std::move(callback);

        // 创建uv_write_t请求
        uv_write_t* req = new uv_write_t;
        req->data = req_data;

        uv_buf_t buf = uv_buf_init(const_cast<char*>(req_data->data.data()),
                                   (ULONG) req_data->data.size());
        int result = uv_write(req,
                              reinterpret_cast<uv_stream_t*>(m_tcpHandle),
                              &buf,
                              1,
                              CUVTcpClient::onSend);
        if (result != 0) {
            if (req_data->callback) {
                req_data->callback(false,
                                   "Failed to initiate send: " + std::string(uv_strerror(result)));
            }
            delete req;
            delete req_data;
        }
    });
}

// ==================== 回调设置方法 ====================

// 设置接收回调
void CUVTcpClient::setReceiveCallback(ReceiveCallback&& callback)
{
    m_receiveCallback = std::move(callback);
}

// 设置连接回调
void CUVTcpClient::setConnectCallback(ConnectCallback&& callback)
{
    m_connectCallback = std::move(callback);
}

// 设置断开回调
void CUVTcpClient::setDisconnectCallback(DisconnectCallback&& callback)
{
    m_disconnectCallback = std::move(callback);
}

void CUVTcpClient::setReconnectCallback(ReconnectCallback&& callback)
{
    m_reconnectCallback = std::move(callback);
}

// 设置重连间隔
void CUVTcpClient::setReconnectInterval(int initialIntervalMs, int maxIntervalMs)
{
    if (initialIntervalMs > 0 && maxIntervalMs >= initialIntervalMs) {
        m_initialReconnectInterval = initialIntervalMs;
        m_maxReconnectInterval = maxIntervalMs;
        m_reconnectInterval = initialIntervalMs;
    }
}

// 设置接收超时
void CUVTcpClient::setReceiveTimeout(int timeoutMs, TimeoutCallback callback)
{
    m_receiveTimeoutInterval = timeoutMs;
    m_receiveTimeoutCallback = callback;
    if (timeoutMs <= 0) {
        // 停止接收超时定时器
        stopReceiveTimeoutTimer();
    } else {
        // 重置接收超时定时器
        stopReceiveTimeoutTimer();
        startReceiveTimeoutTimer();
    }
}

// ==================== 重连定时器相关方法 ====================

void CUVTcpClient::startReconnectTimer()
{
    if (!isLoopValid()) {
        return;
    }

    postTask([this]() {
        if (m_state.load() != ConnectState::DISCONNECTED) {
            return;
        }

        // 使用通用定时器管理函数启动重连定时器
        startTimer(
            &m_reconnectTimer,
            m_reconnectInterval,
            [](uv_timer_t* handle) {
                CUVTcpClient* client = static_cast<CUVTcpClient*>(handle->data);

                if (client->m_state.load() == ConnectState::DISCONNECTED) {
                    if (client->m_reconnectCallback) {
                        client->m_reconnectCallback("Attempting to reconnect to " + client->m_host
                                                    + ":" + std::to_string(client->m_port));
                    }
                    client->connect(client->m_host, client->m_port);
                }
            },
            "Failed to initialize reconnect timer: ",
            "Failed to start reconnect timer: ",
            m_reconnectCallback);
    });
}

void CUVTcpClient::stopReconnectTimer()
{
    if (!isLoopValid() || !m_reconnectTimer) {
        return;
    }

    postTask([this]() {
        if (m_reconnectTimer) {
            uv_timer_stop(m_reconnectTimer);
        }
    });
}

// ==================== 接收超时定时器相关方法 ====================

void CUVTcpClient::startReceiveTimeoutTimer()
{
    if (!isLoopValid() || m_receiveTimeoutInterval <= 0) {
        if (m_receiveTimeoutCallback) {
            m_receiveTimeoutCallback("Receive timeout not set or invalid loop.");
        }
        return;
    }

    postTask([this]() {
        if (m_state.load() != ConnectState::CONNECTED) {
            if (m_receiveTimeoutCallback) {
                m_receiveTimeoutCallback("Client is not connected.");
            }
            return;
        }

        // 使用通用定时器管理函数启动接收超时定时器
        startTimer(
            &m_receiveTimeoutTimer,
            m_receiveTimeoutInterval,
            [](uv_timer_t* handle) {
                CUVTcpClient* client = static_cast<CUVTcpClient*>(handle->data);

                // 调用超时回调
                if (client->m_receiveTimeoutCallback) {
                    client->m_receiveTimeoutCallback("Receive timeout occurred.");
                }

                // 断开连接并尝试重新连接
                client->disconnect();
                client->startReconnectTimer();
            },
            "Failed to initialize receive timeout timer: ",
            "Failed to start receive timeout timer: ",
            m_receiveTimeoutCallback);
    });
}

void CUVTcpClient::stopReceiveTimeoutTimer()
{
    if (!isLoopValid() || !m_receiveTimeoutTimer) {
        return;
    }

    postTask([this]() {
        if (m_receiveTimeoutTimer) {
            uv_timer_stop(m_receiveTimeoutTimer);
        }
    });
}

// ==================== 辅助函数实现 ====================

// 向事件循环发布任务
template<typename Func>
void CUVTcpClient::postTask(Func&& func) const
{
    if (isLoopValid()) {
        m_loop->postTask(std::forward<Func>(func));
    }
}

// 通用定时器管理函数
template<typename CallbackFunc>
bool CUVTcpClient::startTimer(uv_timer_t** timer_ptr,
                              int interval_ms,
                              CallbackFunc callback,
                              const std::string& init_error_msg,
                              const std::string& start_error_msg,
                              std::function<void(const std::string&)>& timer_callback)
{
    if (!timer_ptr)
        return false;

    // 初始化定时器
    if (!*timer_ptr) {
        *timer_ptr = new uv_timer_t;
        std::memset(*timer_ptr, 0, sizeof(uv_timer_t));
        (*timer_ptr)->data = this;

        if (uv_timer_init(m_loop->getLoop(), *timer_ptr) != 0) {
            delete *timer_ptr;
            *timer_ptr = nullptr;
            if (timer_callback) {
                timer_callback(init_error_msg + "failed");
            }
            return false;
        }
    }

    // 先停止定时器，确保状态正确
    uv_timer_stop(*timer_ptr);

    // 启动定时器
    if (uv_timer_start(*timer_ptr, callback, interval_ms, 0) != 0) {
        if (timer_callback) {
            timer_callback(start_error_msg + "failed");
        }
        return false;
    }

    return true;
}

// ==================== 静态回调函数实现 ====================

// 连接回调处理
void CUVTcpClient::onConnect(uv_connect_t* req, int status)
{
    ConnectRequest* req_data = static_cast<ConnectRequest*>(req->data);
    CUVTcpClient* client = req_data->client;
    bool success = (status == 0);
    std::string error = success ? "" : uv_strerror(status);

    if (!success) {
        client->m_state.store(ConnectState::DISCONNECTED);
        // 连接失败，增加重连间隔
        client->m_reconnectInterval = (std::min) (client->m_reconnectInterval * 2,
                                                  client->m_maxReconnectInterval);
        // 启动重连定时器
        client->startReconnectTimer();
    } else {
        client->m_state.store(ConnectState::CONNECTED);
        client->m_reconnectInterval = client->m_initialReconnectInterval;

        // 开始接收数据
        uv_read_start(
            reinterpret_cast<uv_stream_t*>(client->m_tcpHandle),
            [](uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
                buf->base = new char[suggested_size];
                buf->len = (ULONG) suggested_size;
            },
            CUVTcpClient::onReceive);

        // 重置接收超时定时器
        client->stopReceiveTimeoutTimer();
        client->startReceiveTimeoutTimer();
    }

    // 调用用户回调
    if (client->m_connectCallback) {
        client->m_connectCallback(success, error);
    }

    // 清理资源
    delete req_data;
    delete req;
}

// 断开回调处理
void CUVTcpClient::onDisconnect(uv_handle_t* handle)
{
    auto client = static_cast<CUVTcpClient*>(handle->data);

    // 调用用户断开回调
    if (client->m_disconnectCallback) {
        client->m_disconnectCallback(true, "Disconnected successfully");
    }

    // 清理TCP句柄
    delete reinterpret_cast<uv_tcp_t*>(handle);
}

// 发送回调处理
void CUVTcpClient::onSend(uv_write_t* req, int status)
{
    SendRequest* req_data = static_cast<SendRequest*>(req->data);

    // 调用用户回调
    if (req_data->callback) {
        std::string error = (status != 0) ? uv_strerror(status) : "";
        req_data->callback(status == 0, error);
    }

    // 清理资源
    delete req_data;
    delete req;
}

// 接收回调处理
void CUVTcpClient::onReceive(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    CUVTcpClient* client = static_cast<CUVTcpClient*>(stream->data);

    if (nread > 0) {
        if (client->m_receiveCallback) {
            client->m_receiveCallback(buf->base, nread);
        }
        // 重置接收超时定时器
        client->stopReceiveTimeoutTimer();
        client->startReceiveTimeoutTimer();
    } else if (nread < 0) {
        client->disconnect();
        client->startReconnectTimer();
    }

    // 释放缓冲区
    delete[] buf->base;
}
