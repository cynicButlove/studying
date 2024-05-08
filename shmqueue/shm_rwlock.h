//
// Created by zhangshiping on 24-5-2.
//

#ifndef SHMQUEUE_SHM_RWLOCK_H
#define SHMQUEUE_SHM_RWLOCK_H

#include <sys/sem.h>
#include <sys/types.h>

namespace shmmqueue
{
class CShmRWlock
{
public:
    // 构造函数
    CShmRWlock();
    //构造函数.
    CShmRWlock(key_t iKey);
    //读锁
    int Rlock() const;
    //释放读锁
    int UnRlock() const;
    /**
     * TryRlock
     * @return  true lock ok,false lock failed
     */
    bool TryRlock() const;
    //写锁
    int Wlock() const;
    //释放写锁
    int UnWlock() const;
    /**
    * TryRlock
    * @return  true lock ok,false lock failed
    */
    bool TryWlock() const;
    //lock block until lock sucess


private:
    //初始化
    void init(key_t iKey);
protected:
    int m_iSemId;
    key_t m_iSemKey;
};

class CSafeShmRlock
{
public:
    CSafeShmRlock() : m_pLock(nullptr)
    {
    }
    CSafeShmRlock(CShmRWlock *pLock)
        : m_pLock(pLock)
    {
        if (m_pLock != nullptr)
        {
            m_pLock->Rlock();
        }
    }

    void InitLock(CShmRWlock *pLock)
    {
        m_pLock = pLock;
        m_pLock->Rlock();
    }

    ~CSafeShmRlock()
    {
        m_pLock->UnRlock();
    }
private:
    CShmRWlock *m_pLock;
};

class CSafeShmWlock
{
public:
    CSafeShmWlock()
            : m_pLock(nullptr)
    {

    }
    CSafeShmWlock(CShmRWlock *pLock)
        : m_pLock(pLock)
    {
        m_pLock->Wlock();
    }

    void InitLock(CShmRWlock *pLock)
    {
        m_pLock = pLock;
        m_pLock->Wlock();
    }


    ~CSafeShmWlock()
    {
        if (m_pLock != nullptr)
        {
            m_pLock->UnWlock();
        }
    }
private:
    CShmRWlock *m_pLock;
};
}
#endif //SHMQUEUE_SHM_RWLOCK_H
