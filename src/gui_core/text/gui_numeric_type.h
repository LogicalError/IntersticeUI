#pragma once

#include <cstdint>

namespace interstice
{
	// ------------------------------------------------------------------------
	// Numeric Value Union/Type - For editable numeric text fields
	// ------------------------------------------------------------------------

    enum class NumericType
    {
        None,       ///< not a numeric type

        TypeUnsignedBit = 16,
        TypeSignedBit   = 32,
        TypeFloatBit    = 64,

        TypeUInt8  = TypeUnsignedBit | 1,
        TypeUInt16 = TypeUnsignedBit | 2,
        TypeUInt32 = TypeUnsignedBit | 3,
        TypeUInt64 = TypeUnsignedBit | 4,
        
        TypeInt8   = TypeSignedBit | 1,
        TypeInt16  = TypeSignedBit | 2,
        TypeInt32  = TypeSignedBit | 3,
        TypeInt64  = TypeSignedBit | 4,

        TypeFloat  = TypeFloatBit | 1,
        TypeDouble = TypeFloatBit | 2
    };

    union NumericValue
    {
        uint8_t  value_uint8;
        uint16_t value_uint16;
        uint32_t value_uint32;
        uint64_t value_uint64;
        
        int8_t  value_int8;
        int16_t value_int16;
        int32_t value_int32;
        int64_t value_int64;

        float   value_float;
        double  value_double;
    };
    
    template<typename T> constexpr NumericType GetNumericType(const T&     /*value*/) { return NumericType::None; }
    template<> constexpr NumericType GetNumericType(const int8_t&   /*value*/) { return NumericType::TypeInt8; }
    template<> constexpr NumericType GetNumericType(const int16_t&  /*value*/) { return NumericType::TypeInt16; }
    template<> constexpr NumericType GetNumericType(const int32_t&  /*value*/) { return NumericType::TypeInt32; }
    template<> constexpr NumericType GetNumericType(const int64_t&  /*value*/) { return NumericType::TypeInt64; }
    template<> constexpr NumericType GetNumericType(const uint8_t&  /*value*/) { return NumericType::TypeUInt8; }
    template<> constexpr NumericType GetNumericType(const uint16_t& /*value*/) { return NumericType::TypeUInt16; }
    template<> constexpr NumericType GetNumericType(const uint32_t& /*value*/) { return NumericType::TypeUInt32; }
    template<> constexpr NumericType GetNumericType(const uint64_t& /*value*/) { return NumericType::TypeUInt64; }
    template<> constexpr NumericType GetNumericType(const float&    /*value*/) { return NumericType::TypeFloat; }
    template<> constexpr NumericType GetNumericType(const double&   /*value*/) { return NumericType::TypeDouble; }
    
    template<typename T> NumericType CopyNumericTypeTo(const T& /*src*/, NumericValue& dst) { dst = {}; return NumericType::None; }
    template<> inline NumericType CopyNumericTypeTo(const int8_t&   src, NumericValue& dst) { dst.value_int8   = src; return NumericType::TypeInt8; }
    template<> inline NumericType CopyNumericTypeTo(const int16_t&  src, NumericValue& dst) { dst.value_int16  = src; return NumericType::TypeInt16; }
    template<> inline NumericType CopyNumericTypeTo(const int32_t&  src, NumericValue& dst) { dst.value_int32  = src; return NumericType::TypeInt32; }
    template<> inline NumericType CopyNumericTypeTo(const int64_t&  src, NumericValue& dst) { dst.value_int64  = src; return NumericType::TypeInt64; }
    template<> inline NumericType CopyNumericTypeTo(const uint8_t&  src, NumericValue& dst) { dst.value_uint8  = src; return NumericType::TypeUInt8; }
    template<> inline NumericType CopyNumericTypeTo(const uint16_t& src, NumericValue& dst) { dst.value_uint16 = src; return NumericType::TypeUInt16; }
    template<> inline NumericType CopyNumericTypeTo(const uint32_t& src, NumericValue& dst) { dst.value_uint32 = src; return NumericType::TypeUInt32; }
    template<> inline NumericType CopyNumericTypeTo(const uint64_t& src, NumericValue& dst) { dst.value_uint64 = src; return NumericType::TypeUInt64; }
    template<> inline NumericType CopyNumericTypeTo(const float&    src, NumericValue& dst) { dst.value_float  = src; return NumericType::TypeFloat; }
    template<> inline NumericType CopyNumericTypeTo(const double&   src, NumericValue& dst) { dst.value_double = src; return NumericType::TypeDouble; }
        
    template<typename T> bool CopyNumericTypeTo(NumericType /*type*/, const NumericValue& /*src*/, T& /*dst*/) { return false; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int8_t&   dst) { if (type != NumericType::TypeInt8  ) return false; dst = src.value_int8  ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int16_t&  dst) { if (type != NumericType::TypeInt16 ) return false; dst = src.value_int16 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int32_t&  dst) { if (type != NumericType::TypeInt32 ) return false; dst = src.value_int32 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, int64_t&  dst) { if (type != NumericType::TypeInt64 ) return false; dst = src.value_int64 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint8_t&  dst) { if (type != NumericType::TypeUInt8 ) return false; dst = src.value_uint8 ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint16_t& dst) { if (type != NumericType::TypeUInt16) return false; dst = src.value_uint16; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint32_t& dst) { if (type != NumericType::TypeUInt32) return false; dst = src.value_uint32; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, uint64_t& dst) { if (type != NumericType::TypeUInt64) return false; dst = src.value_uint64; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, float&    dst) { if (type != NumericType::TypeFloat ) return false; dst = src.value_float ; return true; }
    template<> inline bool CopyNumericTypeTo(NumericType type, const NumericValue& src, double&   dst) { if (type != NumericType::TypeDouble) return false; dst = src.value_double; return true; }
    
    template<typename T> bool EqualsNumericType(NumericType /*type*/, NumericValue& /*left*/, const T& /*right*/) { return false; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int8_t&   right) { return type == NumericType::TypeInt8   && left.value_int8   == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int16_t&  right) { return type == NumericType::TypeInt16  && left.value_int16  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int32_t&  right) { return type == NumericType::TypeInt32  && left.value_int32  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const int64_t&  right) { return type == NumericType::TypeInt64  && left.value_int64  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint8_t&  right) { return type == NumericType::TypeUInt8  && left.value_uint8  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint16_t& right) { return type == NumericType::TypeUInt16 && left.value_uint16 == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint32_t& right) { return type == NumericType::TypeUInt32 && left.value_uint32 == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const uint64_t& right) { return type == NumericType::TypeUInt64 && left.value_uint64 == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const float&    right) { return type == NumericType::TypeFloat  && left.value_float  == right; }
    template<> inline bool EqualsNumericType(NumericType type, NumericValue& left, const double&   right) { return type == NumericType::TypeDouble && left.value_double == right; }

    inline bool EqualsNumericType(NumericType leftType, NumericValue& left, NumericType rightType, const NumericValue& right)
    { 
        if (leftType != rightType) return false;
        switch (leftType)
        {
		    case NumericType::TypeFloat:  return left.value_float == right.value_float;
			case NumericType::TypeDouble: return left.value_double == right.value_double;

			case NumericType::TypeUInt64: return left.value_uint64 == right.value_uint64;
            case NumericType::TypeUInt32: return left.value_uint32 == right.value_uint32;
            case NumericType::TypeUInt16: return left.value_uint16 == right.value_uint16;
            case NumericType::TypeUInt8:  return left.value_uint8  == right.value_uint8;

			case NumericType::TypeInt64: return left.value_int64 == right.value_int64;
            case NumericType::TypeInt32: return left.value_int32 == right.value_int32;
            case NumericType::TypeInt16: return left.value_int16 == right.value_int16;
            case NumericType::TypeInt8:  return left.value_int8  == right.value_int8;

            default: return false;
        }
    }
}
