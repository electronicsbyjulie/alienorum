#ifndef ALIENORUM_CLOUDS_GPU_H
#define ALIENORUM_CLOUDS_GPU_H

namespace alienorum
{
    enum class CloudGeometryMode
    {
        accelerated,
        reference,
        all_clusters
    };

    struct CloudSkyGpuStats
    {
        // GPU durations come from completed queries, without waiting for them.
        bool timers_available = false;
        bool geometry_cached = false;
        double geometry_ms = 0;
        double render_ms = 0;
        double composite_ms = 0;
        double tile_cpu_ms = 0;
        unsigned int geometry_builds = 0;
        unsigned int image_builds = 0;
        unsigned int image_hits = 0;
        unsigned int clusters = 0;
        unsigned int max_tile_clusters = 0;
        double mean_tile_clusters = 0;
    };

    struct CloudSkyGpuInput
    {
        float camera[16];
        float world[16];
        float light[16];
        unsigned int texture;
        unsigned int revision;
        const float *refraction;
        // Reference modes support image comparisons without changing cloud shapes.
        CloudGeometryMode geometry_mode = CloudGeometryMode::accelerated;
    };

    bool queue_cloud_sky_gpu(const CloudSkyGpuInput &in);
    const CloudSkyGpuStats &cloud_sky_gpu_stats();
}
#endif
