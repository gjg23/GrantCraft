#pragma once

#include "generation/device_shared/host_device.hpp"

#define SPLINE_N 7

HD FINLINE float heightSpline(float t)
{
    constexpr float xs[SPLINE_N] = {
        0.0f, 0.15f, 0.35f, 0.50f,
        0.65f, 0.80f, 1.0f
    };

    constexpr float ys[SPLINE_N] = {
        -1.0f, -0.55f, 0.4f,
        0.8f, 0.9f, 0.99f, 1.0f
    };

    if (t <= xs[0])
        return ys[0];

    if (t >= xs[SPLINE_N - 1])
        return ys[SPLINE_N - 1];

    for (int i = 0; i < SPLINE_N - 1; ++i)
    {
        if (t <= xs[i + 1])
        {
            float u =
                (t - xs[i]) /
                (xs[i + 1] - xs[i]);

            return ys[i] +
                   u * (ys[i + 1] - ys[i]);
        }
    }

    return ys[SPLINE_N - 1];
}