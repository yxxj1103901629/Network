#ifndef CUVLOOP_H
#define CUVLOOP_H

#include "concurrentqueue.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <uv.h>

namespace Common {
namespace Network {

/**
 * @brief 事件循环类，封装了libuv的事件循环功能
 * @details 采用单例模式，自动管理工作线程的启动和停止
 *          用户无需手动调用start()和stop()方法
 */
class CUVLoop
{
private:
    /**
     * @brief 内部工作线程函数
     * @param arg 指向CUVLoop实例的指针
     */
    static void workerThread(void *arg);

private:
    // 私有构造函数，防止直接实例化
    CUVLoop();
    ~CUVLoop();

    // 禁止拷贝构造和赋值操作
    CUVLoop(const CUVLoop &) = delete;
    CUVLoop &operator=(const CUVLoop &) = delete;

public:
    /**
     * @brief 获取全局唯一的事件循环实例
     * @return CUVLoop实例指针
     */
    static CUVLoop *getInstance();

    /**
     * @brief 检查事件循环是否正在运行
     * @return 是否正在运行
     */
    bool isRunning() const;

    /**
     * @brief 获取libuv事件循环指针
     * @return uv_loop_t指针
     */
    uv_loop_t *getLoop() const;

    /**
     * @brief 向事件循环中提交一个任务
     * @param task 任务回调函数
     */
    void postTask(const std::function<void()> &task);

    /**
     * @brief 向事件循环中提交一个任务（右值引用版本）
     * @param task 任务回调函数
     */
    void postTask(std::function<void()> &&task);

private:
    uv_loop_t *m_loop;              // libuv事件循环指针
    std::thread *m_workerThread;    // 工作线程指针
    std::atomic<bool> m_isStopping; // 停止标志
    
    // 线程同步机制
    std::condition_variable m_condition;
    std::mutex m_mutex;
    bool m_loopInitialized;

    // 异步通信句柄
    uv_async_t m_asyncWork; // 异步任务触发句柄
    uv_async_t m_asyncExit; // 异步退出句柄

    // 任务队列（用于处理异步任务）
    moodycamel::ConcurrentQueue<std::function<void()>> m_taskQueue;
};

} // namespace Network
} // namespace Common

#endif // CUVLOOP_H
