#include "CUVLoop.h"
#include "concurrentqueue.h"
#include <functional>
#include <iostream>

using namespace Common::Network;

// Task类型定义
using Task = std::function<void()>;

// 私有构造函数
CUVLoop::CUVLoop()
    : m_loop(nullptr)
    , m_isStopping(false)
    , m_loopInitialized(false)
{
    // 启动工作线程
    m_workerThread = new std::thread(workerThread, this);
    
    // 等待直到工作线程初始化完成
    std::unique_lock<std::mutex> lock(m_mutex);
    m_condition.wait(lock, [this] { return m_loopInitialized; });
    
    // 如果初始化失败，清理资源
    if (m_loopInitialized == false) {
        m_isStopping = true;
        if (m_workerThread != nullptr && m_workerThread->joinable()) {
            m_workerThread->join();
            delete m_workerThread;
            m_workerThread = nullptr;
        }
        return;
    }
}

// 析构函数
CUVLoop::~CUVLoop()
{
    m_isStopping = true;

    // 如果loop已经初始化，通过异步退出句柄触发安全停止
    if (m_loop) {
        uv_async_send(&m_asyncExit);
    }

    // 等待工作线程完成所有清理工作
    if (m_workerThread != nullptr && m_workerThread->joinable()) {
        m_workerThread->join();
        delete m_workerThread;
        m_workerThread = nullptr;
    }

    std::cout << "CUVLoop: Event loop stopped and resources cleaned up." << std::endl;
}

// 内部工作线程函数
void CUVLoop::workerThread(void *arg)
{
    CUVLoop *loop = static_cast<CUVLoop *>(arg);

    // 初始化libuv循环
    loop->m_loop = new uv_loop_t;
    if (uv_loop_init(loop->m_loop) != 0) {
        delete loop->m_loop;
        loop->m_loop = nullptr;
        loop->m_isStopping = true;
        return;
    }

    // 初始化异步退出句柄
    loop->m_asyncExit.data = loop;
    int exitInitResult = uv_async_init(loop->m_loop, &loop->m_asyncExit, [](uv_async_t *handle) {
        CUVLoop *loop = static_cast<CUVLoop *>(handle->data);

        // 清理所有未处理的任务
        Task task;
        while (loop->m_taskQueue.try_dequeue(task)) {
            // 任务对象会被自动销毁
        }

        // 停止libuv循环
        uv_stop(handle->loop);
    });

    // 初始化异步任务处理句柄
    loop->m_asyncWork.data = loop;
    int workInitResult = uv_async_init(loop->m_loop, &loop->m_asyncWork, [](uv_async_t *handle) {
        CUVLoop *loop = static_cast<CUVLoop *>(handle->data);

        Task task;
        while (loop->m_taskQueue.try_dequeue(task)) {
            if (task) {
                task();
            }
        }
    });

    // 检查初始化结果
    if (exitInitResult != 0 || workInitResult != 0) {
        std::cerr << "CUVLoop: Failed to initialize async handles" << std::endl;
        delete loop->m_loop;
        loop->m_loop = nullptr;
        loop->m_isStopping = true;
        
        // 通知主线程初始化失败
        {
            std::lock_guard<std::mutex> lock(loop->m_mutex);
            loop->m_loopInitialized = false;
            loop->m_condition.notify_one();
        }
        return;
    }

    std::cout << "CUVLoop: Event loop started in worker thread." << std::endl;

    // 标记loop已经初始化完成，通知主线程继续执行
    {
        std::lock_guard<std::mutex> lock(loop->m_mutex);
        loop->m_loopInitialized = true;
        loop->m_condition.notify_one();
    }
    
    // 运行libuv循环
    uv_run(loop->m_loop, UV_RUN_DEFAULT);

    // 关闭所有uv句柄
    uv_close(reinterpret_cast<uv_handle_t *>(&loop->m_asyncExit), [](uv_handle_t *) {});
    uv_close(reinterpret_cast<uv_handle_t *>(&loop->m_asyncWork), [](uv_handle_t *) {});

    // 处理所有剩余的事件，直到loop不再活跃
    while (uv_loop_alive(loop->m_loop)) {
        uv_run(loop->m_loop, UV_RUN_DEFAULT);
    }

    // 关闭并清理loop
    uv_loop_close(loop->m_loop);
    delete loop->m_loop;
    loop->m_loop = nullptr;
}

// 获取单例实例
CUVLoop *CUVLoop::getInstance()
{
    static CUVLoop instance;
    return &instance;
}

// 获取libuv循环指针
uv_loop_t *CUVLoop::getLoop() const
{
    return m_loop;
}

// 检查事件循环是否正在运行
bool CUVLoop::isRunning() const
{
    return !m_isStopping.load();
}

// 向循环中提交任务（左值引用版本）
void CUVLoop::postTask(const std::function<void()> &task)
{
    if (!m_isStopping && m_loop) {
        m_taskQueue.enqueue(task);
        // 触发异步任务处理
        uv_async_send(&m_asyncWork);
    }
}

// 向循环中提交任务（右值引用版本）
void CUVLoop::postTask(std::function<void()> &&task)
{
    if (!m_isStopping && m_loop) {
        m_taskQueue.enqueue(std::move(task));
        // 触发异步任务处理
        uv_async_send(&m_asyncWork);
    }
}
