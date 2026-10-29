// The Vulkan upload worker: copies into memory on a thread of its own, done by Wait.

#include <gtest/gtest.h>

#include <gui_core/graphics/vulkan/vk_upload_worker.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace
{
    using tachyon::vulkan::UploadWorker;

    std::vector<std::byte> Pattern(size_t size, int seed)
    {
        std::vector<std::byte> bytes(size);
        for (size_t i = 0; i < size; ++i)
            bytes[i] = std::byte((i * 31 + size_t(seed)) & 0xFF);
        return bytes;
    }

    // A big copy goes to the worker, which runs the job after it there; Wait returns once
    // both are done.
    TEST(UploadWorkerTest, BigCopiesRunOnTheWorker)
    {
        UploadWorker worker;
        const auto source = Pattern(8 * 1024 * 1024, 1);
        std::vector<std::byte> destination(source.size());

        std::thread::id flushedOn;
        bool copiedBeforeFlush = false;
        worker.Copy(destination.data(), source.data(), source.size());
        worker.Then([&] { flushedOn = std::this_thread::get_id(); copiedBeforeFlush = destination == source; });
        worker.Wait();

        EXPECT_FALSE(worker.Busy());
        EXPECT_EQ(destination, source);
        EXPECT_TRUE(copiedBeforeFlush) << "the job runs after the copies queued before it";
        EXPECT_NE(flushedOn, std::this_thread::get_id()) << "on the worker";
    }

    // A small copy, with nothing queued, is done on the calling thread before Copy returns,
    // and so is a job after it.
    TEST(UploadWorkerTest, SmallCopiesRunInline)
    {
        UploadWorker worker;
        const auto source = Pattern(UploadWorker::kInlineBytes - 1, 2);
        std::vector<std::byte> destination(source.size());
        worker.Copy(destination.data(), source.data(), source.size());
        EXPECT_EQ(destination, source);

        std::thread::id ranOn;
        worker.Then([&] { ranOn = std::this_thread::get_id(); });
        EXPECT_EQ(ranOn, std::this_thread::get_id());
    }

    // A null source zeroes; an owned source may go once Copy returns; many copies all land.
    TEST(UploadWorkerTest, ZeroesOwnedSourcesAndManyCopies)
    {
        UploadWorker worker;
        std::vector<std::byte> zeroed(4 * 1024 * 1024, std::byte(0xAB));
        worker.Copy(zeroed.data(), nullptr, zeroed.size());

        const auto expected = Pattern(1024 * 1024, 3);
        std::vector<std::byte> fromOwned(expected.size());
        worker.Copy(fromOwned.data(), std::vector<std::byte>(expected));

        std::vector<std::vector<std::byte>> sources, destinations;
        for (int i = 0; i < 64; ++i)
        {
            sources.push_back(Pattern(size_t(40 * 1024 + i * 1000), i));
            destinations.emplace_back(sources.back().size());
        }
        for (size_t i = 0; i < sources.size(); ++i)
            worker.Copy(destinations[i].data(), sources[i].data(), sources[i].size());
        worker.Wait();

        EXPECT_EQ(zeroed, std::vector<std::byte>(zeroed.size()));
        EXPECT_EQ(fromOwned, expected);
        for (size_t i = 0; i < sources.size(); ++i)
            EXPECT_EQ(destinations[i], sources[i]) << i;
    }

    // Destroying the worker finishes what was queued.
    TEST(UploadWorkerTest, DestroyingFinishesTheQueue)
    {
        const auto source = Pattern(8 * 1024 * 1024, 4);
        std::vector<std::byte> destination(source.size());
        std::atomic<bool> ran = false;
        {
            UploadWorker worker;
            worker.Copy(destination.data(), source.data(), source.size());
            worker.Then([&] { ran = true; });
        }
        EXPECT_EQ(destination, source);
        EXPECT_TRUE(ran);
    }
}
