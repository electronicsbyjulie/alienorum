#ifndef ALIENORUM_CLOUDS_GPU_H
#define ALIENORUM_CLOUDS_GPU_H

namespace alienorum
{
    struct CloudSkyGpuInput
    {
        float camera[16];
        float world[16];
        float light[16];
        unsigned int texture;
        unsigned int revision;
        const float *refraction;
    };

    bool queue_cloud_sky_gpu(const CloudSkyGpuInput &in);
}
#endif
