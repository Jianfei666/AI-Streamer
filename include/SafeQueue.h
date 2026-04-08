// SafeQueue.h - 线程安全队列
#pragma once    // 防止头文件重复包含
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>

template <typename T>
class SafeQueue
{
private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cond;
public:
    SafeQueue(/* args */){};
    ~SafeQueue(){};

    void push(T item) {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_queue.push(item);

        m_cond.notify_one();

    }

    T pop() {
        std::unique_lock<std::mutex> lock(m_mutex);

        // 释放锁， 等待条件变量
        m_cond.wait(lock, [this]() {return !m_queue.empty();}); // [this]把当前类的this指针传递给lambda

        T item = m_queue.front();

        m_queue.pop();

        return item;
    }

    int size() {
        std::unique_lock<std::mutex> lock(m_mutex);

        return m_queue.size();
    }
};
