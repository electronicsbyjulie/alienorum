#include "universe_fixture.h"
#include "../background_file.h"
#include "../snapshot_io.h"
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
        snapshot_writer().finish();
        layout_writer().finish();
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

TEST_F(IoPerformanceTest, UniverseFilesAreAlwaysPrettyPrinted)
{
    auto* star = make_star("Readable primary");
    star->user_added = true;
    make_planet(star, "Readable planet")->user_edited = true;
    for (bool edited : {false, true})
    {
        const auto expected = Serialization::snapshot(cels, edited);
        for (bool background : {false, true})
        {
            const auto path = directory / "readable.json";
            ASSERT_TRUE(Serialization::save_file(path.string(), cels, edited, background));
            ASSERT_TRUE(Serialization::finish_file_saves());
            const auto bytes = contents(path);
            EXPECT_EQ(bytes, expected.dump(4));
            EXPECT_NE(bytes.find("\n    \""), std::string::npos);
            EXPECT_EQ(json::parse(bytes), expected);
        }
    }
}

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
    const auto second = Serialization::snapshot(cels, true).dump(4);
    star->temperature += 100;
    ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, true));
    const auto third = Serialization::snapshot(cels, true).dump(4);
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
    EXPECT_EQ(contents(path), Serialization::snapshot(cels, true).dump(4));
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
    const bool png_case = std::getenv("ALIENORUM_PERF_CASES") && std::string(std::getenv("ALIENORUM_PERF_CASES")) == "95";
    std::vector<unsigned char> pixels(1280 * 720 * 3);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = (i * 31 + i / 997) & 255;
    for (int i = 0; i < (png_case ? 0 : 5000); ++i)
    {
        const auto name = "Synthetic " + std::to_string(i);
        auto* star = make_star(name.c_str());
        star->user_added = true;
        star->location.local_position = Point(i * light_year, i * .3 * light_year, i * .7 * light_year);
    }
    const auto path = directory / (png_case ? "benchmark.png" : "benchmark.json");
    const int frames = std::getenv("ALIENORUM_PERF_FRAMES") ? std::max(1, atoi(std::getenv("ALIENORUM_PERF_FRAMES"))) : 4;
    double producer = 0, completion = 0;
    for (int i = -1; i < frames; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        if (png_case)
        {
            ASSERT_TRUE(save_snapshot_pixels(path.string(), 1280, 720, pixels, PERF_P095));
        }
        else
        {
            ASSERT_TRUE(Serialization::save_file(path.string(), cels, true, performance_enabled(94)));
        }
        const auto returned = std::chrono::steady_clock::now();
        if (png_case)
        {
            ASSERT_TRUE(snapshot_writer().finish());
        }
        else
        {
            ASSERT_TRUE(Serialization::finish_file_saves());
        }
        const auto finished = std::chrono::steady_clock::now();
        if (i >= 0)
        {
            producer += std::chrono::duration<double, std::milli>(returned - start).count() / frames;
            completion += std::chrono::duration<double, std::milli>(finished - start).count() / frames;
        }
    }
    const auto bytes = contents(path);
    if (!png_case)
    {
        ASSERT_EQ(bytes, Serialization::snapshot(cels, true).dump(4));
    }
    std::ofstream csv(output);
    csv << "proposal,workload,round,order,before_ms,after_ms,change_percent,max_channel_error,before_cpu_ms,after_cpu_ms,before_max_ms,after_max_ms\n";
    csv << std::setprecision(12);
    for (const auto& sample : {std::make_pair("save-interactive", producer), std::make_pair("save-completion", completion)})
    {
        csv << (png_case ? "95," : "94,") << sample.first << ",0,AB," << sample.second << ',' << sample.second << ",0,0,0,0,,\n";
        std::cout << (png_case ? "PERF IMAGE P95 " : "PERF IMAGE P94 ") << sample.first << ' ' << hash(bytes) << '\n';
    }
    std::cout << "PERF save objects: 5000; output bytes: " << bytes.size() << '\n';
}

TEST_F(IoPerformanceTest, SnapshotPixelsPreserveOrientationBytesAndOrderedOverwrite)
{
    const int width = 17, height = 9;
    std::vector<unsigned char> pixels(width * height * 3);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = (i * 37 + 13) & 255;
    const auto old = directory / "original.png", path = directory / "snapshot.png";
    ASSERT_TRUE(save_snapshot_pixels(old.string(), width, height, pixels, false));
    ASSERT_TRUE(save_snapshot_pixels(path.string(), width, height, pixels, true));
    ASSERT_TRUE(snapshot_writer().finish());
    EXPECT_EQ(contents(old), contents(path));
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    ASSERT_TRUE(png_image_begin_read_from_file(&image, path.string().c_str()));
    image.format = PNG_FORMAT_RGB;
    std::vector<unsigned char> decoded(PNG_IMAGE_SIZE(image));
    ASSERT_TRUE(png_image_finish_read(&image, nullptr, decoded.data(), 0, nullptr));
    png_image_free(&image);
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width * 3; ++column)
            EXPECT_EQ(decoded[row * width * 3 + column], pixels[(height - row - 1) * width * 3 + column]);
    ASSERT_TRUE(save_snapshot_pixels(path.string(), width, height, pixels, true));
    std::fill(pixels.begin(), pixels.end(), 123);
    ASSERT_TRUE(save_snapshot_pixels(path.string(), width, height, pixels, true));
    ASSERT_TRUE(snapshot_writer().finish());
    ASSERT_TRUE(save_snapshot_pixels(old.string(), width, height, pixels, false));
    EXPECT_EQ(contents(old), contents(path));
    EXPECT_FALSE(save_snapshot_pixels(path.string(), 0, height, {}, true));
    EXPECT_FALSE(save_snapshot_pixels(path.string(), width, height, {}, true));
    ASSERT_TRUE(save_snapshot_pixels((directory / "absent" / "image.png").string(), width, height, pixels, true));
    EXPECT_FALSE(snapshot_writer().finish());
}

TEST_F(IoPerformanceTest, LayoutTextRetainsExactLatestSettings)
{
    const auto path = directory / "layout.ini";
    std::string text = "[Window][Test]\nPos=24,35\nSize=700,400\nCollapsed=0\n";
    ASSERT_TRUE(save_layout_text(path, text));
    text += "\n[Window][Second]\nPos=10,20\nSize=200,300\nCollapsed=0\n";
    ASSERT_TRUE(save_layout_text(path, text));
    ASSERT_TRUE(layout_writer().finish());
    EXPECT_EQ(contents(path), text);
}

TEST_F(IoPerformanceTest, WriterReleasesOwnedPayloadOnWorkerThread)
{
    struct Payload
    {
        std::thread::id& destroyed_on;
        explicit Payload(std::thread::id& result) : destroyed_on(result) {}
        ~Payload() { destroyed_on = std::this_thread::get_id(); }
    };
    std::thread::id destroyed_on;
    BackgroundFile writer;
    ASSERT_TRUE(writer.submit([&]()
    {
        return [payload = std::make_unique<Payload>(destroyed_on)]() { return bool(payload); };
    }));
    ASSERT_TRUE(writer.finish());
    EXPECT_NE(destroyed_on, std::thread::id());
    EXPECT_NE(destroyed_on, std::this_thread::get_id());
}
