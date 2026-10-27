#include <gui_core/gui_base_types.h>
#include <gui_core/style/gui_style.h>
#include <gui_core/style/gui_gradient_manager.h>

namespace tachyon
{
    namespace
    {
        // Write the fields derived from a GradientHandle (if any) into the VCS.
        // Returns the logical page (0 for invalid handles).
        inline uint16_t ApplyHandle(GradientManager& gm, GradientHandle handle,
                                    ushort2& colorUV)
        {
            if (!handle.IsValid())
                return 0;
            AtlasLocation loc = gm.GetLocation(handle);
            colorUV = ushort2{ loc.col, loc.row };
            return uint16_t(loc.page);
        }
    }

    VisualColorStyle ColorStyle::ToVisualStyle(GradientManager& gradientManager,
                                               ImageIndex textureIndex,
                                               GradientHandle& outHandle) const
    {
        auto localTransformationX = this->transformationX;
        auto localTransformationY = this->transformationY;
        ushort2 colorUV = { 0, 0 };
        uint16_t gradientPage = 0;
        outHandle = GradientHandle::Invalid();

        switch (type)
        {
            case ColorType::SolidColor:
            {
                outHandle = gradientManager.AcquireSolidColor(gradient.stops[0].GetColor4b());
                gradientPage = ApplyHandle(gradientManager, outHandle, colorUV);
                break;
            }
            case ColorType::StretchedTexture:
            case ColorType::RepeatedTexture:
            {
                // Texture types don't use the gradient atlas.
                break;
            }
            case ColorType::LinearGradient:
            case ColorType::CircularAngularGradient:
            case ColorType::EllipsoidAngularGradient:
            case ColorType::RadialGradient:
            {
                outHandle = gradientManager.AcquireGradient(gradient);
                gradientPage = ApplyHandle(gradientManager, outHandle, colorUV);

                float angle = gradient.GetValue() * 2.0f * 3.14159265f;
                float2x2 valueRotation = float2x2(float2(cosf(angle), sinf(angle)),
                                                  float2(-sinf(angle), cosf(angle)));
                float3x2 f_transform = GetTransformation();
                float2x2 transform = float2x2(float2(f_transform.r0.x, f_transform.r0.y),
                                              float2(f_transform.r1.x, f_transform.r1.y));
                float2x2 result = transform * valueRotation;
                localTransformationX = short4(FloatToHalf(result[0][0]), FloatToHalf(result[0][1]), localTransformationX.z, localTransformationX.w);
                localTransformationY = short4(FloatToHalf(result[1][0]), FloatToHalf(result[1][1]), localTransformationY.z, localTransformationY.w);
                break;
            }
            case ColorType::InterpolatedTriangle:
            {
                outHandle = gradientManager.AcquireColors(
                    gradient.stops[0].GetColor4b(),
                    gradient.stops[1].GetColor4b(),
                    gradient.stops[2].GetColor4b());
                gradientPage = ApplyHandle(gradientManager, outHandle, colorUV);
                break;
            }
            case ColorType::InterpolatedBox:
            {
                outHandle = gradientManager.AcquireColors(
                    gradient.stops[0].GetColor4b(),
                    gradient.stops[1].GetColor4b(),
                    gradient.stops[2].GetColor4b(),
                    gradient.stops[3].GetColor4b());
                gradientPage = ApplyHandle(gradientManager, outHandle, colorUV);
                break;
            }
            case ColorType::SDFGradient:
            {
                outHandle = gradientManager.AcquireGradient(gradient);
                gradientPage = ApplyHandle(gradientManager, outHandle, colorUV);
                break;
            }
        }

        return VisualColorStyle
        {
            .transformationX = localTransformationX,
            .transformationY = localTransformationY,
            .colorUV         = colorUV,
            .type            = type,
            .gradientPage    = gradientPage,
            .textureIndex    = textureIndex,
            .animSpeed       = animSpeed
        };
    }


    void VisualColorStyle::SetTransformation(float3 x, float3 y)
    {
        transformationX = short4(FloatToHalf(x.x), FloatToHalf(x.y), FloatToHalf(x.z), 0);
        transformationY = short4(FloatToHalf(y.x), FloatToHalf(y.y), FloatToHalf(y.z), 0);
    }

    void VisualColorStyle::SetAnimSpeed(float x, float y)
    {
        animSpeed = short2(FloatToHalf(x), FloatToHalf(y));
    }

    void ColorStyle::SetTransformation(float3 x, float3 y)
    {
        transformationX = short4(FloatToHalf(x.x), FloatToHalf(x.y), FloatToHalf(x.z), 0);
        transformationY = short4(FloatToHalf(y.x), FloatToHalf(y.y), FloatToHalf(y.z), 0);
    }

    void ColorStyle::SetAnimSpeed(float x, float y)
    {
        animSpeed = short2(FloatToHalf(x), FloatToHalf(y));
    }

    float3x2 ColorStyle::GetTransformation() const
    {
        return float3x2
        {
            float3{ HalfToFloat(transformationX.x ), HalfToFloat(transformationX.y ), HalfToFloat(transformationX.z ) },
            float3{ HalfToFloat(transformationY.x ), HalfToFloat(transformationY.y ), HalfToFloat(transformationY.z ) }
        };
    }

    // ------------------------------------------------------------------
    // VisualColorStyle::Create factories - each acquires a fresh handle
    // and returns it through outHandle. Owner is responsible for Release.
    // ------------------------------------------------------------------

    static inline VisualColorStyle BuildFromHandle(GradientManager& gm,
                                                   GradientHandle handle,
                                                   ColorType type)
    {
        VisualColorStyle style;
        if (handle.IsValid())
        {
            AtlasLocation loc = gm.GetLocation(handle);
            style.colorUV = ushort2{ loc.col, loc.row };
            style.gradientPage = uint16_t(loc.page);
        }
        style.type = type;
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4b color, GradientHandle& outHandle)
    {
        outHandle = gm.AcquireSolidColor(color);
        return BuildFromHandle(gm, outHandle, ColorType::SolidColor);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4f color, GradientHandle& outHandle)
    {
        outHandle = gm.AcquireSolidColor(color);
        return BuildFromHandle(gm, outHandle, ColorType::SolidColor);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4b c1, color4b c2, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox && type != ColorType::InterpolatedTriangle);
        outHandle = gm.AcquireColors(c1, c2);
        return BuildFromHandle(gm, outHandle, type);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4f c1, color4f c2, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox && type != ColorType::InterpolatedTriangle);
        outHandle = gm.AcquireColors(c1, c2);
        return BuildFromHandle(gm, outHandle, type);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4b c1, color4b c2, color4b c3, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox);
        outHandle = gm.AcquireColors(c1, c2, c3);
        return BuildFromHandle(gm, outHandle, type);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4f c1, color4f c2, color4f c3, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox);
        outHandle = gm.AcquireColors(c1, c2, c3);
        return BuildFromHandle(gm, outHandle, type);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4b c1, color4b c2, color4b c3, color4b c4, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedTriangle);
        outHandle = gm.AcquireColors(c1, c2, c3, c4);
        return BuildFromHandle(gm, outHandle, type);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, color4f c1, color4f c2, color4f c3, color4f c4, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedTriangle);
        outHandle = gm.AcquireColors(c1, c2, c3, c4);
        return BuildFromHandle(gm, outHandle, type);
    }

    VisualColorStyle VisualColorStyle::Create(GradientManager& gm, Gradient gradient, ColorType type, GradientHandle& outHandle)
    {
        assert(type != ColorType::SolidColor);
        outHandle = gm.AcquireGradient(gradient);
        return BuildFromHandle(gm, outHandle, type);
    }


    ColorStyle ColorStyle::Create(color4b color)
    {
        ColorStyle style;
        style.type = ColorType::SolidColor;
        style.gradient = Gradient::Solid(color);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4f color)
    {
        ColorStyle style;
        style.type = ColorType::SolidColor;
        style.gradient = Gradient::Solid(color);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4b c0, color4b c1, ColorType type)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox && type != ColorType::InterpolatedTriangle);
        ColorStyle style;
        style.type = type;
        style.gradient = Gradient::Linear(c0, c1);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4f c0, color4f c1, ColorType type)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox && type != ColorType::InterpolatedTriangle);
        ColorStyle style;
        style.type = type;
        style.gradient = Gradient::Linear(c0, c1);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4f c0, color4f c1, color4f c2, ColorType type)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox);
        ColorStyle style;
        style.type = type;
        style.gradient = Gradient::Linear(c0, c1, c2);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4b c0, color4b c1, color4b c2, ColorType type)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedBox);
        ColorStyle style;
        style.type = type;
        style.gradient = Gradient::Linear(c0, c1, c2);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4b c0, color4b c1, color4b c2, color4b c3, ColorType type)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedTriangle);
        ColorStyle style;
        style.type = type;
        style.gradient = Gradient::Linear(c0, c1, c2, c3);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(color4f c0, color4f c1, color4f c2, color4f c3, ColorType type)
    {
        assert(type != ColorType::SolidColor && type != ColorType::InterpolatedTriangle);
        ColorStyle style;
        style.type = type;
        style.gradient = Gradient::Linear(c0, c1, c2, c3);
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }

    ColorStyle ColorStyle::Create(Gradient gradient, ColorType type)
    {
        ColorStyle style;
        style.type = type;
        style.gradient = gradient;
        style.SetTransformation(float3(1, 0, 0), float3(0, 1, 0));
        return style;
    }
}
