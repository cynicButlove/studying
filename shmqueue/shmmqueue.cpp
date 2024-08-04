//
// Created by zhangshiping on 24-5-2.
//
#include "shmmqueue.h"
#include <new>
#include <iostream>
#include <cstring>
#include <sys/shm.h>

namespace shmmqueue
{
/// m_stMemTrunk + m_pQueue
/// \param pCurrAddr  创建m_stMemTrunk的起始地址，m_stMemTrunk之后是m_pQueueAddr
/// \param module
/// \param shmKey
/// \param shmId
/// \param size
CMessageQueue::CMessageQueue(BYTE *pCurrAddr, eQueueModel module, key_t shmKey, int shmId, size_t size)
{
    m_pShm = (void*) pCurrAddr;
    m_pQueueAddr = pCurrAddr;
    m_stMemTrunk = new (m_pQueueAddr) stMemTrunk();// 在共享内存中构造一个stMemTrunk对象，这里的m_pQueueAddr是共享内存的起始地址
    m_pQueueAddr += sizeof(stMemTrunk);//m_pQueueAddr偏移到队列起始地址
    m_stMemTrunk->m_iRead = 0;
    m_stMemTrunk->m_iWrite = 0;
    m_stMemTrunk->m_iShmKey = shmKey;
    m_stMemTrunk->m_iShmId = shmId;
    m_stMemTrunk->m_iSize = (unsigned int)size;
    m_stMemTrunk->m_eQueueModule = module;
    m_pReadLock= nullptr;
    m_pWriteLock= nullptr;
    InitLock(); //初始化*m_pReadLock ，*m_pWriteLock
}

CMessageQueue::~CMessageQueue()
{
    if(m_stMemTrunk) {
        DestroyShareMem(m_pShm,m_stMemTrunk->m_iShmKey);
        m_stMemTrunk->~stMemTrunk();
    }
    if (m_pReadLock) {
        delete m_pReadLock;
        m_pReadLock = nullptr;
    }
    if (m_pWriteLock) {
        delete m_pWriteLock;
        m_pWriteLock = nullptr;
    }
}
///
/// \param message
/// \param length
/// \return return 0 if send succeed , return -2 when queue is full,otherwise error code
int CMessageQueue::SendMessage(BYTE *message, MESS_SIZE_TYPE length)
{
    if (!message || length <= 0) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }

    CSafeShmWlock tmLock;
    //修改共享内存写锁，获取写锁
    if (IsWriteLock() && m_pWriteLock) {
        tmLock.InitLock(m_pWriteLock);
    }

    // 首先判断是否队列已满
    int size = GetFreeSize();
    if (size <= 0) {
        return (int) eQueueErrorCode::QUEUE_NO_SPACE;
    }

    //空间不足
    if ((length + sizeof(MESS_SIZE_TYPE)) > size) {
//        std::cout<<"size= "<<size<<std::endl;
//        std::cout<<"free= "<<GetFreeSize()<<std::endl;
        return (int) eQueueErrorCode::QUEUE_NO_SPACE;
    }

    MESS_SIZE_TYPE usInLength = length;
    BYTE *messageQueue = m_pQueueAddr;
    BYTE *messageLen = (BYTE *) (&usInLength);

    //写入的时候我们在数据头插上数据的长度，方便准确取数据,每次写入一个字节可能会分散在队列的头和尾
    unsigned int writePos = m_stMemTrunk->m_iWrite;
    for (MESS_SIZE_TYPE i = 0; i < sizeof(usInLength); i++) {
        messageQueue[writePos] = messageLen[i];  // 拷贝 Code 的长度
        writePos = (writePos + 1) & (m_stMemTrunk->m_iSize - 1);  // % 用于防止 Code 结尾的 idx 超出 code queue
    }

    unsigned int tmpLen = SHM_MIN(usInLength, m_stMemTrunk->m_iSize - writePos);
    memcpy((void *) (&messageQueue[writePos]), (const void *) message, (size_t) tmpLen);
    size_t tmpLastLen = length - tmpLen;
    if(tmpLastLen > 0)
    {
        /* then put the rest (if any) at the beginning of the buffer */
        memcpy(&messageQueue[0], message + tmpLen, tmpLastLen);
    }

    /*
    * Ensure that we add the bytes to the kfifo -before-
    * we update the fifo->in index.
    * 数据写入完成修改m_iWrite，保证读端不会读到写入一半的数据
    */
    __WRITE_BARRIER__;
    if(writePos+usInLength>=m_stMemTrunk->m_iSize)
    {
        m_stMemTrunk->m_iWrite_num++;
    }
    m_stMemTrunk->m_iWrite = (writePos + usInLength) & (m_stMemTrunk->m_iSize - 1);
    return (int) eQueueErrorCode::QUEUE_OK;
    //析构函数释放写锁
}
///
/// \param outDataAddr
/// \return return data length if length > 0 , return -3 when queue is empty ,otherwise error code
int CMessageQueue::GetMessage(BYTE *outDataAddr)
{
    if (!outDataAddr) {
        return (int) eQueueErrorCode::QUEUE_PARAM_ERROR;
    }

    CSafeShmRlock tmLock;
    //修改共享内存写锁，获取写锁
    if (IsReadLock() && m_pReadLock) {
        tmLock.InitLock(m_pReadLock);
    }

    int nTempMaxLength =(int) GetDataSize();
    if (nTempMaxLength == 0) {
        return (int) eQueueErrorCode::QUEUE_NO_MESSAGE;
    }


    // 如果数据的最大长度不到sizeof(MESS_SIZE_TYPE)（存入数据时在数据头插入了数据的长度,长度本身）
    if (nTempMaxLength <= (int) sizeof(MESS_SIZE_TYPE)) {
        std::cout<<nTempMaxLength<<" < "<<(int)sizeof(MESS_SIZE_TYPE)<<std::endl;
        PrintTrunk();
        m_stMemTrunk->m_iRead = m_stMemTrunk->m_iWrite;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }
    //取出数据的长度
    BYTE *messQueue = m_pQueueAddr;
    MESS_SIZE_TYPE messLen;
    BYTE *pTempDst = (BYTE *) &messLen;   // 数据拷贝的目的地址
    unsigned int readPos = m_stMemTrunk->m_iRead;
    //取出数据的长度
    for (MESS_SIZE_TYPE i = 0; i < sizeof(MESS_SIZE_TYPE); i++) {
        pTempDst[i] = messQueue[readPos];
        readPos = (readPos + 1) & (m_stMemTrunk->m_iSize - 1);
    }

    // 将数据长度回传
    //取出的数据的长度大于实际的数据长度，非法
    if (messLen > (int) (nTempMaxLength - sizeof(MESS_SIZE_TYPE)) ) {
        printf("[%s:%d] ReadHeadMessage messLen illegal,messLen: %zu,nTempMaxLength %d \n",
               __FILE__, __LINE__, messLen, nTempMaxLength);
        PrintTrunk();
//        m_stMemTrunk->m_iRead = m_stMemTrunk->m_iWrite;
        return (int) eQueueErrorCode::QUEUE_DATA_SEQUENCE_ERROR;
    }
    //读数据
    pTempDst = &outDataAddr[0];
    unsigned int tmpLen = SHM_MIN(messLen, m_stMemTrunk->m_iSize - readPos);
    memcpy(&pTempDst[0], &messQueue[readPos], tmpLen);
    unsigned int tmpLast = messLen - tmpLen;
    if(tmpLast > 0)
    {
        memcpy(&pTempDst[tmpLen], messQueue, tmpLast);
    }

    __WRITE_BARRIER__;
    if(readPos+messLen>=m_stMemTrunk->m_iSize)
    {
        m_stMemTrunk->m_iRead_num++;
    }

    m_stMemTrunk->m_iRead = (readPos + messLen) & (m_stMemTrunk->m_iSize - 1);
    return messLen;
}



void CMessageQueue::PrintTrunk()
{
    printf("Mem trunk address 0x%p,shmkey %d ,shmid %d, size %d, read %d (%d), write %d (%d), queue module %hhu \n",
           m_stMemTrunk,
           m_stMemTrunk->m_iShmKey,
           m_stMemTrunk->m_iShmId,
           m_stMemTrunk->m_iSize,
           m_stMemTrunk->m_iRead,
           m_stMemTrunk->m_iRead_num,
           m_stMemTrunk->m_iWrite,
           m_stMemTrunk->m_iWrite_num,
           static_cast<unsigned char>(m_stMemTrunk->m_eQueueModule));
}

//获取空闲区大小
unsigned int CMessageQueue::GetFreeSize()
{
    //长度应该减去预留部分长度8，保证首尾不会相接
    return m_stMemTrunk->m_iSize - GetDataSize() - EXTRA_BYTE;
}

//获取数据长度
unsigned int CMessageQueue::GetDataSize()
{
    unsigned int read=m_stMemTrunk->m_iRead;
    unsigned int write=m_stMemTrunk->m_iWrite;
    if(read==write) return 0;
    else if(read>write) return (unsigned int)(m_stMemTrunk->m_iSize+write-read);
    else return write-read;

}

void CMessageQueue::InitLock()
{
    if (IsReadLock()) {
        m_pReadLock = new CShmRWlock((key_t) (m_stMemTrunk->m_iShmKey + 1));
    }

    if (IsWriteLock()) {
        m_pWriteLock = new CShmRWlock((key_t) (m_stMemTrunk->m_iShmKey + 2));
    }
}

bool CMessageQueue::IsReadLock()
{
    return (m_stMemTrunk->m_eQueueModule == eQueueModel::MUL_READ_MUL_WRITE ||
        m_stMemTrunk->m_eQueueModule == eQueueModel::MUL_READ_ONE_WRITE);
}

bool CMessageQueue::IsWriteLock()
{
    return (m_stMemTrunk->m_eQueueModule == eQueueModel::MUL_READ_MUL_WRITE ||
        m_stMemTrunk->m_eQueueModule == eQueueModel::ONE_READ_MUL_WRITE);
}

    /**
     *shmget 是一个在 Unix-like 系统中用于创建或访问共享内存的系统调用。
     *key：这是一个键值，用于唯一标识一个共享内存段。通常，你可以使用 ftok 函数生成一个唯一的键值。
     *size：这是你想要创建的共享内存段的大小，以字节为单位。
     *      如果你正在访问一个已经存在的共享内存段，这个参数可以设置为0。
     *shmflg：这是一个标志位，用于设置共享内存段的权限和其他属性。
     *      例如，IPC_CREAT 表示如果指定的共享内存段不存在，则创建它。如果存在，返回标识符
     *      IPC_EXCL 用于确保创建的是一个全新的共享内存段，如果共享内存段已经存在，调用会失败。
     *      0666 是一个八进制数，表示共享内存段的权限，这里表示所有用户都有读写权限。
     *shmget 函数返回一个整数值，这是共享内存段的标识符（shmID）。如果函数调用失败，它将返回 -1
     */
BYTE *CMessageQueue::CreateShareMem(key_t iKey, long vSize, enShmModule &shmModule,int& shmId)
{
    size_t iTempShmSize;

    if (iKey < 0) {
        printf("[%s:%d] CreateShareMem failed. [key %d]errno:%s \n", __FILE__, __LINE__, iKey,strerror(errno));
        exit(-1);
    }

    iTempShmSize = (size_t) vSize;
    printf("Try to malloc share memory of %d bytes... \n", iTempShmSize);
    shmId = shmget(iKey, iTempShmSize, IPC_CREAT | IPC_EXCL | 0666);
    //    尝试创建一个大小为 iTempShmSize 的新的共享内存段，键值为 iKey，所有用户都可以读写。
    //    如果这个共享内存段已经存在，则 shmget 会失败，并返回 -1。   EXCL=exclusive
    if (shmId < 0) {  //创建失败，是否已经存在
        if (errno != EEXIST) {
            printf("[%s:%d] Alloc share memory failed, [iKey:%d] , size:%d , error:%s \n",
                   __FILE__, __LINE__, iKey, iTempShmSize, strerror(errno));
            exit(-1);
        }
        printf("Same shm seg [key= %d] exist, now try to attach it... \n", iKey);
        //创建如果存在，返回标识符
        shmId = shmget(iKey, iTempShmSize, IPC_CREAT | 0666);
        if (shmId < 0) {  //返回标识符失败，可能是size改变了
            printf("Attach to share memory [key= %d,shmId %d] failed,maybe the size of share memory changed,%s .now try to touch it again \n",
                    iKey, shmId, strerror(errno));
            //访问之前的iKey共享内存
            shmId = shmget(iKey, 0, 0666);
            if (shmId < 0) {//访问失败
                printf("[%s:%d] Fatel error, touch to shm [key= %d,shmId %d] failed, %s.\n", __FILE__, __LINE__, iKey, shmId,strerror(errno));
                exit(-1);
            }
            else {//访问成功
                //先删除之前的share memory
                printf("First remove the exist share memory [key= %d,shmId %d] \n", iKey,shmId);
                if (shmctl(shmId, IPC_RMID, NULL)) {
                    printf("[%s:%d] Remove share memory [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__, iKey,shmId,strerror(errno));
                    exit(-1);
                }
                //重新创建
                shmId = shmget(iKey, iTempShmSize, IPC_CREAT | IPC_EXCL | 0666);
                if (shmId < 0) {
                    printf("[%s:%d] Fatal error, alloc share memory [key= %d,shmId %d] failed, %s \n",
                           __FILE__, __LINE__, iKey,shmId,strerror(errno));
                    exit(-1);
                }
            }
        }
        else {  //返回标识符成功
            shmModule = enShmModule::SHM_RESUME;
            printf("Attach to share memory [key= %d,shmId %d] succeed.\n",iKey,shmId);
        }
    }
    else {
        shmModule = enShmModule::SHM_INIT;
    }

    printf("Successfully malloced share memory block,[key= %d,shmId %d] size = %d \n", iKey, shmId, iTempShmSize);
    //将标识符为shmId的共享内存段连接到当前进程的地址空间，并将连接的地址转换为BYTE*类型，然后赋值给tpShm。
    BYTE *tpShm = (BYTE *) shmat(shmId, NULL, 0);

    if ((void *) -1 == tpShm) {
        printf("[%s:%d] create share memory failed, shmat failed, [key= %d,shmId %d], error = %s. \n",
               __FILE__, __LINE__,iKey, shmId, strerror(errno));
        exit(0);
    }

    return tpShm;
}

//根据key获取共享内存id
//根据shmaddr断开共享内存
//根据id删除共享内存
int CMessageQueue::DestroyShareMem(const void *shmaddr,key_t iKey)
{
    int iShmID;

    if (iKey < 0) {
        printf("[%s:%d] Error in ftok, %s. \n", __FILE__, __LINE__, strerror(errno));
        return -1;
    }
    printf("Touch to share memory [key = %d]... \n", iKey);
    iShmID = shmget(iKey, 0, 0666);//根据key获取共享内存的id
    if (iShmID < 0) {
        printf("[%s:%d] Error, touch to shm [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__, iKey, iShmID, strerror(errno));
        return -1;
    }
    else {
        printf("Now disconnect the exist share memory [key= %d,shmId %d] \n",  iKey, iShmID);
        if(shmdt(shmaddr)){//
            printf("[%s:%d] Disconnect share memory [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__,iKey, iShmID,strerror(errno));
        } else{
            printf("Disconnect the exist share memory [key= %d,shmId %d] succeed \n", iKey, iShmID);
        }
        printf("Now remove the exist share memory [key= %d,shmId %d] \n", iKey, iShmID);
        if (shmctl(iShmID, IPC_RMID, NULL)) {//
            printf("[%s:%d] Remove share memory [key= %d,shmId %d] failed, %s \n", __FILE__, __LINE__, iKey, iShmID,strerror(errno));
            return -1;
        } else{
            printf("Remove shared memory [key= %d,shmId %d] succeed. \n", iShmID, iKey);
        }
    }
    return 0;
}
/// size 和 size-1 按位 & 运算
/// \param size
/// \return
bool CMessageQueue::IsPowerOfTwo(size_t size) {
    if(size < 1) return false;//2的次幂一定大于0
    return ((size & (size -1)) == 0);
}


///将size扩大到2^n
size_t CMessageQueue::RoundupPowofTwo(size_t size) {
    if(IsPowerOfTwo(size)) return size;
    int pos=0;
    while(size!=0){
        size=size>>1;
        pos++;
    }
    return 1UL << pos;
}

CMessageQueue *CMessageQueue::CreateInstance(key_t shmkey,
                                             size_t queuesize,
                                             eQueueModel queueModule)
{
    if(queuesize <= 0) return nullptr;
    //扩充
    std::cout<<"former size= "<<queuesize;
    queuesize= RoundupPowofTwo(queuesize);
    std::cout<<"  current size= "<<queuesize<<std::endl;
    enShmModule shmModule;
    int shmId = 0;
    BYTE * tmpMem = CMessageQueue::CreateShareMem(shmkey, queuesize + sizeof(stMemTrunk), shmModule,shmId);
    CMessageQueue *messageQueue = new CMessageQueue(tmpMem,queueModule, shmkey,shmId, queuesize);
    messageQueue->PrintTrunk();
    return messageQueue;
}

}

