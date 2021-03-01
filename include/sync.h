#ifndef SYNC_H
#define SYNC_H

#include<stdexcept>
#include<pthread.h>
#include<semaphore.h>

/*信号量*/
class sem
{
public:
    sem()
    {
        if(sem_init(&m_sem,0,0)!=0)
        {
            throw std::runtime_error("the constructor sem() error: sem_init(&m_sem,0,0) return not zero.");
        }
    }
    ~sem()
    {
        sem_destroy(&m_sem);
    }
    bool wait()
    {
        return sem_wait(&m_sem)==0;
    }
    bool post()
    {
        return sem_post(&m_sem)==0;
    }

private:
    sem_t m_sem;
};

/*互斥量*/
class locker
{
public:
    locker()
    {
        if(pthread_mutex_init(&m_mutex,NULL)!=0)
        {
            throw std::runtime_error("the constructor locker() error: pthread_mutex_init(&m_mutex,NULL) return not zero.");
        }
    }
    ~locker()
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex)==0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex)==0;
    }
private:
    pthread_mutex_t m_mutex;
};

/*条件变量*/
class cond
{
public:
    cond()
    {
        if(pthread_mutex_init(&m_mutex,NULL)!=0)
        {
            throw std::runtime_error("the constructor cond() error: pthread_mutex_init(&m_mutex,NULL) return not zero.");
        }
        if(pthread_cond_init(&m_cond,NULL)!=0)
        {
            pthread_mutex_destroy(&m_mutex);
            throw std::runtime_error("the constructor cond() error: pthread_cond_init(&m_cond,NULL) return not zero.");
        }
    }
    ~cond()
    {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex)==0;
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex)==0;
    }
    bool wait()
    {
        return pthread_cond_wait(&m_cond,&m_mutex)==0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond)==0;
    }
private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};

#endif