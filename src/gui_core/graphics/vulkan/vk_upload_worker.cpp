#include <gui_core/graphics/vulkan/vk_upload_worker.h>

#include <cstring>

namespace tachyon::vulkan
{
    UploadWorker::UploadWorker()
        : m_thread([this] { Run(); })
    {
    }

    UploadWorker::~UploadWorker()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_one();
        m_thread.join();
    }

    void UploadWorker::Copy(void* destination, const void* source, size_t size)
    {
        if (size == 0)
            return;
        if (size < kInlineBytes && !Busy())
        {
            if (source == nullptr) std::memset(destination, 0, size);
            else                   std::memcpy(destination, source, size);
            return;
        }
        Push(Job{ .destination = destination, .source = source, .size = size });
    }

    void UploadWorker::Copy(void* destination, std::vector<std::byte>&& source)
    {
        if (source.empty())
            return;
        if (source.size() < kInlineBytes && !Busy())
        {
            std::memcpy(destination, source.data(), source.size());
            return;
        }
        const size_t size = source.size();
        Push(Job{ .destination = destination, .size = size, .owned = std::move(source) });
    }

    void UploadWorker::Then(std::function<void()> job)
    {
        if (!Busy())
        {
            job();
            return;
        }
        Push(Job{ .then = std::move(job) });
    }

    void UploadWorker::Wait()
    {
        std::unique_lock lock(m_mutex);
        m_done.wait(lock, [this] { return m_jobs.empty() && !m_running; });
    }

    bool UploadWorker::Busy() const
    {
        std::lock_guard lock(m_mutex);
        return !m_jobs.empty() || m_running;
    }

    void UploadWorker::Push(Job&& job)
    {
        {
            std::lock_guard lock(m_mutex);
            m_jobs.push_back(std::move(job));
        }
        m_wake.notify_one();
    }

    void UploadWorker::Run()
    {
        std::unique_lock lock(m_mutex);
        for (;;)
        {
            m_wake.wait(lock, [this] { return m_stop || !m_jobs.empty(); });
            if (m_jobs.empty())
                return;   // stopping, with nothing left to copy
            Job job = std::move(m_jobs.front());
            m_jobs.pop_front();
            m_running = true;
            lock.unlock();

            if (job.then)
                job.then();
            else if (!job.owned.empty())
                std::memcpy(job.destination, job.owned.data(), job.size);
            else if (job.source == nullptr)
                std::memset(job.destination, 0, job.size);
            else
                std::memcpy(job.destination, job.source, job.size);

            lock.lock();
            m_running = false;
            if (m_jobs.empty())
                m_done.notify_all();
        }
    }
}
