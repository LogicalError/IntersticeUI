#include "nanosvg_bridge.h"

#define NANOSVG_IMPLEMENTATION
#pragma warning(push)
#pragma warning( disable: 4702)
#include <nanosvg.h>
#pragma warning(pop)

#include <skribidi/skb_common.h>
#include <skribidi/skb_icon_collection.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace interstice
{
namespace
{

// NanoSVG packs colors as 0xAABBGGRR (low byte = R, high byte = A).
inline skb_color_t nsvgColorToSkb(unsigned int c, float extraOpacity)
{
	const uint8_t r = (uint8_t)(c & 0xFF);
	const uint8_t g = (uint8_t)((c >> 8) & 0xFF);
	const uint8_t b = (uint8_t)((c >> 16) & 0xFF);
	uint8_t a = (uint8_t)((c >> 24) & 0xFF);
	if (extraOpacity < 1.0f)
		a = (uint8_t)std::clamp(a * extraOpacity + 0.5f, 0.f, 255.f);
	return skb_rgba(r, g, b, a);
}

// 2x3 affine inverse for NanoSVG's xform[6] layout.
// Matches nsvg__xformInverse from nanosvg.h (which is static).
bool nsvgXformInverse(float* out, const float* in)
{
	const double det = (double)in[0] * in[3] - (double)in[2] * in[1];
	if (det > -1e-6 && det < 1e-6) {
		out[0] = 1; out[1] = 0; out[2] = 0; out[3] = 1; out[4] = 0; out[5] = 0;
		return false;
	}
	const double invdet = 1.0 / det;
	out[0] = (float)( in[3] * invdet);
	out[2] = (float)(-in[2] * invdet);
	out[4] = (float)(((double)in[2] * in[5] - (double)in[3] * in[4]) * invdet);
	out[1] = (float)(-in[1] * invdet);
	out[3] = (float)( in[0] * invdet);
	out[5] = (float)(((double)in[1] * in[4] - (double)in[0] * in[5]) * invdet);
	return true;
}

// NanoSVG xform[6] layout: [xx, yx, xy, yy, dx, dy] — same field order as
// skb_mat2_t, so this is a direct field copy.
inline skb_mat2_t nsvgXformToSkb(const float* x)
{
	skb_mat2_t m;
	m.xx = x[0]; m.yx = x[1];
	m.xy = x[2]; m.yy = x[3];
	m.dx = x[4]; m.dy = x[5];
	return m;
}

skb_gradient_spread_t nsvgSpreadToSkb(char spread)
{
	switch (spread) {
		case NSVG_SPREAD_PAD:     return SKB_SPREAD_PAD;
		case NSVG_SPREAD_REFLECT: return SKB_SPREAD_REFLECT;
		case NSVG_SPREAD_REPEAT:  return SKB_SPREAD_REPEAT;
	}
	return SKB_SPREAD_PAD;
}

void buildStops(const NSVGgradient* g, float opacity, std::vector<skb_color_stop_t>& out)
{
	out.clear();
	out.reserve(g->nstops);
	for (int i = 0; i < g->nstops; ++i) {
		skb_color_stop_t s;
		s.offset = g->stops[i].offset;
		s.color  = nsvgColorToSkb(g->stops[i].color, opacity);
		out.push_back(s);
	}
}

void applyPaint(skb_icon_builder_t& b, const NSVGpaint& paint, float opacity)
{
	if (paint.type == NSVG_PAINT_COLOR) {
		skb_icon_builder_fill_color(&b, nsvgColorToSkb(paint.color, opacity));
		return;
	}
	if (paint.type == NSVG_PAINT_LINEAR_GRADIENT ||
	    paint.type == NSVG_PAINT_RADIAL_GRADIENT)
	{
		const NSVGgradient* g = paint.gradient;
		// NanoSVG stores the inverse xform on the gradient (user-space →
		// gradient-space). Skribidi pushes the xform onto its transform
		// stack to map gradient-space → user-space, so invert back.
		float fwd[6];
		nsvgXformInverse(fwd, g->xform);
		const skb_mat2_t skbXform = nsvgXformToSkb(fwd);

		std::vector<skb_color_stop_t> stops;
		buildStops(g, opacity, stops);
		if (stops.empty()) return;

		if (paint.type == NSVG_PAINT_LINEAR_GRADIENT) {
			// NanoSVG normalizes the linear gradient to run from (0,0) to
			// (0,1) in gradient-space; xform brings it to user-space.
			skb_icon_builder_fill_linear_gradient(
				&b,
				skb_vec2_t{ 0.f, 0.f },
				skb_vec2_t{ 0.f, 1.f },
				skbXform,
				nsvgSpreadToSkb(g->spread),
				stops.data(),
				(int32_t)stops.size());
		} else {
			// NanoSVG normalizes the radial gradient to a unit circle at
			// (0,0) with focal at (fx,fy). Skribidi's rasterizer treats
			// p0 as the focal (radius 0) and p1 as the center (with
			// `radius` as the outer radius).
			skb_icon_builder_fill_radial_gradient(
				&b,
				skb_vec2_t{ g->fx, g->fy },
				skb_vec2_t{ 0.f, 0.f },
				1.0f,
				skbXform,
				nsvgSpreadToSkb(g->spread),
				stops.data(),
				(int32_t)stops.size());
		}
		return;
	}
}

void emitFillPaths(skb_icon_builder_t& b, const NSVGshape* s)
{
	for (NSVGpath* p = s->paths; p; p = p->next) {
		if (p->npts < 1) continue;
		skb_icon_builder_move_to(&b, skb_vec2_t{ p->pts[0], p->pts[1] });
		// NanoSVG path: pts[] = x0,y0, [cp1x,cp1y,cp2x,cp2y,x1,y1] repeated.
		for (int i = 0; i + 3 < p->npts; i += 3) {
			const float* c = &p->pts[(i + 1) * 2];
			skb_icon_builder_cubic_to(
				&b,
				skb_vec2_t{ c[0], c[1] },
				skb_vec2_t{ c[2], c[3] },
				skb_vec2_t{ c[4], c[5] });
		}
		if (p->closed)
			skb_icon_builder_close_path(&b);
	}
}

// --- Stroke tessellation -------------------------------------------------
//
// Skribidi's icon model has no native strokes — only filled paths. We
// linearize each NanoSVG cubic-bezier path to a polyline, then offset the
// polyline by ±halfWidth to form a filled outline. SDF rasterization
// smooths the linearization.
//
// Caps: butt only. Joins: miter, clamped at miterLimit (bevel beyond).
// No dashes (#67).

struct Pt { float x, y; };

inline Pt operator+(Pt a, Pt b) { return { a.x + b.x, a.y + b.y }; }
inline Pt operator-(Pt a, Pt b) { return { a.x - b.x, a.y - b.y }; }
inline Pt operator*(Pt a, float s) { return { a.x * s, a.y * s }; }

inline float length(Pt v) { return std::sqrt(v.x * v.x + v.y * v.y); }

inline Pt normalize(Pt v)
{
	const float l = length(v);
	if (l < 1e-6f) return { 0, 0 };
	return { v.x / l, v.y / l };
}

inline Pt perp(Pt v) { return { -v.y, v.x }; }

void flattenCubic(std::vector<Pt>& out, Pt p0, Pt p1, Pt p2, Pt p3, int steps)
{
	// Adaptive step count based on chord length; capped to avoid blowups
	// on degenerate input. Minimum 4 segments per cubic keeps even tiny
	// curves smooth at icon SDF resolution.
	if (steps <= 0) steps = 8;
	for (int i = 1; i <= steps; ++i) {
		const float t = (float)i / (float)steps;
		const float u = 1.f - t;
		const float b0 = u * u * u;
		const float b1 = 3 * u * u * t;
		const float b2 = 3 * u * t * t;
		const float b3 = t * t * t;
		out.push_back({
			p0.x * b0 + p1.x * b1 + p2.x * b2 + p3.x * b3,
			p0.y * b0 + p1.y * b1 + p2.y * b2 + p3.y * b3
		});
	}
}

int chooseSteps(Pt p0, Pt p1, Pt p2, Pt p3)
{
	// Cheap heuristic: sum of control-polygon segments → step count.
	const float chordLen = length(p1 - p0) + length(p2 - p1) + length(p3 - p2);
	int steps = (int)std::ceil(chordLen / 2.0f);
	if (steps < 4) steps = 4;
	if (steps > 32) steps = 32;
	return steps;
}

// Builds a polyline from a NanoSVG path. Skips zero-length segments to
// avoid producing zero-length normals during offset.
void flattenPath(const NSVGpath* p, std::vector<Pt>& out)
{
	out.clear();
	if (p->npts < 1) return;
	out.push_back({ p->pts[0], p->pts[1] });
	for (int i = 0; i + 3 < p->npts; i += 3) {
		const float* c = &p->pts[(i + 1) * 2];
		const Pt p0 = out.back();
		const Pt c0 = { c[0], c[1] };
		const Pt c1 = { c[2], c[3] };
		const Pt p3 = { c[4], c[5] };
		const int steps = chooseSteps(p0, c0, c1, p3);
		flattenCubic(out, p0, c0, c1, p3, steps);
	}
	// Drop consecutive duplicates.
	out.erase(std::unique(out.begin(), out.end(),
		[](const Pt& a, const Pt& b) {
			return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f;
		}), out.end());
}

// Vertex normal at index `i` for an offset of ±halfWidth, given the
// segment normals before and after the vertex. Returns the outward
// bisector scaled so that offset edges intersect cleanly. Falls back to
// bevel (segment-normal directly) when the miter would exceed
// miterLimit*halfWidth — this matches SVG's miter-clip rule.
Pt vertexOffset(Pt nPrev, Pt nNext, float halfWidth, float miterLimit, bool& outBevel)
{
	const Pt sum = nPrev + nNext;
	const float lenSq = sum.x * sum.x + sum.y * sum.y;
	outBevel = false;
	if (lenSq < 1e-8f) {
		// 180° turn — use the previous normal (bevel).
		outBevel = true;
		return nPrev * halfWidth;
	}
	// Length of the bisector vector |nPrev+nNext| equals 2*cos(theta/2) where
	// theta is the exterior angle. The miter length in halfWidths is
	// 1/cos(theta/2) = 2/|nPrev+nNext|.
	const float denom = (nPrev.x * nNext.x + nPrev.y * nNext.y) + 1.f;
	if (denom < 1e-6f) {
		outBevel = true;
		return nPrev * halfWidth;
	}
	const float scale = halfWidth / denom;
	Pt off = { sum.x * scale, sum.y * scale };
	const float miterLen = length(off);
	if (miterLen > halfWidth * miterLimit) {
		outBevel = true;
		return nPrev * halfWidth;
	}
	return off;
}

void emitStrokeRibbon(skb_icon_builder_t& b,
                      const std::vector<Pt>& poly,
                      float halfWidth,
                      float miterLimit,
                      bool closed)
{
	const int n = (int)poly.size();
	if (n < 2) return;

	// Per-segment normals (n-1 segments).
	std::vector<Pt> segNormals(n - 1);
	for (int i = 0; i < n - 1; ++i)
		segNormals[i] = perp(normalize(poly[i + 1] - poly[i]));

	auto vertN = [&](int i) -> Pt {
		// Segment normal at vertex i: bisect the two adjacent segments.
		// At endpoints of an open path, fall back to the single adjacent
		// segment normal (butt cap).
		if (closed) {
			const Pt nPrev = segNormals[(i - 1 + (n - 1)) % (n - 1)];
			const Pt nNext = segNormals[i % (n - 1)];
			bool _b;
			return vertexOffset(nPrev, nNext, halfWidth, miterLimit, _b);
		}
		if (i == 0)         return segNormals[0] * halfWidth;
		if (i == n - 1)     return segNormals[n - 2] * halfWidth;
		bool _b;
		return vertexOffset(segNormals[i - 1], segNormals[i], halfWidth, miterLimit, _b);
	};

	std::vector<Pt> outer(n), inner(n);
	for (int i = 0; i < n; ++i) {
		const Pt off = vertN(i);
		outer[i] = poly[i] + off;
		inner[i] = poly[i] - off;
	}

	if (closed) {
		// Two closed rings with opposite winding so non-zero fill
		// (skb_canvas.c) cancels inside the inner ring and paints only
		// the stroked annulus.
		skb_icon_builder_move_to(&b, skb_vec2_t{ outer[0].x, outer[0].y });
		for (int i = 1; i < n; ++i)
			skb_icon_builder_line_to(&b, skb_vec2_t{ outer[i].x, outer[i].y });
		skb_icon_builder_close_path(&b);

		skb_icon_builder_move_to(&b, skb_vec2_t{ inner[n - 1].x, inner[n - 1].y });
		for (int i = n - 2; i >= 0; --i)
			skb_icon_builder_line_to(&b, skb_vec2_t{ inner[i].x, inner[i].y });
		skb_icon_builder_close_path(&b);
	} else {
		// Open path: single ribbon polygon (outer forward, inner back).
		skb_icon_builder_move_to(&b, skb_vec2_t{ outer[0].x, outer[0].y });
		for (int i = 1; i < n; ++i)
			skb_icon_builder_line_to(&b, skb_vec2_t{ outer[i].x, outer[i].y });
		for (int i = n - 1; i >= 0; --i)
			skb_icon_builder_line_to(&b, skb_vec2_t{ inner[i].x, inner[i].y });
		skb_icon_builder_close_path(&b);
	}
}

void emitStrokePaths(skb_icon_builder_t& b, const NSVGshape* s)
{
	const float halfWidth  = 0.5f * s->strokeWidth;
	const float miterLimit = s->miterLimit > 0 ? s->miterLimit : 4.f;
	std::vector<Pt> poly;
	for (NSVGpath* p = s->paths; p; p = p->next) {
		flattenPath(p, poly);
		if (poly.size() < 2) continue;
		emitStrokeRibbon(b, poly, halfWidth, miterLimit, p->closed != 0);
	}
}

bool isNeutralMonochrome(const NSVGimage* img)
{
	// NanoSVG colors are 0xAABBGGRR; alpha (opacity) doesn't affect the hue.
	constexpr int kMaxSpread = 12; // r/g/b may differ this much and still count as grey
	unsigned rgb = 0;
	bool any = false;
	for (const NSVGshape* s = img->shapes; s; s = s->next)
	{
		if (!(s->flags & NSVG_FLAGS_VISIBLE)) continue;
		for (const NSVGpaint* paint : { &s->fill, &s->stroke })
		{
			if (paint->type == NSVG_PAINT_NONE) continue;
			if (paint->type != NSVG_PAINT_COLOR) return false;
			const unsigned c = paint->color & 0xFFFFFFu;
			if (any && c != rgb) return false;
			rgb = c;
			any = true;
		}
	}
	const int r = int(rgb & 0xFF), g = int((rgb >> 8) & 0xFF), bl = int((rgb >> 16) & 0xFF);
	return any && std::max({ r, g, bl }) - std::min({ r, g, bl }) <= kMaxSpread;
}

void emitShape(skb_icon_builder_t& b, const NSVGshape* s, bool asStroke)
{
	skb_icon_builder_begin_shape(&b);
	if (asStroke) {
		emitStrokePaths(b, s);
		applyPaint(b, s->stroke, s->opacity);
	} else {
		emitFillPaths(b, s);
		applyPaint(b, s->fill, s->opacity);
	}
	skb_icon_builder_end_shape(&b);
}

} // namespace

skb_icon_handle_t RegisterNanoSVG(skb_icon_collection_t* collection,
                                  const char* name,
                                  const char* svg_data,
                                  size_t svg_size)
{
	if (!collection || !svg_data || svg_size == 0)
		return 0;

	// nsvgParse mutates the buffer — copy into a writable null-terminated
	// string. Icon SVGs are small (a few hundred bytes); the copy is free.
	std::string buf(svg_data, svg_size);

	NSVGimage* img = nsvgParse(buf.data(), "px", 96.0f);
	if (!img) return 0;

	const float w = img->width  > 0 ? img->width  : 1.f;
	const float h = img->height > 0 ? img->height : 1.f;

	skb_icon_handle_t handle = skb_icon_collection_add_icon(collection, name, w, h);
	if (!handle) {
		nsvgDelete(img);
		return 0;
	}

	skb_icon_builder_t builder = skb_icon_builder_make(collection, handle);

	// Drawn in one neutral grey (Material-style glyph icons): rasterize as a mask so
	// it takes the text color of wherever it's shown.
	if (isNeutralMonochrome(img))
		skb_icon_collection_set_is_color(collection, handle, false);

	for (NSVGshape* s = img->shapes; s; s = s->next) {
		if (!(s->flags & NSVG_FLAGS_VISIBLE)) continue;

		if (s->fill.type != NSVG_PAINT_NONE)
			emitShape(builder, s, /*asStroke=*/false);

		if (s->stroke.type != NSVG_PAINT_NONE && s->strokeWidth > 0.f)
			emitShape(builder, s, /*asStroke=*/true);
	}

	nsvgDelete(img);
	return handle;
}

} // namespace interstice
