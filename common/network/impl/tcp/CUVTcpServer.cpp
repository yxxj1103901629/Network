#include "CUVTcpServer.h"
// #include <iostream>
#include <utility>
#include <uv.h>

namespace {

Address getAddress(uv_tcp_t* clientHandle)
{
    Address address;
    struct sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    if (uv_tcp_getpeername(clientHandle, reinterpret_cast<struct sockaddr*>(&clientAddr), &addrLen)
        == 0) {
        char ipStr[INET_ADDRSTRLEN];
        uv_ip4_name(&clientAddr, ipStr, INET_ADDRSTRLEN);
        address.ip = ipStr;
        address.port = ntohs(clientAddr.sin_port);
    } else {
        address.ip = "";
        address.port = 0;
    }
    return address;
}

} // namespace

using namespace Common::Network;

// 构造函数
CUVTcpServer::CUVTcpServer()
    : m_loop(CUVLoop::getInstance())
    , m_serverHandle(nullptr)
    , m_state(ServerState::STOPPED)
    , m_listenAddress({"", 0})
    , m_maxConnections(1000)
    , m_receiveTimeoutInterval(0)
{}

// 析构函数
CUVTcpServer::~CUVTcpServer()
{
    stop();
    // std::cout << "CUVTcpServer: Server destroyed." << std::endl;
}

// 辅助函数实现
template<typename Func>
void CUVTcpServer::postTask(Func&& func) const
{
    if (isLoopValid()) {
        m_loop->postTask(std::forward<Func>(func));
    }
}

bool CUVTcpServer::isLoopValid() const
{
    return m_loop != nullptr;
}

void CUVTcpServer::closeClientConnection(uv_tcp_t* clientHandle) const
{
    if (clientHandle && !uv_is_closing(reinterpret_cast<uv_handle_t*>(clientHandle))) {
        uv_close(reinterpret_cast<uv_handle_t*>(clientHandle), onClientDisconnect);
    }
}

void CUVTcpServer::deleteClientHandle(uv_tcp_t* clientHandle)
{
    if (clientHandle) {
        uv_close(reinterpret_cast<uv_handle_t*>(clientHandle),
                 [](uv_handle_t* handle) { delete reinterpret_cast<uv_tcp_t*>(handle); });
    }
}

void CUVTcpServer::deleteTimeoutTimer(uv_timer_t* timeoutTimer)
{
    if (timeoutTimer && !uv_is_closing(reinterpret_cast<uv_handle_t*>(timeoutTimer))) {
        uv_timer_stop(timeoutTimer);
        uv_close(reinterpret_cast<uv_handle_t*>(timeoutTimer),
                 [](uv_handle_t* handle) { delete reinterpret_cast<uv_timer_t*>(handle); });
    }
}

void CUVTcpServer::listen(const std::string& host, int port)
{
    if (!isLoopValid()) {
        if (m_serverStartCallback) {
            m_serverStartCallback(false, "CUVTcpServer: Invalid event loop.");
        }
        return;
    }

    postTask([=]() {
        // 检查服务器状态
        if (m_state.load() != ServerState::STOPPED) {
            if (m_serverStartCallback) {
                m_serverStartCallback(false, "CUVTcpServer: Server is already running.");
            }
            return;
        }

        m_state.store(ServerState::STARTING);
        m_listenAddress.ip = host;
        m_listenAddress.port = port;

        // 初始化服务器TCP句柄
        if (!m_serverHandle) {
            m_serverHandle = new uv_tcp_t;
            std::memset(m_serverHandle, 0, sizeof(uv_tcp_t));
            m_serverHandle->data = this;

            if (int result = uv_tcp_init(m_loop->getLoop(), m_serverHandle); result != 0) {
                delete m_serverHandle;
                m_serverHandle = nullptr;
                if (m_serverStartCallback) {
                    m_serverStartCallback(false,
                                          "CUVTcpServer: Failed to initialize TCP handle: "
                                              + std::string(uv_strerror(result)));
                }
                return;
            }
        }

        // 绑定地址
        struct sockaddr_in addr;
        int result = uv_ip4_addr(host.c_str(), port, &addr);
        if (result != 0) {
            m_state.store(ServerState::STOPPED);
            if (m_serverStartCallback) {
                m_serverStartCallback(false,
                                      "CUVTcpServer: Failed to parse address: "
                                          + std::string(uv_strerror(result)));
            }
            return;
        }

        result = uv_tcp_bind(m_serverHandle, reinterpret_cast<const struct sockaddr*>(&addr), 0);
        if (result != 0) {
            m_state.store(ServerState::STOPPED);
            if (m_serverStartCallback) {
                m_serverStartCallback(false,
                                      "CUVTcpServer: Failed to bind address: "
                                          + std::string(uv_strerror(result)));
            }
            return;
        }

        // 开始监听连接
        result = uv_listen(reinterpret_cast<uv_stream_t*>(m_serverHandle),
                           (int) m_maxConnections,
                           onNewConnection);
        if (result != 0) {
            m_state.store(ServerState::STOPPED);
            if (m_serverStartCallback) {
                m_serverStartCallback(false,
                                      "CUVTcpServer: Failed to start listening: "
                                          + std::string(uv_strerror(result)));
            }
            return;
        }

        if (m_serverStartCallback) {
            m_serverStartCallback(true,
                                  "CUVTcpServer: Server started at " + host + ":"
                                      + std::to_string(port));
        }

        m_state.store(ServerState::RUNNING);
    });
}

void CUVTcpServer::stop()
{
    if (!isLoopValid()) {
        if (m_serverStopCallback) {
            m_serverStopCallback("CUVTcpServer: Invalid event loop.");
        }
        return;
    }

    if (m_state.load() == ServerState::STOPPED) {
        if (m_serverStopCallback) {
            m_serverStopCallback("CUVTcpServer: Server is already stopped.");
        }
        return;
    };
    m_state.store(ServerState::STOPPING);

    auto serverHandle = m_serverHandle;
    m_serverHandle = nullptr;

    auto clients_copy = m_clients;
    m_clients.clear();

    postTask([serverHandle, clients = std::move(clients_copy)]() {
        // 关闭服务器TCP句柄
        if (serverHandle && !uv_is_closing(reinterpret_cast<uv_handle_t*>(serverHandle))) {
            uv_close(reinterpret_cast<uv_handle_t*>(serverHandle),
                     [](uv_handle_t* handle) { delete reinterpret_cast<uv_tcp_t*>(handle); });
        }

        // 关闭所有客户端连接
        for (auto& pair : clients) {
            // 关闭客户端句柄
            deleteClientHandle(pair.second.handle);
            // 关闭超时定时器
            deleteTimeoutTimer(pair.second.timeoutTimer);
        }
    });

    m_state.store(ServerState::STOPPED);

    if (m_serverStopCallback) {
        m_serverStopCallback("CUVTcpServer: Server stopped.");
    }
}

void CUVTcpServer::send(const Address& clientAddr, const std::string& data)
{
    postTask([this, clientAddr, data]() {
        // 查找客户端
        auto it = m_clients.find(clientAddr);
        if (it == m_clients.end()) {
            if (m_sendCallback) {
                m_sendCallback(clientAddr, false, "CUVTcpServer: Client not found.");
            }
            return;
        }

        uv_tcp_t* clientHandle = it->second.handle;
        ClientContext* clientCtx = static_cast<ClientContext*>(clientHandle->data);

        uv_write_t* writeReq = new uv_write_t;
        writeReq->data = clientCtx;

        uv_buf_t buf = uv_buf_init(const_cast<char*>(data.data()), static_cast<ULONG>(data.size()));

        int result = uv_write(writeReq,
                              reinterpret_cast<uv_stream_t*>(clientHandle),
                              &buf,
                              1,
                              onSend);
        if (result != 0) {
            delete writeReq;
            if (m_sendCallback) {
                m_sendCallback(clientAddr,
                               false,
                               "CUVTcpServer: Failed to send data: "
                                   + std::string(uv_strerror(result)));
            }
            return;
        }
    });
}

CUVTcpServer::ServerState CUVTcpServer::getState() const
{
    return m_state.load();
}

void CUVTcpServer::setReceiveTimeoutInterval(int intervalMs)
{
    if (intervalMs < 0) {
        return;
    }

    postTask([this, intervalMs]() { m_receiveTimeoutInterval = intervalMs; });
}

// ======= 回调设置实现 =======

void CUVTcpServer::setStartCallback(ServerStartCallback&& callback)
{
    m_serverStartCallback = std::move(callback);
}

void CUVTcpServer::setStopCallback(ServerStopCallback&& callback)
{
    m_serverStopCallback = std::move(callback);
}

void CUVTcpServer::setConnectCallback(ClientConnectCallback&& callback)
{
    m_clientConnectCallback = std::move(callback);
}

void CUVTcpServer::setDisconnectCallback(ClientDisconnectCallback&& callback)
{
    m_clientDisconnectCallback = std::move(callback);
}

void CUVTcpServer::setReceiveCallback(ClientReceiveCallback&& callback)
{
    m_clientReceiveCallback = std::move(callback);
}

void CUVTcpServer::setSendCallback(SendCallback&& callback)
{
    m_sendCallback = std::move(callback);
}

void CUVTcpServer::setReceiveTimeoutCallback(ReceiveTimeoutCallback&& callback)
{
    m_receiveTimeoutCallback = std::move(callback);
}

// ======= CUVTcpServer 回调实现 =======

// 新连接回调处理
void CUVTcpServer::onNewConnection(uv_stream_t* server, int status)
{
    CUVTcpServer* tcpServer = static_cast<CUVTcpServer*>(server->data);

    if (status < 0) {
        if (tcpServer->m_clientConnectCallback) {
            tcpServer->m_clientConnectCallback(Address{"", 0}, // 无效地址
                                               false,
                                               "CUVTcpServer: New connection error: "
                                                   + std::string(uv_strerror(status)));
        }
        return;
    }

    // 创建新的TCP客户端句柄
    uv_tcp_t* clientHandle = new uv_tcp_t;
    std::memset(clientHandle, 0, sizeof(uv_tcp_t));

    if (int result = uv_tcp_init(tcpServer->m_loop->getLoop(), clientHandle); result != 0) {
        delete clientHandle;

        if (tcpServer->m_clientConnectCallback) {
            tcpServer
                ->m_clientConnectCallback(Address{"", 0}, // 无效地址
                                          false,
                                          "CUVTcpServer: Failed to initialize client TCP handle: "
                                              + std::string(uv_strerror(result)));
        };
        return;
    }

    // 接受新连接
    if (uv_accept(server, reinterpret_cast<uv_stream_t*>(clientHandle)) == 0) {
        // 获取客户端地址信息
        Address address = getAddress(clientHandle);

        // 检查是否重复连接
        if (tcpServer->m_clients.find(address) != tcpServer->m_clients.end()) {
            if (tcpServer->m_clientConnectCallback) {
                tcpServer->m_clientConnectCallback(address,
                                                   false,
                                                   "CUVTcpServer: Duplicate client connection.");
            };

            deleteClientHandle(clientHandle);
            return;
        }

        // 调用外部新连接回调
        if (tcpServer->m_clientConnectCallback) {
            tcpServer->m_clientConnectCallback(address, true, "");
        }

        uv_timer_t* timeoutTimer = nullptr;
        if (tcpServer->m_receiveTimeoutInterval > 0) {
            // 初始化接收超时定时器
            timeoutTimer = new uv_timer_t;
            std::memset(timeoutTimer, 0, sizeof(uv_timer_t));

            if (int result = uv_timer_init(tcpServer->m_loop->getLoop(), timeoutTimer);
                result != 0) {
                delete timeoutTimer;
                timeoutTimer = nullptr;

                deleteClientHandle(clientHandle);

                if (tcpServer->m_clientConnectCallback) {
                    tcpServer->m_clientConnectCallback(
                        address,
                        false,
                        "CUVTcpServer: Failed to initialize receive timeout timer: "
                            + std::string(uv_strerror(result)));
                }
                return;
            }
        }

        // 创建客户端上下文
        ClientContext* clientCtx = new ClientContext{tcpServer, address, clientHandle, timeoutTimer};

        // 将上下文存储在句柄的data字段中
        clientHandle->data = clientCtx;
        if (timeoutTimer) {
            timeoutTimer->data = clientCtx;
        }

        // 存储客户端信息
        tcpServer->m_clients[address] = ClientInfo{clientHandle, timeoutTimer};

        // 启动接收超时定时器
        if (timeoutTimer) {
            uv_timer_start(timeoutTimer,
                           onReceiveTimeout,
                           tcpServer->m_receiveTimeoutInterval, // 延迟
                           0);                                  // 不重复
        }

        uv_read_start(
            reinterpret_cast<uv_stream_t*>(clientHandle),
            [](uv_handle_t*, size_t suggested_size, uv_buf_t* buf) {
                // 分配缓冲区
                buf->base = new char[suggested_size];
                buf->len = (ULONG) suggested_size;
            },
            onClientRead);

    } else {
        if (tcpServer->m_clientConnectCallback) {
            tcpServer->m_clientConnectCallback(Address{"", 0}, // 无效地址
                                               false,
                                               "CUVTcpServer: Failed to accept new connection.");
        }

        deleteClientHandle(clientHandle);
    }
}

void CUVTcpServer::onClientDisconnect(uv_handle_t* handle)
{
    // 获取客户端上下文
    ClientContext* clientCtx = static_cast<ClientContext*>(handle->data);
    CUVTcpServer* tcpServer = clientCtx->server;
    Address addr = clientCtx->addr;

    // 调用外部断开回调
    if (tcpServer->m_clientDisconnectCallback) {
        tcpServer->m_clientDisconnectCallback(addr);
    }

    // 清理客户端句柄
    delete reinterpret_cast<uv_tcp_t*>(handle);

    // 清理接收超时定时器
    deleteTimeoutTimer(clientCtx->timeoutTimer);

    // 从客户端列表中移除
    tcpServer->m_clients.erase(addr);

    // 清理客户端上下文
    delete clientCtx;
}

void CUVTcpServer::onClientRead(uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf)
{
    // 获取客户端上下文
    ClientContext* clientCtx = static_cast<ClientContext*>(stream->data);
    CUVTcpServer* tcpServer = clientCtx->server;
    Address addr = clientCtx->addr;

    if (nread > 0) {
        // 调用外部数据接收回调
        if (tcpServer->m_clientReceiveCallback) {
            tcpServer->m_clientReceiveCallback(addr, std::string(buf->base, nread));
        }

        // 重置接收超时定时器
        if (clientCtx->timeoutTimer) {
            uv_timer_stop(clientCtx->timeoutTimer);
            uv_timer_start(clientCtx->timeoutTimer,
                           onReceiveTimeout,
                           tcpServer->m_receiveTimeoutInterval, // 延迟
                           0);                                  // 不重复
        }

    } else if (nread < 0) {
        // 发生错误或连接关闭，断开客户端连接
        tcpServer->closeClientConnection(clientCtx->clientHandle);
    }

    // 释放缓冲区
    if (buf->base) {
        delete[] buf->base;
    }
}

void CUVTcpServer::onSend(uv_write_t* req, int status)
{
    // 获取客户端上下文
    ClientContext* clientCtx = static_cast<ClientContext*>(req->data);
    CUVTcpServer* tcpServer = clientCtx->server;
    Address clientAddr = clientCtx->addr;

    if (tcpServer->m_sendCallback) {
        if (status == 0) {
            tcpServer->m_sendCallback(clientAddr, true, "");
        } else {
            tcpServer->m_sendCallback(clientAddr,
                                      false,
                                      "CUVTcpServer: Failed to send data: "
                                          + std::string(uv_strerror(status)));
        }
    }

    delete req;
}

void CUVTcpServer::onReceiveTimeout(uv_timer_t* handle)
{
    // 获取客户端上下文
    ClientContext* clientCtx = static_cast<ClientContext*>(handle->data);
    CUVTcpServer* tcpServer = clientCtx->server;
    Address clientAddr = clientCtx->addr;

    // 关闭超时的客户端连接
    if (clientCtx->clientHandle
        && !uv_is_closing(reinterpret_cast<uv_handle_t*>(clientCtx->clientHandle))) {
        tcpServer->closeClientConnection(clientCtx->clientHandle);

        if (tcpServer->m_receiveTimeoutCallback) {
            tcpServer->m_receiveTimeoutCallback(clientAddr);
        };
    }
}
