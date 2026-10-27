#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <stdint.h>
#include <type_traits>

namespace tachyon
{
    /// @addtogroup text
    /// @{

    /// Drag speed and range of a numeric Field, specialized per value type. Typed and
    /// dragged values are clamped to [minVal, maxVal].
    ///
    ///     Field(opacity, NumericFieldConfig<float>{ .minVal = 0.0f, .maxVal = 1.0f });
    template<typename T>
    struct NumericFieldConfig {};
    
    /// NumericFieldConfig for double values.
    template<>
    struct NumericFieldConfig<double>
    {
        double  stepPerPixel = 0.01; ///< value change per pixel dragged
        double  minVal       = -DBL_MAX; ///< lowest allowed value
        double  maxVal       = DBL_MAX; ///< highest allowed value

        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        double Calculate(int32_t offset, double speedScale, double startValue) const
        {
            double result = startValue + offset * speedScale * stepPerPixel;
            if (std::isnan(result)) return 0.0;
            return std::clamp(result, minVal, maxVal);
        }
	};

    /// NumericFieldConfig for float values.
    template<>
    struct NumericFieldConfig<float>
    {
        double  stepPerPixel = 0.01; ///< value change per pixel dragged
        float   minVal       = -FLT_MAX; ///< lowest allowed value
        float   maxVal       = FLT_MAX; ///< highest allowed value

        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        float Calculate(int32_t offset, double speedScale, float startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            if (std::isnan(result)) return 0.0f;
            return float(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for uint64_t values.
    template<>
    struct NumericFieldConfig<uint64_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint64_t minVal = 0; ///< lowest allowed value
        uint64_t maxVal = UINT64_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint64_t Calculate(int32_t offset, double speedScale, uint64_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint64_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for uint32_t values.
    template<>
    struct NumericFieldConfig<uint32_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint32_t minVal = 0; ///< lowest allowed value
        uint32_t maxVal = UINT32_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint32_t Calculate(int32_t offset, double speedScale, uint32_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint32_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for uint16_t values.
    template<>
    struct NumericFieldConfig<uint16_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint16_t minVal = 0; ///< lowest allowed value
        uint16_t maxVal = UINT16_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint16_t Calculate(int32_t offset, double speedScale, uint16_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint16_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for uint8_t values.
    template<>
    struct NumericFieldConfig<uint8_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        uint8_t minVal = 0; ///< lowest allowed value
        uint8_t maxVal = UINT8_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        uint8_t Calculate(int32_t offset, double speedScale, uint8_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return uint8_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for int64_t values.
    template<>
    struct NumericFieldConfig<int64_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int64_t minVal = INT64_MIN; ///< lowest allowed value
        int64_t maxVal = INT64_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int64_t Calculate(int32_t offset, double speedScale, int64_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int64_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for int32_t values.
    template<>
    struct NumericFieldConfig<int32_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int32_t minVal = INT32_MIN; ///< lowest allowed value
        int32_t maxVal = INT32_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int32_t Calculate(int32_t offset, double speedScale, int32_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int32_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for int16_t values.
    template<>
    struct NumericFieldConfig<int16_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int16_t minVal = INT16_MIN; ///< lowest allowed value
        int16_t maxVal = INT16_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int16_t Calculate(int32_t offset, double speedScale, int16_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int16_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };

    /// NumericFieldConfig for int8_t values.
    template<>
    struct NumericFieldConfig<int8_t>
    {
        double  stepPerPixel = 1.00; ///< value change per pixel dragged
        int8_t minVal = INT8_MIN; ///< lowest allowed value
        int8_t maxVal = INT8_MAX; ///< highest allowed value
        /// The value after dragging `offset` pixels from startValue; speedScale is the modifier
        /// factor (Ctrl 0.1, Shift 10). The result is clamped to the range.
        int8_t Calculate(int32_t offset, double speedScale, int8_t startValue) const
        {
            double result = double(startValue) + offset * speedScale * stepPerPixel;
            return int8_t(std::clamp(result, double(minVal), double(maxVal)));
        }
    };
    /// @}
}
