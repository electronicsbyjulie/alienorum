#pragma once

#include "point.h"

namespace alienorum
{
    struct SystemViewCamera
    {
        double azimuth = 0, altitude = 0, yaw = 0, pitch = 0;

        explicit SystemViewCamera(Point light, double pan_yaw = 0, double pan_pitch = 0)
            : yaw(pan_yaw), pitch(pan_pitch)
        {
            const double length = light.magnitude();
            light = length > 0 && std::isfinite(length) ? light * (1 / length) : Point(0, 0, -1);
            // First face the actual star, then turn 45 degrees for a three-quarter disc.
            azimuth = atan2(-light.x, -light.z);
            altitude = atan2(-light.y, hypot(light.x, light.z));
            yaw -= _pi / 4;
        }

        Point to_camera(Point point) const
        {
            point = rotate3D(point, Point(), Point(0, 1, 0), -azimuth);
            point = rotate3D(point, Point(), Point(1, 0, 0), altitude);
            point = rotate3D(point, Point(), Point(0, 1, 0), -yaw);
            return rotate3D(point, Point(), Point(1, 0, 0), pitch);
        }

        Point from_camera(Point point) const
        {
            point = rotate3D(point, Point(), Point(1, 0, 0), -pitch);
            point = rotate3D(point, Point(), Point(0, 1, 0), yaw);
            point = rotate3D(point, Point(), Point(1, 0, 0), -altitude);
            return rotate3D(point, Point(), Point(0, 1, 0), azimuth);
        }
    };
}
