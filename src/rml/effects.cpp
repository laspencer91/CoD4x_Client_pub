// Adapted from ProlineUnreal 396a541, ProlineRmlSoftFade.cpp.
#include "effects.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Decorator.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/RenderManager.h>

namespace cod4x::rml {
namespace
{
// Rectangular, smooth edge fades, equivalent to the authored UI line material's four fades.
// Cached vertex colors use the existing renderer; no intermediate texture or per-frame rebuild.
class SoftFade final : public Rml::Decorator
{
public:
    Rml::Colourb Color;
    float Top = 0, Right = 0, Bottom = 0, Left = 0;

    static float Fade(float Position, float Length)
    {
        if (Length <= 0) return 1;
        const float T = std::clamp(Position / Length, 0.f, 1.f);
        return T * T * (3.f - 2.f * T);
    }

    static std::vector<float> Coordinates(float Start, float End)
    {
        std::vector<float> Result{0, 1};
        // Place subdivisions inside the fade, including very narrow edge fades.
        for (int I = 1; I <= 16; ++I)
        {
            if (Start > 0) Result.push_back(Start * I / 16.f);
            if (End > 0) Result.push_back(1.f - End * I / 16.f);
        }
        std::sort(Result.begin(), Result.end());
        for (int I = int(Result.size()) - 1; I > 0; --I)
            if (std::abs(Result[I] - Result[I - 1]) < 0.00001f) Result.erase(Result.begin() + I);
        return Result;
    }

    Rml::DecoratorDataHandle GenerateElementData(Rml::Element* Element, Rml::BoxArea Area) const override
    {
        const auto Box = Element->GetRenderBox(Area);
        const auto Size = Box.GetFillSize();
        const auto Offset = Box.GetFillOffset();
        const auto Xs = Coordinates(Left, Right);
        const auto Ys = Coordinates(Top, Bottom);
        Rml::Mesh Mesh;
        for (float Y : Ys)
            for (float X : Xs)
            {
                const float Alpha = Element->GetComputedValues().opacity() * Fade(X, Left) * Fade(1 - X, Right) * Fade(Y, Top) * Fade(1 - Y, Bottom);
                Mesh.vertices.push_back({Offset + Rml::Vector2f(X * Size.x, Y * Size.y), Color.ToPremultiplied(Alpha), {0, 0}});
            }
        for (int Y = 0; Y < int(Ys.size()) - 1; ++Y)
            for (int X = 0; X < int(Xs.size()) - 1; ++X)
            {
                const int A = Y * int(Xs.size()) + X, B = A + 1, C = A + int(Xs.size()), D = C + 1;
                for (int Index : {A, B, C, B, D, C}) Mesh.indices.push_back(Index);
            }
        return reinterpret_cast<Rml::DecoratorDataHandle>(new Rml::Geometry(Element->GetRenderManager()->MakeGeometry(std::move(Mesh))));
    }

    void ReleaseElementData(Rml::DecoratorDataHandle Data) const override { delete reinterpret_cast<Rml::Geometry*>(Data); }
    void RenderElement(Rml::Element* Element, Rml::DecoratorDataHandle Data) const override
    {
        reinterpret_cast<Rml::Geometry*>(Data)->Render(Element->GetAbsoluteOffset(Rml::BoxArea::Border));
    }
};

class SoftFadeInstancer final : public Rml::DecoratorInstancer
{
    Rml::PropertyId Color, Top, Right, Bottom, Left;
public:
    SoftFadeInstancer()
    {
        Color = RegisterProperty("color", "#000").AddParser("color").GetId();
        Top = RegisterProperty("top", "0").AddParser("number").GetId();
        Right = RegisterProperty("right", "0").AddParser("number").GetId();
        Bottom = RegisterProperty("bottom", "0").AddParser("number").GetId();
        Left = RegisterProperty("left", "0").AddParser("number").GetId();
        RegisterShorthand("decorator", "color, top, right, bottom, left", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::Decorator> InstanceDecorator(const Rml::String&, const Rml::PropertyDictionary& Properties, const Rml::DecoratorInstancerInterface&) override
    {
        auto Result = Rml::MakeShared<SoftFade>();
        Result->Color = Properties.GetProperty(Color)->Get<Rml::Colourb>();
        auto Length = [&](Rml::PropertyId Id) { return std::clamp(Properties.GetProperty(Id)->Get<float>(), 0.f, 1.f); };
        Result->Top = Length(Top); Result->Right = Length(Right); Result->Bottom = Length(Bottom); Result->Left = Length(Left);
        return Result;
    }
};
}

void RegisterEffects()
{
    static SoftFadeInstancer Instancer;
    Rml::Factory::RegisterDecoratorInstancer("soft-fade", &Instancer);
}
}
