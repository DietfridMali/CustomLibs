#include "threadpool.h"

ThreadPool threadPool;

thread_local int32_t ThreadPool::m_nThreadId = 0;

static const int32_t		nSpinRounds = 256;
static constexpr uint32_t	cActiveBits = 8;
static constexpr uint32_t	cActiveMask = (1u << cActiveBits) - 1;

// =================================================================================================

ThreadPool::ThreadPool()
	: m_nGeneration(0)
	, m_nPending(0)
	, m_bShutdown(false)
	, m_bRunning(false)
	, m_pTask(nullptr)
	, m_nActive(0)
	, m_nThreads(1)
{
}

// =================================================================================================

ThreadPool::~ThreadPool()
{
	Destroy();
}

// =================================================================================================

bool ThreadPool::Create(int32_t nThreads)
{
	Destroy();
	if (nThreads < 1)
		nThreads = 1;
	m_nThreads = 1;
	if (nThreads < 2)
		return true;
	uint32_t nGeneration = m_nGeneration.load(std::memory_order_acquire);

	try {
		m_workers.reserve(size_t(nThreads - 1));
		for (int32_t i = 1; i < nThreads; i++)
			m_workers.emplace_back(&ThreadPool::WorkerLoop, this, i, nGeneration);
	}
	catch (...) {
	}
	m_nThreads = int32_t(m_workers.size()) + 1;
	return m_nThreads > 1;
}

// =================================================================================================

void ThreadPool::Destroy(void)
{
	if (m_workers.empty()) {
		m_nThreads = 1;
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_bShutdown.store(true, std::memory_order_relaxed);
		m_nGeneration.fetch_add(1, std::memory_order_release);
	}
	m_wakeup.notify_all();
	for (std::thread& worker : m_workers)
		if (worker.joinable())
			worker.join();
	m_workers.clear();
	m_bShutdown.store(false, std::memory_order_relaxed);
	m_bRunning.store(false, std::memory_order_relaxed);
	m_pTask = nullptr;
	m_nActive = 0;
	m_nThreads = 1;
}

// =================================================================================================

void ThreadPool::WorkerLoop(int32_t nId, uint32_t nSeen)
{
	m_nThreadId = nId;
	for (;;) {
		uint32_t nGeneration = nSeen;

		for (int32_t i = 0; i < nSpinRounds; i++) {
			nGeneration = m_nGeneration.load(std::memory_order_acquire);
			if (nGeneration != nSeen)
				break;
			std::this_thread::yield();
		}
		if (nGeneration == nSeen) {
			std::unique_lock<std::mutex> lock(m_mutex);
			m_wakeup.wait(lock, [this, nSeen] { return m_nGeneration.load(std::memory_order_acquire) != nSeen; });
			nGeneration = m_nGeneration.load(std::memory_order_acquire);
		}
		nSeen = nGeneration;
		if (m_bShutdown.load(std::memory_order_relaxed))
			return;
		if (nId >= int32_t(nGeneration & cActiveMask))
			continue;
		(*m_pTask)(nId);
		if (m_nPending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_finished.notify_one();
		}
	}
}

// =================================================================================================

void ThreadPool::WaitForWorkers(void)
{
	for (int32_t i = 0; i < nSpinRounds; i++) {
		if (not m_nPending.load(std::memory_order_acquire))
			return;
		std::this_thread::yield();
	}
	std::unique_lock<std::mutex> lock(m_mutex);
	m_finished.wait(lock, [this] { return m_nPending.load(std::memory_order_acquire) == 0; });
}

// =================================================================================================

void ThreadPool::Run(int32_t nThreads, const ThreadTask& task)
{
	if (nThreads < 1)
		return;
	if ((nThreads > 1) and (nThreads <= m_nThreads) and (nThreads <= int32_t(cActiveMask)) and not m_workers.empty() and
		not m_bRunning.exchange(true, std::memory_order_acq_rel)) {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_pTask = &task;
			m_nActive = nThreads;
			m_nPending.store(nThreads - 1, std::memory_order_relaxed);
			m_nGeneration.store((((m_nGeneration.load(std::memory_order_relaxed) >> cActiveBits) + 1) << cActiveBits) |
									uint32_t(nThreads),
								std::memory_order_release);
		}
		m_wakeup.notify_all();
		task(0);
		WaitForWorkers();
		m_bRunning.store(false, std::memory_order_release);
		return;
	}
	for (int32_t i = 0; i < nThreads; i++)
		task(i);
}

// =================================================================================================
