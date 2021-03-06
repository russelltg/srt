/*****************************************************************************
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2017 Haivision Systems Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>
 * 
 * Based on UDT4 SDK version 4.11
 *****************************************************************************/

/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 05/05/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifdef WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>f
#endif
#include <cstring>

#include "common.h"
#include "core.h"
#include "netinet_any.h"
#include "threadname.h"
#include "logging.h"
#include "queue.h"

using namespace std;

CUnitQueue::CUnitQueue():
m_pQEntry(NULL),
m_pCurrQueue(NULL),
m_pLastQueue(NULL),
m_iSize(0),
m_iCount(0),
m_iMSS(),
m_iIPversion()
{
}

CUnitQueue::~CUnitQueue()
{
   CQEntry* p = m_pQEntry;

   while (p != NULL)
   {
      delete [] p->m_pUnit;
      delete [] p->m_pBuffer;

      CQEntry* q = p;
      if (p == m_pLastQueue)
         p = NULL;
      else
         p = p->m_pNext;
      delete q;
   }
}

int CUnitQueue::init(int size, int mss, int version)
{
   CQEntry* tempq = NULL;
   CUnit* tempu = NULL;
   char* tempb = NULL;

   try
   {
      tempq = new CQEntry;
      tempu = new CUnit [size];
      tempb = new char [size * mss];
   }
   catch (...)
   {
      delete tempq;
      delete [] tempu;
      delete [] tempb;

      return -1;
   }

   for (int i = 0; i < size; ++ i)
   {
      tempu[i].m_iFlag = CUnit::FREE;
      tempu[i].m_Packet.m_pcData = tempb + i * mss;
   }
   tempq->m_pUnit = tempu;
   tempq->m_pBuffer = tempb;
   tempq->m_iSize = size;

   m_pQEntry = m_pCurrQueue = m_pLastQueue = tempq;
   m_pQEntry->m_pNext = m_pQEntry;

   m_pAvailUnit = m_pCurrQueue->m_pUnit;

   m_iSize = size;
   m_iMSS = mss;
   m_iIPversion = version;

   return 0;
}

int CUnitQueue::increase()
{
   // adjust/correct m_iCount
   int real_count = 0;
   CQEntry* p = m_pQEntry;
   while (p != NULL)
   {
      CUnit* u = p->m_pUnit;
      for (CUnit* end = u + p->m_iSize; u != end; ++ u)
         if (u->m_iFlag != CUnit::FREE)
            ++ real_count;

      if (p == m_pLastQueue)
         p = NULL;
      else
         p = p->m_pNext;
   }
   m_iCount = real_count;
   if (double(m_iCount) / m_iSize < 0.9)
      return -1;

   CQEntry* tempq = NULL;
   CUnit* tempu = NULL;
   char* tempb = NULL;

   // all queues have the same size
   int size = m_pQEntry->m_iSize;

   try
   {
      tempq = new CQEntry;
      tempu = new CUnit [size];
      tempb = new char [size * m_iMSS];
   }
   catch (...)
   {
      delete tempq;
      delete [] tempu;
      delete [] tempb;

      return -1;
   }

   for (int i = 0; i < size; ++ i)
   {
      tempu[i].m_iFlag = CUnit::FREE;
      tempu[i].m_Packet.m_pcData = tempb + i * m_iMSS;
   }
   tempq->m_pUnit = tempu;
   tempq->m_pBuffer = tempb;
   tempq->m_iSize = size;

   m_pLastQueue->m_pNext = tempq;
   m_pLastQueue = tempq;
   m_pLastQueue->m_pNext = m_pQEntry;

   m_iSize += size;

   return 0;
}

int CUnitQueue::shrink()
{
   // currently queue cannot be shrunk.
   return -1;
}

CUnit* CUnitQueue::getNextAvailUnit()
{
   if (m_iCount * 10 > m_iSize * 9)
      increase();

   if (m_iCount >= m_iSize)
      return NULL;

   CQEntry* entrance = m_pCurrQueue;

   do
   {
      for (CUnit* sentinel = m_pCurrQueue->m_pUnit + m_pCurrQueue->m_iSize - 1; m_pAvailUnit != sentinel; ++ m_pAvailUnit)
         if (m_pAvailUnit->m_iFlag == CUnit::FREE)
            return m_pAvailUnit;

      if (m_pCurrQueue->m_pUnit->m_iFlag == CUnit::FREE)
      {
         m_pAvailUnit = m_pCurrQueue->m_pUnit;
         return m_pAvailUnit;
      }

      m_pCurrQueue = m_pCurrQueue->m_pNext;
      m_pAvailUnit = m_pCurrQueue->m_pUnit;
   } while (m_pCurrQueue != entrance);

   increase();

   return NULL;
}


CSndUList::CSndUList():
    m_pHeap(NULL),
    m_iArrayLength(4096),
    m_iLastEntry(-1),
    m_ListLock(),
    m_pWindowLock(NULL),
    m_pWindowCond(NULL),
    m_pTimer(NULL)
{
    m_pHeap = new CSNode*[m_iArrayLength];
    pthread_mutex_init(&m_ListLock, NULL);
}

CSndUList::~CSndUList()
{
    delete [] m_pHeap;
    pthread_mutex_destroy(&m_ListLock);
}

void CSndUList::insert(int64_t ts, const CUDT* u)
{
   CGuard listguard(m_ListLock);

   // increase the heap array size if necessary
   if (m_iLastEntry == m_iArrayLength - 1)
   {
      CSNode** temp = NULL;

      try
      {
         temp = new CSNode*[m_iArrayLength * 2];
      }
      catch(...)
      {
         return;
      }

      memcpy(temp, m_pHeap, sizeof(CSNode*) * m_iArrayLength);
      m_iArrayLength *= 2;
      delete [] m_pHeap;
      m_pHeap = temp;
   }

   insert_(ts, u);
}

void CSndUList::update(const CUDT* u, bool reschedule)
{
   CGuard listguard(m_ListLock);

   CSNode* n = u->m_pSNode;

   if (n->m_iHeapLoc >= 0)
   {
      if (!reschedule)
         return;

      if (n->m_iHeapLoc == 0)
      {
         n->m_llTimeStamp = 1;
         m_pTimer->interrupt();
         return;
      }

      remove_(u);
   }

   insert_(1, u);
}

int CSndUList::pop(sockaddr*& addr, CPacket& pkt)
{
   CGuard listguard(m_ListLock);

   if (-1 == m_iLastEntry)
      return -1;

   // no pop until the next schedulled time
   uint64_t ts;
   CTimer::rdtsc(ts);
   if (ts < m_pHeap[0]->m_llTimeStamp)
      return -1;

   CUDT* u = m_pHeap[0]->m_pUDT;
   remove_(u);

   if (!u->m_bConnected || u->m_bBroken)
      return -1;

   // pack a packet from the socket
   if (u->packData(pkt, ts) <= 0)
      return -1;

   addr = u->m_pPeerAddr;

   // insert a new entry, ts is the next processing time
   if (ts > 0)
      insert_(ts, u);

   return 1;
}

void CSndUList::remove(const CUDT* u)
{
   CGuard listguard(m_ListLock);

   remove_(u);
}

uint64_t CSndUList::getNextProcTime()
{
   CGuard listguard(m_ListLock);

   if (-1 == m_iLastEntry)
      return 0;

   return m_pHeap[0]->m_llTimeStamp;
}

void CSndUList::insert_(int64_t ts, const CUDT* u)
{
   CSNode* n = u->m_pSNode;

   // do not insert repeated node
   if (n->m_iHeapLoc >= 0)
      return;

   m_iLastEntry ++;
   m_pHeap[m_iLastEntry] = n;
   n->m_llTimeStamp = ts;

   int q = m_iLastEntry;
   int p = q;
   while (p != 0)
   {
      p = (q - 1) >> 1;
      if (m_pHeap[p]->m_llTimeStamp > m_pHeap[q]->m_llTimeStamp)
      {
         CSNode* t = m_pHeap[p];
         m_pHeap[p] = m_pHeap[q];
         m_pHeap[q] = t;
         t->m_iHeapLoc = q;
         q = p;
      }
      else
         break;
   }

   n->m_iHeapLoc = q;

   // an earlier event has been inserted, wake up sending worker
   if (n->m_iHeapLoc == 0)
      m_pTimer->interrupt();

   // first entry, activate the sending queue
   if (0 == m_iLastEntry)
   {
       pthread_mutex_lock(m_pWindowLock);
       pthread_cond_signal(m_pWindowCond);
       pthread_mutex_unlock(m_pWindowLock);
   }
}

void CSndUList::remove_(const CUDT* u)
{
   CSNode* n = u->m_pSNode;

   if (n->m_iHeapLoc >= 0)
   {
      // remove the node from heap
      m_pHeap[n->m_iHeapLoc] = m_pHeap[m_iLastEntry];
      m_iLastEntry --;
      m_pHeap[n->m_iHeapLoc]->m_iHeapLoc = n->m_iHeapLoc;

      int q = n->m_iHeapLoc;
      int p = q * 2 + 1;
      while (p <= m_iLastEntry)
      {
         if ((p + 1 <= m_iLastEntry) && (m_pHeap[p]->m_llTimeStamp > m_pHeap[p + 1]->m_llTimeStamp))
            p ++;

         if (m_pHeap[q]->m_llTimeStamp > m_pHeap[p]->m_llTimeStamp)
         {
            CSNode* t = m_pHeap[p];
            m_pHeap[p] = m_pHeap[q];
            m_pHeap[p]->m_iHeapLoc = p;
            m_pHeap[q] = t;
            m_pHeap[q]->m_iHeapLoc = q;

            q = p;
            p = q * 2 + 1;
         }
         else
            break;
      }

      n->m_iHeapLoc = -1;
   }

   // the only event has been deleted, wake up immediately
   if (0 == m_iLastEntry)
      m_pTimer->interrupt();
}

//
CSndQueue::CSndQueue():
m_WorkerThread(),
m_pSndUList(NULL),
m_pChannel(NULL),
m_pTimer(NULL),
m_WindowLock(),
m_WindowCond(),
m_bClosing(false),
m_ExitCond()
{
    pthread_cond_init(&m_WindowCond, NULL);
    pthread_mutex_init(&m_WindowLock, NULL);
}

CSndQueue::~CSndQueue()
{
   m_bClosing = true;

   if(m_pTimer != NULL)
   {
        m_pTimer->interrupt();
   }

   pthread_mutex_lock(&m_WindowLock);
   pthread_cond_signal(&m_WindowCond);
   pthread_mutex_unlock(&m_WindowLock);
   if (!pthread_equal(m_WorkerThread, pthread_t()))
       pthread_join(m_WorkerThread, NULL);
   pthread_cond_destroy(&m_WindowCond);
   pthread_mutex_destroy(&m_WindowLock);

   delete m_pSndUList;
}

void CSndQueue::init(CChannel* c, CTimer* t)
{
   m_pChannel = c;
   m_pTimer = t;
   m_pSndUList = new CSndUList;
   m_pSndUList->m_pWindowLock = &m_WindowLock;
   m_pSndUList->m_pWindowCond = &m_WindowCond;
   m_pSndUList->m_pTimer = m_pTimer;

   ThreadName tn("SRT:SndQ:worker");
   if (0 != pthread_create(&m_WorkerThread, NULL, CSndQueue::worker, this))
   {
	   m_WorkerThread = pthread_t();
       throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
   }
}

#ifdef SRT_ENABLE_IPOPTS
int CSndQueue::getIpTTL() const
{
   return m_pChannel ? m_pChannel->getIpTTL() : -1;
}

int CSndQueue::getIpToS() const
{
   return m_pChannel ? m_pChannel->getIpToS() : -1;
}
#endif

void* CSndQueue::worker(void* param)
{
    CSndQueue* self = (CSndQueue*)param;

    THREAD_STATE_INIT("SRT Tx Queue");

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
    CTimer::rdtsc(self->m_ullDbgTime);
    self->m_ullDbgPeriod = 5000000LL * CTimer::getCPUFrequency();
    self->m_ullDbgTime += self->m_ullDbgPeriod;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

    while (!self->m_bClosing)
    {
        uint64_t ts = self->m_pSndUList->getNextProcTime();

#if   defined(SRT_DEBUG_SNDQ_HIGHRATE)
        self->m_WorkerStats.lIteration++;
#endif /* SRT_DEBUG_SNDQ_HIGHRATE */

        if (ts > 0)
        {
            // wait until next processing time of the first socket on the list
            uint64_t currtime;
            CTimer::rdtsc(currtime);

#if      defined(SRT_DEBUG_SNDQ_HIGHRATE)
            if (self->m_ullDbgTime <= currtime) {
                fprintf(stdout, "SndQueue %lu slt:%lu nrp:%lu snt:%lu nrt:%lu ctw:%lu\n",  
                        self->m_WorkerStats.lIteration,
                        self->m_WorkerStats.lSleepTo,
                        self->m_WorkerStats.lNotReadyPop,
                        self->m_WorkerStats.lSendTo,
                        self->m_WorkerStats.lNotReadyTs,  
                        self->m_WorkerStats.lCondWait);
                memset(&self->m_WorkerStats, 0, sizeof(self->m_WorkerStats));
                self->m_ullDbgTime = currtime + self->m_ullDbgPeriod;
            }
#endif   /* SRT_DEBUG_SNDQ_HIGHRATE */

            THREAD_PAUSED();
            if (currtime < ts) 
            {
                self->m_pTimer->sleepto(ts);

#if         defined(HAI_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lSleepTo++;
#endif      /* SRT_DEBUG_SNDQ_HIGHRATE */
            }
            THREAD_RESUMED();

            // it is time to send the next pkt
            sockaddr* addr;
            CPacket pkt;
            if (self->m_pSndUList->pop(addr, pkt) < 0) 
            {
                continue;

#if         defined(SRT_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lNotReadyPop++;
#endif      /* SRT_DEBUG_SNDQ_HIGHRATE */
            }
            if ( pkt.isControl() )
            {
                LOGC(mglog.Debug) << self->CONID() << "chn:SENDING: " << MessageTypeStr(pkt.getType(), pkt.getExtendedType());
            }
            else
            {
                LOGC(dlog.Debug) << self->CONID() << "chn:SENDING SIZE " << pkt.getLength() << " SEQ: " << pkt.getSeqNo();
            }
            self->m_pChannel->sendto(addr, pkt);

#if      defined(HAI_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lSendTo++;
#endif   /* SRT_DEBUG_SNDQ_HIGHRATE */
        }
        else
        {
#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
            self->m_WorkerStats.lNotReadyTs++;
#endif   /* SRT_DEBUG_SNDQ_HIGHRATE */

            // wait here if there is no sockets with data to be sent
            THREAD_PAUSED();
            pthread_mutex_lock(&self->m_WindowLock);
            if (!self->m_bClosing && (self->m_pSndUList->m_iLastEntry < 0)) {
                pthread_cond_wait(&self->m_WindowCond, &self->m_WindowLock);

#if defined(SRT_DEBUG_SNDQ_HIGHRATE)
                self->m_WorkerStats.lCondWait++;
#endif         /* SRT_DEBUG_SNDQ_HIGHRATE */
            }
            THREAD_RESUMED();
            pthread_mutex_unlock(&self->m_WindowLock);
        }
    }

    THREAD_EXIT();
    return NULL;
}

int CSndQueue::sendto(const sockaddr* addr, CPacket& packet)
{
   // send out the packet immediately (high priority), this is a control packet
   m_pChannel->sendto(addr, packet);
   return packet.getLength();
}


//
CRcvUList::CRcvUList():
m_pUList(NULL),
m_pLast(NULL)
{
}

CRcvUList::~CRcvUList()
{
}

void CRcvUList::insert(const CUDT* u)
{
   CRNode* n = u->m_pRNode;
   CTimer::rdtsc(n->m_llTimeStamp);

   if (NULL == m_pUList)
   {
      // empty list, insert as the single node
      n->m_pPrev = n->m_pNext = NULL;
      m_pLast = m_pUList = n;

      return;
   }

   // always insert at the end for RcvUList
   n->m_pPrev = m_pLast;
   n->m_pNext = NULL;
   m_pLast->m_pNext = n;
   m_pLast = n;
}

void CRcvUList::remove(const CUDT* u)
{
   CRNode* n = u->m_pRNode;

   if (!n->m_bOnList)
      return;

   if (NULL == n->m_pPrev)
   {
      // n is the first node
      m_pUList = n->m_pNext;
      if (NULL == m_pUList)
         m_pLast = NULL;
      else
         m_pUList->m_pPrev = NULL;
   }
   else
   {
      n->m_pPrev->m_pNext = n->m_pNext;
      if (NULL == n->m_pNext)
      {
         // n is the last node
         m_pLast = n->m_pPrev;
      }
      else
         n->m_pNext->m_pPrev = n->m_pPrev;
   }

   n->m_pNext = n->m_pPrev = NULL;
}

void CRcvUList::update(const CUDT* u)
{
   CRNode* n = u->m_pRNode;

   if (!n->m_bOnList)
      return;

   CTimer::rdtsc(n->m_llTimeStamp);

   // if n is the last node, do not need to change
   if (NULL == n->m_pNext)
      return;

   if (NULL == n->m_pPrev)
   {
      m_pUList = n->m_pNext;
      m_pUList->m_pPrev = NULL;
   }
   else
   {
      n->m_pPrev->m_pNext = n->m_pNext;
      n->m_pNext->m_pPrev = n->m_pPrev;
   }

   n->m_pPrev = m_pLast;
   n->m_pNext = NULL;
   m_pLast->m_pNext = n;
   m_pLast = n;
}

//
CHash::CHash():
m_pBucket(NULL),
m_iHashSize(0)
{
}

CHash::~CHash()
{
   for (int i = 0; i < m_iHashSize; ++ i)
   {
      CBucket* b = m_pBucket[i];
      while (NULL != b)
      {
         CBucket* n = b->m_pNext;
         delete b;
         b = n;
      }
   }

   delete [] m_pBucket;
}

void CHash::init(int size)
{
   m_pBucket = new CBucket* [size];

   for (int i = 0; i < size; ++ i)
      m_pBucket[i] = NULL;

   m_iHashSize = size;
}

CUDT* CHash::lookup(int32_t id)
{
   // simple hash function (% hash table size); suitable for socket descriptors
   CBucket* b = m_pBucket[id % m_iHashSize];

   while (NULL != b)
   {
      if (id == b->m_iID)
         return b->m_pUDT;
      b = b->m_pNext;
   }

   return NULL;
}

void CHash::insert(int32_t id, CUDT* u)
{
   CBucket* b = m_pBucket[id % m_iHashSize];

   CBucket* n = new CBucket;
   n->m_iID = id;
   n->m_pUDT = u;
   n->m_pNext = b;

   m_pBucket[id % m_iHashSize] = n;
}

void CHash::remove(int32_t id)
{
   CBucket* b = m_pBucket[id % m_iHashSize];
   CBucket* p = NULL;

   while (NULL != b)
   {
      if (id == b->m_iID)
      {
         if (NULL == p)
            m_pBucket[id % m_iHashSize] = b->m_pNext;
         else
            p->m_pNext = b->m_pNext;

         delete b;

         return;
      }

      p = b;
      b = b->m_pNext;
   }
}


//
CRendezvousQueue::CRendezvousQueue():
m_lRendezvousID(),
m_RIDVectorLock()
{
    pthread_mutex_init(&m_RIDVectorLock, NULL);
}

CRendezvousQueue::~CRendezvousQueue()
{
   pthread_mutex_destroy(&m_RIDVectorLock);

   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (AF_INET == i->m_iIPversion)
         delete (sockaddr_in*)i->m_pPeerAddr;
      else
         delete (sockaddr_in6*)i->m_pPeerAddr;
   }

   m_lRendezvousID.clear();
}

void CRendezvousQueue::insert(const UDTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl)
{
   CGuard vg(m_RIDVectorLock);

   CRL r;
   r.m_iID = id;
   r.m_pUDT = u;
   r.m_iIPversion = ipv;
   r.m_pPeerAddr = (AF_INET == ipv) ? (sockaddr*)new sockaddr_in : (sockaddr*)new sockaddr_in6;
   memcpy(r.m_pPeerAddr, addr, (AF_INET == ipv) ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
   r.m_ullTTL = ttl;

   m_lRendezvousID.push_back(r);
}

void CRendezvousQueue::remove(const UDTSOCKET& id)
{
   CGuard vg(m_RIDVectorLock);

   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (i->m_iID == id)
      {
         if (AF_INET == i->m_iIPversion)
            delete (sockaddr_in*)i->m_pPeerAddr;
         else
            delete (sockaddr_in6*)i->m_pPeerAddr;

         m_lRendezvousID.erase(i);

         return;
      }
   }
}

CUDT* CRendezvousQueue::retrieve(const sockaddr* addr, UDTSOCKET& id)
{
   CGuard vg(m_RIDVectorLock);

   // TODO: optimize search
   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
   {
      if (CIPAddress::ipcmp(addr, i->m_pPeerAddr, i->m_iIPversion) && ((0 == id) || (id == i->m_iID)))
      {
         id = i->m_iID;
         return i->m_pUDT;
      }
   }

   return NULL;
}

void CRendezvousQueue::updateConnStatus()
{
   if (m_lRendezvousID.empty())
      return;

   CGuard vg(m_RIDVectorLock);

#ifdef HAI_PATCH // Remove iterator increment (to remove elements while looping)
   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end();)
#else  /* HAI_PATCH */
   for (list<CRL>::iterator i = m_lRendezvousID.begin(); i != m_lRendezvousID.end(); ++ i)
#endif /* HAI_PATCH */
   {
      // avoid sending too many requests, at most 1 request per 250ms
      if (CTimer::getTime() - i->m_pUDT->m_llLastReqTime > 250000)
      {
         if (CTimer::getTime() >= i->m_ullTTL)
         {
            // connection timer expired, acknowledge app via epoll
            i->m_pUDT->m_bConnecting = false;
            CUDT::s_UDTUnited.m_EPoll.update_events(i->m_iID, i->m_pUDT->m_sPollID, UDT_EPOLL_ERR, true);
#ifdef HAI_PATCH // BugBug
            /*
            * Setting m_bConnecting to false but keeping socket in rendezvous queue is not a good idea.
            * Next CUDT::close will not remove it from rendezvous queue (because !m_bConnecting)
            * and may crash here on next pass.
            */
            if (AF_INET == i->m_iIPversion)
               delete (sockaddr_in*)i->m_pPeerAddr;
            else
               delete (sockaddr_in6*)i->m_pPeerAddr;

            i = m_lRendezvousID.erase(i);
#endif /* HAI_PATCH */
            continue;
         }

         CPacket request;
         char* reqdata = new char [i->m_pUDT->m_iPayloadSize];
         request.pack(UMSG_HANDSHAKE, NULL, reqdata, i->m_pUDT->m_iPayloadSize);
         // ID = 0, connection request
         request.m_iID = !i->m_pUDT->m_bRendezvous ? 0 : i->m_pUDT->m_ConnRes.m_iID;
         int hs_size = i->m_pUDT->m_iPayloadSize;
         i->m_pUDT->m_ConnReq.serialize(reqdata, hs_size);
         request.setLength(hs_size);
#ifdef SRT_ENABLE_CTRLTSTAMP
         uint64_t now = CTimer::getTime();
         request.m_iTimeStamp = int(now - i->m_pUDT->m_StartTime);
         i->m_pUDT->m_llLastReqTime = now;
         i->m_pUDT->m_pSndQueue->sendto(i->m_pPeerAddr, request);
#else
         i->m_pUDT->m_pSndQueue->sendto(i->m_pPeerAddr, request);
         i->m_pUDT->m_llLastReqTime = CTimer::getTime();
#endif
         delete [] reqdata;
      }
#ifdef HAI_PATCH
      i++;
#endif /* HAI_PATCH */
   }
}

//
CRcvQueue::CRcvQueue():
    m_WorkerThread(),
    m_UnitQueue(),
    m_pRcvUList(NULL),
    m_pHash(NULL),
    m_pChannel(NULL),
    m_pTimer(NULL),
    m_iPayloadSize(),
    m_bClosing(false),
    m_ExitCond(),
    m_LSLock(),
    m_pListener(NULL),
    m_pRendezvousQueue(NULL),
    m_vNewEntry(),
    m_IDLock(),
    m_mBuffer(),
    m_PassLock(),
    m_PassCond()
{
    pthread_mutex_init(&m_PassLock, NULL);
    pthread_cond_init(&m_PassCond, NULL);
    pthread_mutex_init(&m_LSLock, NULL);
    pthread_mutex_init(&m_IDLock, NULL);
}

CRcvQueue::~CRcvQueue()
{
    m_bClosing = true;
	if (!pthread_equal(m_WorkerThread, pthread_t()))
        pthread_join(m_WorkerThread, NULL);
    pthread_mutex_destroy(&m_PassLock);
    pthread_cond_destroy(&m_PassCond);
    pthread_mutex_destroy(&m_LSLock);
    pthread_mutex_destroy(&m_IDLock);

    delete m_pRcvUList;
    delete m_pHash;
    delete m_pRendezvousQueue;

    // remove all queued messages
    for (map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.begin(); i != m_mBuffer.end(); ++ i)
    {
        while (!i->second.empty())
        {
            CPacket* pkt = i->second.front();
            delete [] pkt->m_pcData;
            delete pkt;
            i->second.pop();
        }
    }
}

void CRcvQueue::init(int qsize, int payload, int version, int hsize, CChannel* cc, CTimer* t)
{
    m_iPayloadSize = payload;

    m_UnitQueue.init(qsize, payload, version);

    m_pHash = new CHash;
    m_pHash->init(hsize);

    m_pChannel = cc;
    m_pTimer = t;

    m_pRcvUList = new CRcvUList;
    m_pRendezvousQueue = new CRendezvousQueue;

    ThreadName tn("SRT:RcvQ:worker");
    if (0 != pthread_create(&m_WorkerThread, NULL, CRcvQueue::worker, this))
    {
		m_WorkerThread = pthread_t();
        throw CUDTException(MJ_SYSTEMRES, MN_THREAD);
    }
}

void* CRcvQueue::worker(void* param)
{
   CRcvQueue* self = (CRcvQueue*)param;
   sockaddr_any sa ( self->m_UnitQueue.m_iIPversion );
   //sockaddr* addr = (AF_INET == self->m_UnitQueue.m_iIPversion) ? (sockaddr*) new sockaddr_in : (sockaddr*) new sockaddr_in6;
   int32_t id;

   THREAD_STATE_INIT("SRT Rx Queue");

   CUnit* unit = 0;
   while (!self->m_bClosing)
   {
       if ( self->worker_RetrieveUnit(id, unit, &sa) )
       {
           if ( id < 0 )
           {
               // User error on peer. May log something, but generally can only ignore it.
               // XXX Think maybe about sending some "connection rejection response".
               LOGC(mglog.Debug) << self->CONID() << "RECEIVED negative socket id '" << id << "', rejecting (POSSIBLE ATTACK)";
               continue;
           }

           // Note to rendezvous connection. This can accept:
           // - ID == 0 - take the first waiting rendezvous socket
           // - ID > 0  - find the rendezvous socket that has this ID.
           if (id == 0)
           {
               // ID 0 is for connection request, which should be passed to the listening socket or rendezvous sockets
               self->worker_ProcessConnectionRequest(unit, &sa);
           }
           else
           {
               // Otherwise ID is expected to be associated with:
               // - an enqueued rendezvous socket
               // - a socket connected to a peer
               self->worker_ProcessAddressedPacket(id, unit, &sa);
           }
       }

//TIMER_CHECK:
       // take care of the timing event for all UDT sockets

       uint64_t currtime;
       CTimer::rdtsc(currtime);

       CRNode* ul = self->m_pRcvUList->m_pUList;
       uint64_t ctime = currtime - 100000 * CTimer::getCPUFrequency();
       while ((NULL != ul) && (ul->m_llTimeStamp < ctime))
       {
           CUDT* u = ul->m_pUDT;

           if (u->m_bConnected && !u->m_bBroken && !u->m_bClosing)
           {
               u->checkTimers();
               self->m_pRcvUList->update(u);
           }
           else
           {
               // the socket must be removed from Hash table first, then RcvUList
               self->m_pHash->remove(u->m_SocketID);
               self->m_pRcvUList->remove(u);
               u->m_pRNode->m_bOnList = false;
           }

           ul = self->m_pRcvUList->m_pUList;
       }

       // Check connection requests status for all sockets in the RendezvousQueue.
       self->m_pRendezvousQueue->updateConnStatus();
   }

   /*
   if (AF_INET == self->m_UnitQueue.m_iIPversion)
      delete (sockaddr_in*)addr;
   else
      delete (sockaddr_in6*)addr;
      */

   THREAD_EXIT();
   return NULL;
}

bool CRcvQueue::worker_RetrieveUnit(int32_t& id, CUnit*& unit, sockaddr* addr)
{
#ifdef NO_BUSY_WAITING
    m_pTimer->tick();
#endif

    // check waiting list, if new socket, insert it to the list
    while (ifNewEntry())
    {
        CUDT* ne = getNewEntry();
        if (ne)
        {
            m_pRcvUList->insert(ne);
            m_pHash->insert(ne->m_SocketID, ne);
        }
    }
    // find next available slot for incoming packet
    unit = m_UnitQueue.getNextAvailUnit();
    if (!unit)
    {
        // no space, skip this packet
        CPacket temp;
        temp.m_pcData = new char[m_iPayloadSize];
        temp.setLength(m_iPayloadSize);
        THREAD_PAUSED();
        m_pChannel->recvfrom(addr, temp);
        THREAD_RESUMED();
#if ENABLE_LOGGING
        string packetinfo;
        if (temp.isControl())
        {
            packetinfo = "CONTROL: " + MessageTypeStr(temp.getType(), temp.getExtendedType());
        }
        else
        {
            ostringstream os;
            // It's hard to extract the information about peer's supported rexmit flag.
            // This is only a log, nothing crucial, so we can risk displaying incorrect message number.
            // Declaring that the peer supports rexmit flag cuts off the highest bit from
            // the displayed number.
            os << "DATA: msg=" << temp.getMsgSeq(true) << " seq=" << temp.getSeqNo();
            packetinfo = os.str();
        }
        LOGC(mglog.Error) << CONID() << "LOCAL STORAGE DEPLETED. Dropping 1 packet: " << packetinfo;

#endif
        delete [] temp.m_pcData;
        return false;
    }

    unit->m_Packet.setLength(m_iPayloadSize);

    // reading next incoming packet, recvfrom returns -1 is nothing has been received
    THREAD_PAUSED();
    if (m_pChannel->recvfrom(addr, unit->m_Packet) < 0)
    {
        THREAD_RESUMED();
        return false;
    }
    THREAD_RESUMED();
    id = unit->m_Packet.m_iID;
    return true;
}

void CRcvQueue::worker_ProcessConnectionRequest(CUnit* unit, const sockaddr* addr)
{
    // Introduced protection because it may potentially happen
    // that another thread could have closed the socket at
    // the same time and inject a bug between checking the
    // pointer for NULL and using it.
    bool have_listener = false;
    {
        CGuard cg(m_LSLock);
        if (m_pListener)
        {
            m_pListener->processConnectRequest(addr, unit->m_Packet);
            // XXX This returns some very significant return value, which
            // is completely ignored here.
            // Actually this is the only place in the code where this
            // function is being called, so it's hard to say what the
            // returned value had to serve for.

            // The tricky part is that this is something done "under the hood";
            // if any problem occurs during this process, then this will simply
            // drop the connection request. The only user process that is connected
            // to it is accept() call (or connect() in case of rendezvous), but
            // the system cannot return an error from accept() just because some
            // user was attempting to connect, but was too stupid to properly
            // formulate the connection request.

            // The only thing that could be done in case when the "listen" call
            // fails, is to probably send a short information packet (once; it's
            // not so important to make it reach the target) that the connection
            // has been rejected due to incorrectly formulated request. However
            // just in order to send anything in response, the actual sender must
            // be properly known, and this isn't the case of incorrectly formulated
            // connection request. So, we can only say sorry to ourselves and
            // do nothing.

            have_listener = true;
        }
    }

    if ( !have_listener )
        worker_TryConnectRendezvous(0, unit, addr); // 0 id because this is connection request
    else
    {
        LOGC(mglog.Note) << CONID() << "listener received connection request from: " << SockaddrToString(addr);
    }
}

void CRcvQueue::worker_ProcessAddressedPacket(int32_t id, CUnit* unit, const sockaddr* addr)
{
    CUDT* u = m_pHash->lookup(id);
    if ( !u )
    {
        // Fallback to rendezvous connection request.
        // If this still didn't make it, ignore.
        return worker_TryConnectRendezvous(id, unit, addr);
    }

    // Found associated CUDT - process this as control or data packet
    // addressed to an associated socket.
    if (!CIPAddress::ipcmp(addr, u->m_pPeerAddr, u->m_iIPversion))
    {
        LOGC(mglog.Debug) << CONID() << "Packet for SID=" << id << " asoc with " << CIPAddress::show(u->m_pPeerAddr)
            << " received from " << CIPAddress::show(addr) << " (CONSIDERED ATTACK ATTEMPT)";
        // This came not from the address that is the peer associated
        // with the socket. Reject.
        return;
    }

    if (!u->m_bConnected || u->m_bBroken || u->m_bClosing)
    {
        // The socket is currently in the process of being disconnected
        // or destroyed. Ignore.
        // XXX send UMSG_SHUTDOWN in this case?
        // XXX May it require mutex protection?
        return;
    }

    if (unit->m_Packet.isControl())
        u->processCtrl(unit->m_Packet);
    else
        u->processData(unit);

    u->checkTimers();
    m_pRcvUList->update(u);
}

void CRcvQueue::worker_TryConnectRendezvous(int32_t id, CUnit* unit, const sockaddr* addr)
{
    CUDT* u = m_pRendezvousQueue->retrieve(addr, id);
    if ( !u )
    {
        // XXX this socket is then completely unknown to the system.
        // May be nice to send some rejection info to the peer.
        if ( id == 0 )
            LOGC(mglog.Debug) << CONID() << "Rendezvous: no sockets expect connection from " << CIPAddress::show(addr) << " - POSSIBLE ATTACK";
        else
            LOGC(mglog.Debug) << CONID() << "Rendezvous: no sockets expect socket " << id << " from " << CIPAddress::show(addr) << " - POSSIBLE ATTACK";
        return;
    }

    // asynchronous connect: call connect here
    // otherwise wait for the UDT socket to retrieve this packet
    if (!u->m_bSynRecving)
    {
        u->processRendezvous(unit->m_Packet);
    }
    else
    {
        storePkt(id, unit->m_Packet.clone());
    }
}


int CRcvQueue::recvfrom(int32_t id, CPacket& packet)
{
   CGuard bufferlock(m_PassLock);

   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

   if (i == m_mBuffer.end())
   {
       uint64_t now = CTimer::getTime();
       timespec timeout;

       timeout.tv_sec = now / 1000000 + 1;
       timeout.tv_nsec = (now % 1000000) * 1000;

       pthread_cond_timedwait(&m_PassCond, &m_PassLock, &timeout);

      i = m_mBuffer.find(id);
      if (i == m_mBuffer.end())
      {
         packet.setLength(-1);
         return -1;
      }
   }

   // retrieve the earliest packet
   CPacket* newpkt = i->second.front();

   if (packet.getLength() < newpkt->getLength())
   {
      packet.setLength(-1);
      return -1;
   }

   // copy packet content
   // XXX Check if this wouldn't be better done by providing
   // copy constructor for DynamicStruct.
   memcpy(packet.m_nHeader, newpkt->m_nHeader, CPacket::HDR_SIZE);
   memcpy(packet.m_pcData, newpkt->m_pcData, newpkt->getLength());
   packet.setLength(newpkt->getLength());

   delete [] newpkt->m_pcData;
   delete newpkt;

   // remove this message from queue,
   // if no more messages left for this socket, release its data structure
   i->second.pop();
   if (i->second.empty())
      m_mBuffer.erase(i);

   return packet.getLength();
}

int CRcvQueue::setListener(CUDT* u)
{
   CGuard lslock(m_LSLock);

   if (NULL != m_pListener)
      return -1;

   m_pListener = u;
   return 0;
}

void CRcvQueue::removeListener(const CUDT* u)
{
   CGuard lslock(m_LSLock);

   if (u == m_pListener)
      m_pListener = NULL;
}

void CRcvQueue::registerConnector(const UDTSOCKET& id, CUDT* u, int ipv, const sockaddr* addr, uint64_t ttl)
{
   m_pRendezvousQueue->insert(id, u, ipv, addr, ttl);
}

void CRcvQueue::removeConnector(const UDTSOCKET& id)
{
   m_pRendezvousQueue->remove(id);

   CGuard bufferlock(m_PassLock);

   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);
   if (i != m_mBuffer.end())
   {
      while (!i->second.empty())
      {
         delete [] i->second.front()->m_pcData;
         delete i->second.front();
         i->second.pop();
      }
      m_mBuffer.erase(i);
   }
}

void CRcvQueue::setNewEntry(CUDT* u)
{
   CGuard listguard(m_IDLock);
   m_vNewEntry.push_back(u);
}

bool CRcvQueue::ifNewEntry()
{
   return !(m_vNewEntry.empty());
}

CUDT* CRcvQueue::getNewEntry()
{
   CGuard listguard(m_IDLock);

   if (m_vNewEntry.empty())
      return NULL;

   CUDT* u = (CUDT*)*(m_vNewEntry.begin());
   m_vNewEntry.erase(m_vNewEntry.begin());

   return u;
}

void CRcvQueue::storePkt(int32_t id, CPacket* pkt)
{
   CGuard bufferlock(m_PassLock);

   map<int32_t, std::queue<CPacket*> >::iterator i = m_mBuffer.find(id);

   if (i == m_mBuffer.end())
   {
      m_mBuffer[id].push(pkt);
      pthread_cond_signal(&m_PassCond);
   }
   else
   {
      //avoid storing too many packets, in case of malfunction or attack
      if (i->second.size() > 16)
         return;

      i->second.push(pkt);
   }
}
