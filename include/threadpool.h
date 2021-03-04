#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <queue>
#include <stdexcept>
#include "sync.h"

template <typename T>
class threadpool
{
public:
    threadpool(int thread_num = 8, int max_requests = 10000);
    ~threadpool();
    bool push(T *request);

private:
    // 静态函数，线程入口
    static void *worker(void *arg);
    void run();

private:
    int m_thread_num;            // 线程数
    int m_max_requests;          // 最大请求
    pthread_t *m_threads;        // 线程数组
    std::queue<T *> m_workqueue; // 工作队列
    locker m_queuelocker;        // 队列互斥量
    sem m_queuestat;             // 队列信号量
    bool m_stop;                 // 线程池停止工作
};

// 线程池构造函数
template <typename T>
threadpool<T>::threadpool(int thread_num, int max_requests) : m_thread_num(thread_num), m_max_requests(max_requests), m_threads(NULL), m_stop(false)
{
    if (thread_num <= 0 || max_requests <= 0)
    {
        throw std::runtime_error("the constructor threadpool() error: thread_num<=0||max_requests<=0.");
    }
    if ((m_threads = new pthread_t[thread_num]) == NULL)
    {
        throw std::runtime_error("the constructor threadpool() error: (m_threads=new pthread_t[thread_num])==NULL.");
    }

    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
    {
        throw std::runtime_error("the constructor threadpool() error: pthread_attr_init(&attr)!=0.");
    }
    // 设置线程分离属性
    if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) != 0)
    {
        throw std::runtime_error("the constructor threadpool() error: pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED)!=0.");
    }

    for (int i = 0; i < thread_num; ++i)
    {
#ifdef DEBUG
        printf("create the %dth thread\n", i);
#endif
        // 创建线程
        if (pthread_create(m_threads + i, &attr, worker, (void *)this) != 0)
        {
            delete[] m_threads;
            throw std::runtime_error("the constructor threadpool() error: pthread_create(m_threads + i, &attr, worker, (void *)this) != 0.");
        }
    }
    pthread_attr_destroy(&attr);
}
template <typename T>
threadpool<T>::~threadpool()
{
    m_stop=true;
    delete[] m_threads;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    threadpool *pool = static_cast<threadpool *>(arg);
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        while (m_queuestat.wait() == false)
            ;
        while (m_queuelocker.lock() == false)
            ;
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
            //throw std::runtime_error("run() error: m_workqueue.empty().");
        }

        T *request = m_workqueue.front();
        m_workqueue.pop();
        m_queuelocker.unlock();
        if (request == NULL)
        {
            continue;
        }
        request->process();
    }
}
// 向队列加任务
template <typename T>
bool threadpool<T>::push(T *request)
{
    while (m_queuelocker.lock() == false)
        ;
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

#endif
