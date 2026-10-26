#include "nanosvg_bridge.h"

#define NANOSVG_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning( disable: 4702)
#endif
#include <nanosvg.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

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

// NanoSVG xform[6] layout: [xx, yx, xy, yy, dx, dy] - same field order as
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
		// NanoSVG stores the inverse xform on the gradient (user-space ->
		// gradient-space). Skribidi pushes the xform onto its transform
		// stack to map gradient-space -> user-space, so invert back.
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
// Skribidi's icon model has no native strokes - only filled paths. Each
// NanoSVG path is linearized to a polyline, and the stroke becomes a set of
// polygons: a quad per segment, a wedge per join and a cap per open end.
// They are all wound the same way, so the non-zero fill paints their union
// and overlaps never cancel. Dashed strokes are split into open pieces first.

struct Pt { float x, y; };

inline Pt operator+(Pt a, Pt b) { return { a.x + b.x, a.y + b.y }; }
inline Pt operator-(Pt a, Pt b) { return { a.x - b.x, a.y - b.y }; }
inline Pt operator*(Pt a, float s) { return { a.x * s, a.y * s }; }

inline float dot(Pt a, Pt b)   { return a.x * b.x + a.y * b.y; }
inline float cross(Pt a, Pt b) { return a.x * b.y - a.y * b.x; }
inline float length(Pt v) { return std::sqrt(dot(v, v)); }

inline Pt normalize(Pt v)
{
	const float l = length(v);
	if (l < 1e-6f) return { 0, 0 };
	return { v.x / l, v.y / l };
}

inline Pt perp(Pt v) { return { -v.y, v.x }; }

inline bool samePoint(Pt a, Pt b) { return std::fabs(a.x - b.x) < 1e-5f && std::fabs(a.y - b.y) < 1e-5f; }

void flattenCubic(std::vector<Pt>& out, Pt p0, Pt p1, Pt p2, Pt p3, int steps)
{
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
	// A straight segment (control points on the chord) needs one step;
	// curves get one per ~2 units of control polygon, 4 to 32.
	if (std::fabs(cross(p3 - p0, p1 - p0)) < 1e-4f && std::fabs(cross(p3 - p0, p2 - p0)) < 1e-4f)
		return 1;
	const float chordLen = length(p1 - p0) + length(p2 - p1) + length(p3 - p2);
	return std::clamp((int)std::ceil(chordLen / 2.0f), 4, 32);
}

// The polyline of a NanoSVG path, without consecutive duplicates. A closed
// path doesn't repeat its first point at the end.
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
		flattenCubic(out, p0, c0, c1, p3, chooseSteps(p0, c0, c1, p3));
	}
	out.erase(std::unique(out.begin(), out.end(), samePoint), out.end());
	if (p->closed && out.size() > 1 && samePoint(out.front(), out.back()))
		out.pop_back();
}

struct StrokeStyle
{
	float halfWidth;
	float miterLimit;
	char  join;
	char  cap;
};

// Emits a polygon with positive winding, whatever order its points come in.
void emitPolygon(skb_icon_builder_t& b, const std::vector<Pt>& pts)
{
	const int n = (int)pts.size();
	if (n < 3) return;
	float area = 0.f;
	for (int i = 0; i < n; ++i)
		area += cross(pts[i], pts[(i + 1) % n]);
	if (std::fabs(area) < 1e-8f) return;
	const bool forward = area > 0.f;
	for (int k = 0; k < n; ++k) {
		const Pt& p = pts[forward ? k : n - 1 - k];
		if (k == 0) skb_icon_builder_move_to(&b, skb_vec2_t{ p.x, p.y });
		else        skb_icon_builder_line_to(&b, skb_vec2_t{ p.x, p.y });
	}
	skb_icon_builder_close_path(&b);
}

// Points on the circle of radius |from| around `center`, from `from` turning
// by `angle` radians; the first point is left out.
void appendArc(std::vector<Pt>& out, Pt center, Pt from, float angle)
{
	constexpr float kMaxStep = 3.14159265f / 8.f;
	const int steps = std::max(1, (int)std::ceil(std::fabs(angle) / kMaxStep));
	for (int i = 1; i <= steps; ++i) {
		const float a = angle * (float)i / (float)steps;
		const float c = std::cos(a), s = std::sin(a);
		out.push_back(center + Pt{ from.x * c - from.y * s, from.x * s + from.y * c });
	}
}

void emitSegment(skb_icon_builder_t& b, Pt p0, Pt p1, float halfWidth, std::vector<Pt>& scratch)
{
	const Pt n = perp(normalize(p1 - p0)) * halfWidth;
	scratch.assign({ p0 + n, p1 + n, p1 - n, p0 - n });
	emitPolygon(b, scratch);
}

// Fills the gap on the outer side of the corner at `v` between a segment
// running along `d0` and the next running along `d1`.
void emitJoin(skb_icon_builder_t& b, Pt v, Pt d0, Pt d1, const StrokeStyle& style, std::vector<Pt>& scratch)
{
	const float turn = cross(d0, d1);
	if (std::fabs(turn) < 1e-6f && dot(d0, d1) > 0.f)
		return; // straight on
	// The outer side is opposite the turn.
	const float side = turn > 0.f ? -1.f : 1.f;
	const Pt n0 = perp(d0) * (side * style.halfWidth);
	const Pt n1 = perp(d1) * (side * style.halfWidth);

	scratch.assign({ v, v + n0 });
	if (style.join == NSVG_JOIN_ROUND) {
		appendArc(scratch, v, n0, std::atan2(cross(n0, n1), dot(n0, n1)));
	} else {
		const float denom = 1.f + dot(perp(d0), perp(d1));
		if (style.join == NSVG_JOIN_MITER && denom > 1e-6f) {
			const Pt miter = (n0 + n1) * (1.f / denom);
			if (length(miter) <= style.halfWidth * style.miterLimit)
				scratch.push_back(v + miter);
		}
		scratch.push_back(v + n1);
	}
	emitPolygon(b, scratch);
}

// The cap at end point `p` of an open stroke; `out` points away from the stroke.
void emitCap(skb_icon_builder_t& b, Pt p, Pt out, const StrokeStyle& style, std::vector<Pt>& scratch)
{
	const Pt n = perp(out) * style.halfWidth;
	const Pt e = out * style.halfWidth;
	if (style.cap == NSVG_CAP_SQUARE) {
		scratch.assign({ p + n, p + n + e, p - n + e, p - n });
		emitPolygon(b, scratch);
	} else if (style.cap == NSVG_CAP_ROUND) {
		scratch.assign({ p + n });
		appendArc(scratch, p, n, -3.14159265f);
		emitPolygon(b, scratch);
	}
}

void emitStroke(skb_icon_builder_t& b, const std::vector<Pt>& poly, bool closed, const StrokeStyle& style)
{
	std::vector<Pt> scratch;
	const int n = (int)poly.size();
	if (n == 1) {
		// A zero-length stroke shows only its caps: a dot or a square.
		emitCap(b, poly[0], { 1.f, 0.f }, style, scratch);
		emitCap(b, poly[0], { -1.f, 0.f }, style, scratch);
		return;
	}
	if (n < 2) return;

	const int segments = closed && n > 2 ? n : n - 1;
	auto dir = [&](int s) { return normalize(poly[(s + 1) % n] - poly[s]); };
	for (int s = 0; s < segments; ++s)
		emitSegment(b, poly[s], poly[(s + 1) % n], style.halfWidth, scratch);
	for (int s = 1; s < segments; ++s)
		emitJoin(b, poly[s], dir(s - 1), dir(s), style, scratch);

	if (segments == n) {
		emitJoin(b, poly[0], dir(n - 1), dir(0), style, scratch);
	} else {
		emitCap(b, poly[0],     dir(0) * -1.f,  style, scratch);
		emitCap(b, poly[n - 1], dir(n - 2),     style, scratch);
	}
}

// Splits a polyline into the open pieces a dash pattern keeps.
void splitDashes(const std::vector<Pt>& poly, bool closed, const float* dashes, int dashCount, float offset,
                 std::vector<std::vector<Pt>>& out)
{
	// An odd-length pattern repeats to even length.
	std::vector<float> pattern(dashes, dashes + dashCount);
	if (pattern.size() % 2 == 1)
		pattern.insert(pattern.end(), dashes, dashes + dashCount);
	float total = 0.f;
	for (float d : pattern) total += d;

	std::vector<Pt> path = poly;
	if (closed && path.size() > 1)
		path.push_back(path.front());

	float phase = std::fmod(offset, total);
	if (phase < 0.f) phase += total;
	size_t index = 0;
	while (phase >= pattern[index]) {
		phase -= pattern[index];
		index = (index + 1) % pattern.size();
	}
	float remaining = pattern[index] - phase;

	std::vector<Pt> dash;
	if (index % 2 == 0)
		dash.push_back(path[0]);
	for (size_t i = 0; i + 1 < path.size(); ++i) {
		const Pt p0 = path[i], p1 = path[i + 1];
		const float len = length(p1 - p0);
		if (len <= 0.f) continue;
		float pos = 0.f;
		while (pos < len) {
			const float step = std::min(remaining, len - pos);
			pos += step;
			remaining -= step;
			const Pt at = p0 + (p1 - p0) * (pos / len);
			if (index % 2 == 0)
				dash.push_back(at);
			if (remaining <= 1e-6f) {
				if (index % 2 == 0) {
					dash.erase(std::unique(dash.begin(), dash.end(), samePoint), dash.end());
					out.push_back(std::move(dash));
				}
				dash.clear();
				index = (index + 1) % pattern.size();
				remaining = pattern[index];
				if (index % 2 == 0)
					dash.push_back(at);
			}
		}
	}
	dash.erase(std::unique(dash.begin(), dash.end(), samePoint), dash.end());
	if (!dash.empty())
		out.push_back(std::move(dash));
}

void emitStrokePaths(skb_icon_builder_t& b, const NSVGshape* s)
{
	const StrokeStyle style {
		0.5f * s->strokeWidth,
		s->miterLimit > 0 ? s->miterLimit : 4.f,
		s->strokeLineJoin,
		s->strokeLineCap,
	};
	float dashTotal = 0.f;
	for (int i = 0; i < s->strokeDashCount; ++i)
		dashTotal += s->strokeDashArray[i];

	std::vector<Pt> poly;
	std::vector<std::vector<Pt>> dashes;
	for (NSVGpath* p = s->paths; p; p = p->next) {
		flattenPath(p, poly);
		if (poly.empty()) continue;
		if (dashTotal <= 0.f) {
			emitStroke(b, poly, p->closed != 0, style);
			continue;
		}
		dashes.clear();
		splitDashes(poly, p->closed != 0, s->strokeDashArray, s->strokeDashCount, s->strokeDashOffset, dashes);
		for (const std::vector<Pt>& dash : dashes)
			emitStroke(b, dash, /*closed*/ false, style);
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

	// nsvgParse mutates the buffer - copy into a writable null-terminated
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
