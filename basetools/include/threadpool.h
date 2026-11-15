#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

constexpr int32_t nParallelMinWorkNs = 30000;

constexpr int32_t nItemCostUnknown = 0;
constexpr int32_t nItemCostTrivial = 1;
constexpr int32_t nItemCostSmall = 3;
constexpr int32_t nItemCostMedium = 30;
constexpr int32_t nItemCostBulk = 500;
constexpr int32_t nItemCostLarge = 2000;
constexpr int32_t nItemCostSubsystem = 20000;

class ThreadTask {
private:
	void* m_pCallable;
	void (*m_pInvoke)(void*, int32_t);

public:
	template <typename FUNC_T, typename = std::enable_if_t<not std::is_same_v<std::decay_t<FUNC_T>, ThreadTask>>>
	ThreadTask(FUNC_T&& func)
	noexcept
		: m_pCallable(const_cast<void*>(static_cast<const void*>(std::addressof(func))))
		, m_pInvoke([](void* pCallable, int32_t nThread) { (*static_cast<std::decay_t<FUNC_T>*>(pCallable))(nThread); })
	{}

	inline void operator()(int32_t nThread) const {
		m_pInvoke(m_pCallable, nThread);
	}
};

class Semaphore {
private:
	std::mutex				m_mutex;
	std::condition_variable	m_signal;
	int32_t					m_nCount;

public:
	explicit Semaphore(int32_t nCount = 0)
		: m_nCount(nCount)
	{}

	void Post(void)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_nCount++;
		}
		m_signal.notify_one();
	}

	void Wait(void) {
		std::unique_lock<std::mutex> lock(m_mutex);
		m_signal.wait(lock, [this] { return m_nCount > 0; });
		m_nCount--;
	}

	void Reset(void) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_nCount = 0;
	}
};

class ThreadPool {
private:
	std::vector<std::thread>	m_workers;
	std::mutex					m_mutex;
	std::condition_variable		m_wakeup;
	std::condition_variable		m_finished;
	std::atomic<uint32_t>		m_nGeneration;
	std::atomic<int32_t>		m_nPending;
	std::atomic<bool>			m_bShutdown;
	std::atomic<bool>			m_bRunning;
	const ThreadTask*			m_pTask;
	int32_t						m_nActive;
	int32_t						m_nThreads;

	static thread_local int32_t m_nThreadId;

	void WorkerLoop(int32_t nId, uint32_t nSeen);
	void WaitForWorkers(void);

public:
	ThreadPool();
	~ThreadPool();

	bool Create(int32_t nThreads);
	void Destroy(void);

	void Run(int32_t nThreads, const ThreadTask& task);

	inline void Run(const ThreadTask& task) {
		Run(m_nThreads, task);
	}

	inline int32_t ThreadCount(void) const {
		return m_nThreads;
	}

	inline bool Running(void) const {
		return m_bRunning.load(std::memory_order_acquire);
	}

	static inline int32_t ThreadId(void) {
		return m_nThreadId;
	}

	inline bool WorthThreading(int32_t nItems, int32_t nItemWorkNs) const
	{
		if ((m_nThreads < 2) or (nItems < 2))
			return false;
		if (nItemWorkNs <= nItemCostUnknown)
			return true;
		return int64_t(nItems) * int64_t(nItemWorkNs) >= int64_t(nParallelMinWorkNs);
	}

	static inline void ComputeRange(int32_t nId, int32_t nMax, int32_t& nStart, int32_t& nEnd, int32_t nThreads)
	{
		int32_t nRange = (nMax + nThreads - 1) / nThreads;
		nStart = nId * nRange;
		nEnd = nStart + nRange;
		if (nEnd > nMax)
			nEnd = nMax;
	}

	template <typename FUNC_T>
	void ParallelFor(int32_t nItems, FUNC_T&& func, int32_t nItemWorkNs = nItemCostUnknown, int32_t nWorkItems = -1)
	{
		if (nItems < 1)
			return;
		int32_t nThreads = m_nThreads;
		if (not WorthThreading((nWorkItems < 0) ? nItems : nWorkItems, nItemWorkNs))
			nThreads = 1;
		else if (nThreads > nItems)
			nThreads = nItems;
		auto worker = [&](int32_t nThread) {
			int32_t nStart, nEnd;
			ComputeRange(nThread, nItems, nStart, nEnd, nThreads);
			for (int32_t i = nStart; i < nEnd; i++)
				func(i, nThread);
		};
		Run(nThreads, worker);
	}
};

extern ThreadPool threadPool;

#endif // _THREADPOOL_H
