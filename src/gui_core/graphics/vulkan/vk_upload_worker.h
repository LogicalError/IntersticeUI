#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tachyon::vulkan
{
    /// @addtogroup graphics_vulkan
    /// @{

    /// Library internal: copies into mapped GPU memory on a thread of its own, so the main
    /// thread goes on recording commands. The copies only have to be done before the
    /// submit that reads them: call Wait before it.
    class UploadWorker
    {
    public:
        /// Copies this small run on the calling thread: handing them over costs more.
        static constexpr size_t kInlineBytes = 32 * 1024;

        UploadWorker();
        ~UploadWorker();
        UploadWorker(const UploadWorker&) = delete;
        UploadWorker& operator=(const UploadWorker&) = delete;

        /// Copies `size` bytes from `source` to `destination`; a null source zeroes them. The
        /// source stays the caller's and must stay valid until Wait returns.
        void Copy(void* destination, const void* source, size_t size);
        /// As Copy, from bytes the worker keeps until it has copied them.
        void Copy(void* destination, std::vector<std::byte>&& source);
        /// Runs `job` on the worker after every copy queued before it (flushing what they
        /// wrote), or now when nothing is queued.
        void Then(std::function<void()> job);
        /// Returns once every copy and job queued so far is done.
        void Wait();
        /// Whether anything queued isn't done yet.
        bool Busy() const;

    private:
        struct Job
        {
            void*                  destination = nullptr;
            const void*            source      = nullptr;
            size_t                 size        = 0;
            std::vector<std::byte> owned;
            std::function<void()>  then;
        };
        void Push(Job&& job);
        void Run();

        mutable std::mutex      m_mutex;
        std::condition_variable m_wake;  ///< a job queued, or stopping
        std::condition_variable m_done;  ///< the queue ran empty
        std::deque<Job>         m_jobs;
        bool                    m_running = false; ///< the worker is in a job
        bool                    m_stop    = false;
        std::thread             m_thread;
    };
    /// @}
}
