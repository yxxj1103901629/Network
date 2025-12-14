#include "CUVTcpClient.h"
#include "common/network/base/CUVLoop.h"
#include <cstring>
#include <iostream>
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

// 构造函数
CUVTcpClient::CUVTcpClient()
    : m_loop(CUVLoop::getInstance())
    , m_tcpHandle(nullptr)
    , m_callback(this)
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
    // 延迟清理TCP句柄，确保在事件循环线程中执行
    if (m_tcpHandle) {
        auto tcpHandle = m_tcpHandle;
        m_tcpHandle = nullptr;

        if (m_loop) {
            m_loop->postTask([tcpHandle]() {
                uv_read_stop(reinterpret_cast<uv_stream_t*>(tcpHandle));
                uv_close(reinterpret_cast<uv_handle_t*>(tcpHandle), [](uv_handle_t* handle) {
                    // 清理TCP句柄
                    delete reinterpret_cast<uv_tcp_t*>(handle);
                    handle = nullptr;
                });
            });
        } else {
            // 如果没有事件循环，直接清理
            delete tcpHandle;
            tcpHandle = nullptr;
        }
    }

    // 清除重连定时器
    stopReconnectTimer();

    // 清除接收超时定时器
    stopReceiveTimeoutTimer();

    std::cout << "CUVTcpClient: Destructor called, resources cleaned up." << std::endl;
}

// 连接服务器
void CUVTcpClient::connect(const std::string& host, int port)
{
    if (!m_loop) {
        if (m_connectCallback) {
            m_connectCallback(false, "Invalid loop");
        }
        return;
    }

    // 确保在事件循环线程中执行连接操作
    m_loop->postTask([this, host, port]() {
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

        // 创建TCP句柄
        if (!m_tcpHandle) {
            m_tcpHandle = new uv_tcp_t;
            std::memset(m_tcpHandle, 0, sizeof(uv_tcp_t));
            m_tcpHandle->data = this;

            // 初始化TCP句柄
            int result = uv_tcp_init(m_loop->getLoop(), m_tcpHandle);
            if (result != 0) {
                m_state.store(ConnectState::DISCONNECTED);
                if (m_connectCallback) {
                    m_connectCallback(false,
                                      "Failed to initialize TCP handle: "
                                          + std::string(uv_strerror(result)));
                }
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
                                [](uv_connect_t* req, int status) {
                                    ConnectRequest* req_data = static_cast<ConnectRequest*>(
                                        req->data);
                                    CUVTcpClient* client = req_data->client;
                                    client->m_callback.onConnect(req, status);
                                });
        if (result != 0) {
            delete req;
            delete req_data;
            if (m_connectCallback) {
                m_connectCallback(false, "Failed to connect: " + std::string(uv_strerror(result)));
            }

            m_state.store(ConnectState::DISCONNECTED);
            // 启动重连定时器
            startReconnectTimer();
            return;
        }
    });
}

// 断开连接
void CUVTcpClient::disconnect()
{
    if (!m_loop) {
        std::cout << "CUVTcpClient: Invalid loop, cannot disconnect." << std::endl;
        return;
    }

    if (!m_tcpHandle) {
        std::cout << "CUVTcpClient: No active connection to disconnect." << std::endl;
        return;
    }

    auto tcpHandle = m_tcpHandle;
    m_tcpHandle = nullptr;

    m_loop->postTask([this, tcpHandle]() {
        if (m_state.load() == ConnectState::DISCONNECTED) {
            return;
        }
        // 更新状态
        m_state.store(ConnectState::DISCONNECTED);

        if (!uv_is_closing(reinterpret_cast<uv_handle_t*>(tcpHandle))) {
            uv_read_stop(reinterpret_cast<uv_stream_t*>(tcpHandle));
            uv_close(reinterpret_cast<uv_handle_t*>(tcpHandle), [](uv_handle_t* handle) {
                if (CUVTcpClient* client = static_cast<CUVTcpClient*>(handle->data)) {
                    client->m_callback.onDisconnect(handle);
                }
            });
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
        if (callback) {
            callback(false, "Invalid data");
        }
        return;
    }

    std::string str_data(data, length);
    send(str_data, std::move(callback));
}

// 发送数据（string版本）
void CUVTcpClient::send(const std::string& data, SendCallback&& callback)
{
    if (data.empty()) {
        if (callback) {
            callback(false, "Empty data");
        }
        return;
    }

    if (!m_loop) {
        if (callback) {
            callback(false, "Invalid loop");
        }
        return;
    }

    // 确保在事件循环线程中执行发送操作
    m_loop->postTask([this, data, callback]() {
        if (m_state.load() != ConnectState::CONNECTED) {
            if (callback) {
                callback(false, "Client is not connected");
            }
            return;
        }

        // 检查写队列大小
        if (uv_stream_get_write_queue_size(reinterpret_cast<uv_stream_t*>(m_tcpHandle))
            > 1024 * 1024) { // 1MB限制
            // 写队列过大，重新连接
            if (callback) {
                callback(false, "Write queue is too large, reconnecting...");
            }

            // 断开当前连接
            disconnect();
            // 启动重连定时器
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
                              [](uv_write_t* req, int status) {
                                  SendRequest* req_data = static_cast<SendRequest*>(req->data);
                                  CUVTcpClient* client = req_data->client;
                                  client->m_callback.onSend(req, status);
                              });
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
void CUVTcpClient::setReceiveCallback(ReceiveCallback callback)
{
    m_receiveCallback = callback;
}

// 设置连接回调
void CUVTcpClient::setConnectCallback(ConnectCallback callback)
{
    m_connectCallback = callback;
}

// 设置断开回调
void CUVTcpClient::setDisconnectCallback(DisconnectCallback callback)
{
    m_disconnectCallback = callback;
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
}

// ==================== 重连定时器相关方法 ====================

void CUVTcpClient::startReconnectTimer()
{
    if (!m_loop) {
        return;
    }

    m_loop->postTask([this]() {
        if (m_state.load() != ConnectState::DISCONNECTED) {
            return;
        }

        // 初始化重连定时器
        if (!m_reconnectTimer) {
            m_reconnectTimer = new uv_timer_t;
            std::memset(m_reconnectTimer, 0, sizeof(uv_timer_t));
            m_reconnectTimer->data = this;
            if (int result = uv_timer_init(m_loop->getLoop(), m_reconnectTimer); result != 0) {
                delete m_reconnectTimer;
                m_reconnectTimer = nullptr;
                std::cerr << "CUVTcpClient: Failed to initialize reconnect timer: "
                          << uv_strerror(result) << std::endl;
                return;
            }
        };

        // 启动重连定时器
        uv_timer_start(
            m_reconnectTimer,
            [](uv_timer_t* handle) {
                CUVTcpClient* client = static_cast<CUVTcpClient*>(handle->data);

                std::cout << "CUVTcpClient: Attempting to reconnect to " << client->m_host << ":"
                          << client->m_port << " ..." << std::endl;

                client->connect(client->m_host, client->m_port);
            },
            m_reconnectInterval, // 延迟
            0);                  // 不重复
    });
}

void CUVTcpClient::stopReconnectTimer()
{
    if (!m_loop || !m_reconnectTimer) {
        return;
    }

    m_loop->postTask([this]() {
        if (m_reconnectTimer) {
            auto reconnectTimer = m_reconnectTimer;
            m_reconnectTimer = nullptr;

            uv_timer_stop(reconnectTimer);
            uv_close(reinterpret_cast<uv_handle_t*>(reconnectTimer), [](uv_handle_t* handle) {
                delete reinterpret_cast<uv_timer_t*>(handle);
                handle = nullptr;
            });
        }
    });
}

// ==================== 接收超时定时器相关方法 ====================

void CUVTcpClient::startReceiveTimeoutTimer()
{
    if (!m_loop || m_receiveTimeoutInterval <= 0) {
        std::cout
            << "CUVTcpClient: Invalid loop or timeout interval, cannot start receive timeout timer."
            << std::endl;
        return;
    }

    m_loop->postTask([this]() {
        // 初始化接收超时定时器
        if (!m_receiveTimeoutTimer) {
            m_receiveTimeoutTimer = new uv_timer_t;
            std::memset(m_receiveTimeoutTimer, 0, sizeof(uv_timer_t));
            m_receiveTimeoutTimer->data = this;
            if (int result = uv_timer_init(m_loop->getLoop(), m_receiveTimeoutTimer); result != 0) {
                delete m_receiveTimeoutTimer;
                m_receiveTimeoutTimer = nullptr;
                std::cerr << "CUVTcpClient: Failed to initialize receive timeout timer: "
                          << uv_strerror(result) << std::endl;
                return;
            }
        };

        // 启动接收超时定时器
        uv_timer_start(
            m_receiveTimeoutTimer,
            [](uv_timer_t* handle) {
                CUVTcpClient* client = static_cast<CUVTcpClient*>(handle->data);

                std::cout << "CUVTcpClient: Receive timeout occurred." << std::endl;

                // 调用超时回调
                if (client->m_receiveTimeoutCallback) {
                    client->m_receiveTimeoutCallback();
                }

                // 断开连接
                client->disconnect();
                // 启动重连定时器
                client->startReconnectTimer();
            },
            m_receiveTimeoutInterval, // 延迟
            0);                       // 不重复
    });
}

void CUVTcpClient::stopReceiveTimeoutTimer()
{
    if (!m_loop || !m_receiveTimeoutTimer) {
        return;
    }

    m_loop->postTask([this]() {
        if (m_receiveTimeoutTimer) {
            auto receiveTimeoutTimer = m_receiveTimeoutTimer;
            m_receiveTimeoutTimer = nullptr;

            uv_timer_stop(receiveTimeoutTimer);
            uv_close(reinterpret_cast<uv_handle_t*>(receiveTimeoutTimer), [](uv_handle_t* handle) {
                delete reinterpret_cast<uv_timer_t*>(handle);
                handle = nullptr;
            });
        }
    });
}

// ==================== TcpClientCallback 内部类实现 ====================

// 连接回调处理
void CUVTcpClient::TcpClientCallback::onConnect(uv_connect_t* req, int status)
{
    ConnectRequest* req_data = static_cast<ConnectRequest*>(req->data);
    CUVTcpClient* client = req_data->client;
    bool success = (status == 0);
    std::string error;

    if (!success) {
        error = uv_strerror(status);
        client->m_state.store(ConnectState::DISCONNECTED);
        // 连接失败，增加重连间隔
        client->m_reconnectInterval = (std::min) (client->m_reconnectInterval * 2,
                                                  client->m_maxReconnectInterval);
        // 启动重连定时器
        client->startReconnectTimer();

    } else {
        client->m_state.store(ConnectState::CONNECTED);

        // 重置重连间隔
        client->m_reconnectInterval = client->m_initialReconnectInterval;

        // 开始接收数据
        uv_read_start(
            reinterpret_cast<uv_stream_t*>(client->m_tcpHandle),
            [](uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
                // 分配缓冲区
                buf->base = new char[suggested_size];
                buf->len = (ULONG) suggested_size;
            },
            [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
                CUVTcpClient* client = static_cast<CUVTcpClient*>(stream->data);
                client->m_callback.onReceive(stream, nread, buf);
            });

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
void CUVTcpClient::TcpClientCallback::onDisconnect(uv_handle_t* handle)
{
    CUVTcpClient* client = static_cast<CUVTcpClient*>(handle->data);

    if (client->m_disconnectCallback) {
        client->m_disconnectCallback();
    }

    // 清理TCP句柄
    delete reinterpret_cast<uv_tcp_t*>(handle);
    handle = nullptr;
}

// 发送回调处理
void CUVTcpClient::TcpClientCallback::onSend(uv_write_t* req, int status)
{
    SendRequest* req_data = static_cast<SendRequest*>(req->data);
    bool success = (status == 0);
    std::string error;

    if (!success) {
        error = uv_strerror(status);
    }

    // 调用用户回调
    if (req_data->callback) {
        req_data->callback(success, error);
    }

    // 清理资源
    delete req_data;
    delete req;
}

// 接收回调处理
void CUVTcpClient::TcpClientCallback::onReceive(uv_stream_t* stream,
                                                ssize_t nread,
                                                const uv_buf_t* buf)
{
    CUVTcpClient* client = static_cast<CUVTcpClient*>(stream->data);

    if (nread > 0) {
        // 有数据可读
        if (client->m_receiveCallback) {
            client->m_receiveCallback(buf->base, nread);
        }

        // 重置接收超时定时器
        client->stopReceiveTimeoutTimer();
        client->startReceiveTimeoutTimer();
    } else if (nread < 0) {
        // 关闭超时定时器
        client->stopReceiveTimeoutTimer();
        // 断开连接
        client->disconnect();
        // 启动重连定时器
        client->startReconnectTimer();
    }

    // 释放缓冲区
    if (buf->base) {
        delete[] buf->base;
    }
}
