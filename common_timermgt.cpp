/*
 * common_timermgt.cpp
 *
 *  Created on: 2013��12��18��
 *      Author: jimm
 */

#include "common_datetime.h"
#include "common_errordef.h"
#include "common_timermgt.h"
#include "common_memmgt.h"


CTimerMgt::CTimerMgt()
{
	m_nTimerSeq = 0;
}

CTimerMgt::~CTimerMgt()
{

}

//��ʼ����������
int32_t CTimerMgt::Initialize()
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	m_timerPool.Initialize();
	m_timerMap.Initialize();
	return S_OK;
}

//ע����ʱ��������
int32_t CTimerMgt::Uninitialize()
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	m_timerPool.Uninitialize();
	m_timerMap.Uninitialize();
	return S_OK;
}

//��������С
int32_t CTimerMgt::GetSize()
{
	return sizeof(*this);
}

int32_t CTimerMgt::CreateTimer(TimerProc Proc, CObject *pTimer, ITimerData *pTimerData, int64_t nCycleTime, bool bLoop, TimerIndex& timerIndex)
{
	uint8_t *pObj = NULL;
	if(pTimerData != NULL)
	{
		int32_t nTimerDataSize = pTimerData->GetSize();
		pObj = MALLOC(nTimerDataSize);//g_FrameMemMgt.AllocBlock(nTimerDataSize);
		if(pObj == NULL)
		{
			return E_UNKNOWN;
		}

		memcpy(pObj, pTimerData, nTimerDataSize);
	}

	//��ʼ����ʱ������
	Timer timer;
	timer.nTimerID = 0;
	timer.pData = pObj;
	timer.nStartTime = CTimeValue::CurrentTime().Microseconds();
	timer.nCycleTime = nCycleTime;
	timer.nEndTime = timer.nStartTime + timer.nCycleTime;
	timer.bLoop = bLoop;
	timer.pTimerHandler = pTimer;
	timer.pTimerProc = Proc;
	timer.nFiredCount = 0;

	MUTEX_GUARD(TimerLock, m_stTimerLock);
	//����Timer����ʱ��Seq
	timer.nTimerSeq = m_nTimerSeq;
	//����TimerSeq;
	++m_nTimerSeq;

	//������ʱ������
	TimerPool::CIndex* pIndex = m_timerPool.CreateObject();
	if (NULL == pIndex)
	{
		FREE(pObj);
		return E_UNKNOWN;
	}

	//������ʱ������
	TimerMap::CIndex* pMapIndex = m_timerMap.Insert(timer.nEndTime, pIndex->Index());
	if (NULL == pMapIndex)
	{
		FREE(pObj);
		m_timerPool.DestroyObject(pIndex);
		pIndex = NULL;
		return E_UNKNOWN;
	}

	//���������浽�������ݱ���
	int32_t ret = pIndex->SetAdditionalData(enmAdditionalIndex_RBTreeIndex, (uint64_t)pMapIndex);
	if (0 > ret)
	{
		FREE(pObj);
		m_timerMap.Erase(pMapIndex);
		m_timerPool.DestroyObject(pIndex);
		pIndex = NULL;
		return ret;
	}

	//���ö�ʱ������
	pIndex->ObjectPtr()->SetTimer(timer);
	timerIndex = pIndex->Index();

	return S_OK;
}

//ɾ����ʱ��
int32_t CTimerMgt::RemoveTimer(TimerIndex timerIndex)
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	TimerPool::CIndex* pIndex = m_timerPool.GetIndex(timerIndex);
	if (NULL == pIndex)
	{
		return E_UNKNOWN;
	}

	return RemoveTimer(pIndex);
}

//��ն�ʱ��������
int32_t CTimerMgt::Clear()
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	m_timerPool.Clear();
	m_timerMap.Clear();
	return S_OK;
}

//��ʱ���Ѵ���
int32_t CTimerMgt::TimerFired(TimerIndex timerIndex, uint32_t timerSeq)
{
	CTimer *pTimer = NULL;
	int32_t nRet = GetTimer(timerIndex,pTimer);
	if( nRet == S_OK && pTimer->GetTimerSeq() == timerSeq)
	{
		return TimerFired(timerIndex);
	}

	return S_FALSE;
}


//��ȡ��ʱ��
int32_t CTimerMgt::GetTimer(TimerIndex timerIndex, CTimer*& pTimer)
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	TimerPool::CIndex* pIndex = m_timerPool.GetIndex(timerIndex);
	if (NULL == pIndex)
	{
		return E_UNKNOWN;
	}

	pTimer = pIndex->ObjectPtr();

	return S_OK;
}

//ɾ����ʱ��
int32_t CTimerMgt::RemoveTimer(TimerPool::CIndex* pIndex)
{
	if (NULL == pIndex)
	{
		return E_UNKNOWN;
	}

	CTimer* pTimer = pIndex->ObjectPtr();
	if(pTimer == NULL)
	{
		return E_NULLPOINTER;
	}
	//���ն�ʱ����������ڴ���Դ
	Timer timer;
	pTimer->GetTimer(timer);
	if(timer.pData != NULL)
	{
		FREE((uint8_t *)timer.pData);//g_FrameMemMgt.RecycleBlock((uint8_t *)timer.pData);
	}

	uint64_t nAddtionalValue = 0;
	pIndex->GetAdditionalData(enmAdditionalIndex_RBTreeIndex, nAddtionalValue);
	TimerMap::CIndex* pMapIndex = (TimerMap::CIndex*)nAddtionalValue;

	//����ʱ���Ӷ���ؼ���������ɾ��
//	MUTEX_GUARD(TimerLock, m_stTimerLock);
	m_timerMap.Erase(pMapIndex);
	m_timerPool.DestroyObject(pIndex);

	return S_OK;
}

//��ʱ���Ѵ���
int32_t CTimerMgt::TimerFired(TimerIndex timerIndex)
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	TimerPool::CIndex* pIndex = m_timerPool.GetIndex(timerIndex);
	if (NULL == pIndex)
	{
		return E_UNKNOWN;
	}

	CTimer* pTimer = pIndex->ObjectPtr();
	if(pTimer == NULL)
	{
		return E_NULLPOINTER;
	}

	if (!pTimer->IsLoop())
	{
		//������ѭ��������ɾ����ʱ��
		return RemoveTimer(pIndex);
	}

	Timer timer;
	pTimer->GetTimer(timer);

	//���¶�ʱ������
	timer.nEndTime = CTimeValue::CurrentTime().Microseconds() + timer.nCycleTime;
	++timer.nFiredCount;
	pTimer->SetTimer(timer);

	//������ʱ������
	uint64_t nAddtionalValue = 0;
	pIndex->GetAdditionalData(enmAdditionalIndex_RBTreeIndex, nAddtionalValue);
	TimerMap::CIndex* pMapIndex = (TimerMap::CIndex*)nAddtionalValue;

	m_timerMap.Erase(pMapIndex);
	pMapIndex = m_timerMap.Insert(timer.nEndTime, pIndex->Index());
	if (NULL == pMapIndex)
	{
		m_timerPool.DestroyObject(pIndex);
		return E_UNKNOWN;
	}

	//���������浽�������ݱ���
	int32_t ret = pIndex->SetAdditionalData(enmAdditionalIndex_RBTreeIndex, (uint64_t)pMapIndex);
	if (0 > ret)
	{
		m_timerMap.Erase(pMapIndex);
		m_timerPool.DestroyObject(pIndex);
		return ret;
	}

	return S_OK;
}

//��ȡ����ʱ����С�Ķ�ʱ��
int32_t CTimerMgt::GetFirstTimer(CTimer*& pTimer, TimerIndex& timerIndex)
{
	MUTEX_GUARD(TimerLock, m_stTimerLock);
	TimerMap::CIndex* pMapIndex = m_timerMap.First();
	if (NULL == pMapIndex)
	{
		return E_UNKNOWN;
	}

	TimerPool::CIndex* pIndex = m_timerPool.GetIndex( pMapIndex->Object() );
	if (NULL == pIndex)
	{
		//�����б��д�����Ч������ɾ��
		m_timerMap.Erase(pMapIndex);
		return E_UNKNOWN;
	}

	pTimer = pIndex->ObjectPtr();
	timerIndex = pIndex->Index();

	return S_OK;
}

//����ʱ���¼�
bool CTimerMgt::Process()
{
	CTimer *pTimer = NULL;
	TimerIndex timerIndex = -1;

	//��ȡ��ʱ���б��еĽ���ʱ������Ķ�ʱ��
	int32_t ret = GetFirstTimer(pTimer, timerIndex);
	if (0 > ret)
	{
		return false;
	}

	//��ʱ������ʱ��С�ڵ�ǰʱ��
	if (pTimer->GetEndTime() > CTimeValue::CurrentTime().Microseconds())
	{
		return false;
	}

	//����TimerSeq ���ڱ���
	uint32_t nTimerSeq = pTimer->GetTimerSeq();

	//�ⲿTimerֱ�ӻص��ӿ�
	CObject *pHandler = pTimer->GetTimerHandler();
	if( NULL != pHandler)
	{
		TimerProc proc = pTimer->GetEventProc();
		if(proc != NULL)
		{
			(pHandler->*proc)(pTimer);
		}
	}

	//��ʱ���¼��Ѵ���
	TimerFired(timerIndex,nTimerSeq);

	return true;
}
