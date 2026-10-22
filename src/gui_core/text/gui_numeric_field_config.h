#pragma once

#include <algorithm>
#include <cmath>
#include <stdint.h>
#include <type_traits>

namespace GUI
{
    template<typename T>
    struct NumericFieldConfig {};
    
    template<>
    struct NumericFieldConfig<double>
    {
        double  stepPerPixel = 0.01; // value change per pixel dragged
        double  minVal       = -DBL_MAX;
        double  maxVal       = DBL_MAX;

        double Calculate(int32_t offset, double speedScale, double startValue) const
        {
            double result = startValue + offset * speedScale * stepPerPixel;
            if (std::isnan(result)) return 0.0;
            return std::clamp(result, minVal, maxVal);
        }
	};

    template<>
    struct NumericFieldConfig<float>
    {
        double  stepPerPixel = 0.01; // value change per pixel dragged
        float   minVal       = -FLT_MAX;
        float   maxVal       = FLT_MAX;

        float Calculate(int32_t offset, double speedScale, float startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            if (std::isnan(result)) return 0.0f;
            return float(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<uint64_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        uint64_t minVal = 0;
        uint64_t maxVal = UINT64_MAX;
        uint64_t Calculate(int32_t offset, double speedScale, uint64_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint64_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<uint32_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        uint32_t minVal = 0;
        uint32_t maxVal = UINT32_MAX;
        uint32_t Calculate(int32_t offset, double speedScale, uint32_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint32_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<uint16_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        uint16_t minVal = 0;
        uint16_t maxVal = UINT16_MAX;
        uint16_t Calculate(int32_t offset, double speedScale, uint16_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint16_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<uint8_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        uint8_t minVal = 0;
        uint8_t maxVal = UINT8_MAX;
        uint8_t Calculate(int32_t offset, double speedScale, uint8_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint8_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<int64_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        int64_t minVal = INT64_MIN;
        int64_t maxVal = INT64_MAX;
        int64_t Calculate(int32_t offset, double speedScale, int64_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int64_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<int32_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        int32_t minVal = INT32_MIN;
        int32_t maxVal = INT32_MAX;
        int32_t Calculate(int32_t offset, double speedScale, int32_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int32_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<int16_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        int16_t minVal = INT16_MIN;
        int16_t maxVal = INT16_MAX;
        int16_t Calculate(int32_t offset, double speedScale, int16_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int16_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    template<>
    struct NumericFieldConfig<int8_t>
    {
        double  stepPerPixel = 1.00; // value change per pixel dragged
        int8_t minVal = INT8_MIN;
        int8_t maxVal = INT8_MAX;
        int8_t Calculate(int32_t offset, double speedScale, int8_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int8_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };
}
