#pragma once

#include <gui_core/gui_base_types.h>

namespace GUI
{
    template<typename T>
    struct TypeDimensions
    {
        using Type = T;
        using ComponentType = T;
        static constexpr int dimensions = 1;
        static T& GetComponent(T& value, int index) { return value; }
    };

    template<> struct TypeDimensions<int2>    { using Type = int2; using ComponentType = decltype(Type::x); static constexpr int dimensions = 2; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y" }; };
    template<> struct TypeDimensions<int3>    { using Type = int3; using ComponentType = decltype(Type::x); static constexpr int dimensions = 3; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z" }; };
    template<> struct TypeDimensions<int4>    { using Type = int4; using ComponentType = decltype(Type::x); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z", "W"}; };

    template<> struct TypeDimensions<uint2>   { using Type = uint2; using ComponentType = decltype(Type::x); static constexpr int dimensions = 2; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y"}; };
    template<> struct TypeDimensions<uint3>   { using Type = uint3; using ComponentType = decltype(Type::x); static constexpr int dimensions = 3; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z"}; };
    template<> struct TypeDimensions<uint4>   { using Type = uint4; using ComponentType = decltype(Type::x); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z", "W"}; };

    template<> struct TypeDimensions<short2>  { using Type = short2; using ComponentType = decltype(Type::x); static constexpr int dimensions = 2; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y"}; };
    template<> struct TypeDimensions<short3>  { using Type = short3; using ComponentType = decltype(Type::x); static constexpr int dimensions = 3; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z"}; };
    template<> struct TypeDimensions<short4>  { using Type = short4; using ComponentType = decltype(Type::x); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z", "W"}; };

    template<> struct TypeDimensions<ushort2> { using Type = ushort2; using ComponentType = decltype(Type::x); static constexpr int dimensions = 2; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const* const names[dimensions] = { "X", "Y" }; };
    template<> struct TypeDimensions<ushort3> { using Type = ushort3; using ComponentType = decltype(Type::x); static constexpr int dimensions = 3; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const* const names[dimensions] = { "X", "Y", "Z" }; };
    template<> struct TypeDimensions<ushort4> { using Type = ushort4; using ComponentType = decltype(Type::x); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z", "W"}; };

    template<> struct TypeDimensions<byte2>   { using Type = byte2; using ComponentType = decltype(Type::x); static constexpr int dimensions = 2; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y"}; };
    template<> struct TypeDimensions<byte3>   { using Type = byte3; using ComponentType = decltype(Type::x); static constexpr int dimensions = 3; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z"}; };
    template<> struct TypeDimensions<byte4>   { using Type = byte4; using ComponentType = decltype(Type::x); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z", "W"}; };
                                                                
    template<> struct TypeDimensions<float2>  { using Type = float2; using ComponentType = decltype(Type::x); static constexpr int dimensions = 2; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y"}; };
    template<> struct TypeDimensions<float3>  { using Type = float3; using ComponentType = decltype(Type::x); static constexpr int dimensions = 3; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const*const names[dimensions] = {"X", "Y", "Z"}; };
    template<> struct TypeDimensions<float4>  { using Type = float4; using ComponentType = decltype(Type::x); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const* const names[dimensions] = { "X", "Y", "Z", "W"}; };

    template<> struct TypeDimensions<color4b> { using Type = color4b; using ComponentType = decltype(Type::r); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const* const names[dimensions] = { "R", "G", "B", "A" }; };
    template<> struct TypeDimensions<color4f> { using Type = color4f; using ComponentType = decltype(Type::r); static constexpr int dimensions = 4; inline static ComponentType& GetComponent(Type& value, int index) { return value[index]; } static constexpr char const* const names[dimensions] = { "R", "G", "B", "A" }; };
}
