#include "universe_fixture.h"
#include "../background_file.h"
#include "../performance_options.h"
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <thread>

using namespace alienorum;

class IoPerformanceTest : public UniverseFixture
{
    protected:
    std::filesystem::path directory;
    void SetUp() override
    {
        UniverseFixture::SetUp();
        directory = std::filesystem::temp_directory_path() / ("alienorum-io-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }
    void TearDown() override
    {
        Serialization::finish_file_saves();
        delete_the_universe();
        std::error_code error;
        std::filesystem::remove_all(directory, error);
        UniverseFixture::TearDown();
    }
    static std::string contents(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), {});
    }
    static uint64_t hash(const std::string& text)
    {
        uint64_t result = 14695981039346656037ULL;
        for (unsigned char byte : text) { result ^= byte; result *= 1099511628211ULL; }
        return result;
    }
};

TEST_F(IoPerformanceTest, SaveSnapshotOwnsDataAndPublishesInOrder)
{
    auto* star = make_star();
    star->user_added = true;
    const auto path = directory / "save.json";
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, false));
    const auto expected = contents(path);
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, true));
    star->temperature += 100;
    ASSERT_TRUE(Serialization::finish_file_saves());
    EXPECT_EQ(contents(path), expected);
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, true));
    const auto second = Serialization::snapshot(cels, true).dump();
    star->temperature += 100;
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, true));
    const auto third = Serialization::snapshot(cels, true).dump();
    EXPECT_NE(second, third);
    ASSERT_TRUE(Serialization::finish_file_saves());
    EXPECT_EQ(contents(path), third);
}

TEST_F(IoPerformanceTest, FailedPublicationPreservesPriorFileAndWorkerRecovers)
{
    const auto path = directory / "prior.json";
    { std::ofstream file(path); file << "prior"; }
    EXPECT_FALSE(publish_file(path, [](std::ostream& stream) { stream << "partial"; return false; }));
    EXPECT_EQ(contents(path), "prior");
    EXPECT_FALSE(publish_file(path, [](std::ostream&) -> bool { throw std::runtime_error("test"); }));
    EXPECT_EQ(contents(path), "prior");
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()), 1);
    auto* star = make_star();
    star->user_added = true;
    ASSERT_TRUE(Serialization::save_file((directory / "absent" / "save.json").string(), cels, true, true));
    EXPECT_FALSE(Serialization::finish_file_saves());
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, true));
    ASSERT_TRUE(Serialization::finish_file_saves());
    EXPECT_EQ(contents(path), Serialization::snapshot(cels, true).dump());
}

TEST_F(IoPerformanceTest, OwnedQueueWaitsBeforeCapturingAndJoinsAtDestruction)
{
    std::promise<void> release;
    auto gate = release.get_future().share();
    std::atomic<bool> captured{false}, finished{false};
    {
        BackgroundFile writer;
        ASSERT_TRUE(writer.submit([&]() { return [gate]() { gate.wait(); return true; }; }));
        std::thread producer([&]()
        {
            writer.submit([&]()
            {
                captured = true;
                return [&]() { finished = true; return true; };
            });
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        EXPECT_FALSE(captured.load());
        release.set_value();
        producer.join();
    }
    EXPECT_TRUE(captured.load());
    EXPECT_TRUE(finished.load());
}

TEST_F(IoPerformanceTest, PairedProductionPaths)
{
    if (!std::getenv("ALIENORUM_RUN_PERF")) GTEST_SKIP();
    const char* output = std::getenv("ALIENORUM_PERF_OUTPUT");
    ASSERT_NE(output, nullptr);
#ifdef ALIENORUM_PERFORMANCE_FIXED
    std::cout << "PERF comparison: fixed\n";
#endif
    for (int i = 0; i < 5000; ++i)
    {
        const auto name = "Synthetic " + std::to_string(i);
        auto* star = make_star(name.c_str());
        star->user_added = true;
        star->location.local_position = Point(i * light_year, i * .3 * light_year, i * .7 * light_year);
    }
    const auto path = directory / "benchmark.json";
    const int frames = std::getenv("ALIENORUM_PERF_FRAMES") ? std::max(1, atoi(std::getenv("ALIENORUM_PERF_FRAMES"))) : 4;
    double producer = 0, completion = 0;
    for (int i = -1; i < frames; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, performance_enabled(94)));
        const auto returned = std::chrono::steady_clock::now();
        ASSERT_TRUE(Serialization::finish_file_saves());
        const auto finished = std::chrono::steady_clock::now();
        if (i >= 0)
        {
            producer += std::chrono::duration<double, std::milli>(returned - start).count() / frames;
            completion += std::chrono::duration<double, std::milli>(finished - start).count() / frames;
        }
    }
    const auto bytes = contents(path);
    ASSERT_EQ(bytes, Serialization::snapshot(cels, true).dump());
    std::ofstream csv(output);
    csv << "proposal,workload,round,order,before_ms,after_ms,change_percent,max_channel_error,before_cpu_ms,after_cpu_ms,before_max_ms,after_max_ms\n";
    csv << std::setprecision(12);
    for (const auto& sample : {std::make_pair("save-interactive", producer), std::make_pair("save-completion", completion)})
    {
        csv << "94," << sample.first << ",0,AB," << sample.second << ',' << sample.second << ",0,0,0,0,,\n";
        std::cout << "PERF IMAGE P94 " << sample.first << ' ' << hash(bytes) << '\n';
    }
    std::cout << "PERF save objects: 5000; output bytes: " << bytes.size() << '\n';
}
