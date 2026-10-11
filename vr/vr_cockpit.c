/* Part of Descent 1 & 2 VR, a derivative of DXX-Redux.
 * Distributed under the Parallax and D1X-Rebirth licenses in COPYING.txt:
 * non-commercial use only, and the source of any modified version must be
 * freely and publicly available. */

/* vr_cockpit.c - the ship's cockpit round the player, for both games.
 *
 * Modelled in 3D on each game's own cockpit: a console curving round below
 * the view, its face leaning back towards the player, with the two weapon
 * screens either side of the round ship-and-shields hub, the energy bars
 * over the screens, the key lights and the LOCK warning at the sides; a
 * ledge above it with two vent grilles and a bracket over the hub; and two
 * riveted canopy struts arching up from the ledge and back over the head.
 *
 * The gauges are the game's own and live: once a frame the engine draws its
 * full-cockpit gauges (render_gauges, the code that draws them on the flat
 * cockpit) into a see-through picture, which lies over the console's face.
 * Everything here is laid out in that picture's coordinates (the flat
 * cockpit's, as fractions of its width and height), so each gauge lands in
 * its screen, hub or housing.  Both games share the layout; Descent 2 has
 * its key lights on the right and an afterburner gauge on the left.
 *
 * It is fixed to the ship: still while the head looks round it, the mine
 * moving past outside (and turning with the ship under Swivel chair, like
 * the HUD).  Lit by the mine: the light the engine works out at the ship
 * (segment light plus flares, shots and explosions, in color) shades the
 * metal each frame; the gauges and accent lights light themselves.
 *
 * Plain geometry with colors per vertex plus the one gauge picture: OpenGL
 * 1.1 arrays on PC and the same calls under OpenGL ES 1.1 on Quest.
 *
 * Written for this port, against the engine's gauge and
 * lighting code and vr_xr.h's eye; the shapes follow the games' cockpit art,
 * read from the player's own copy for reference. */

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* VRD_COCKPIT_MESH_ONLY: just the mesh, for the desk preview
 * (tools/cockpit_preview.c); no engine. */
#ifndef VRD_COCKPIT_MESH_ONLY
#include "pstypes.h"
#include "maths.h"
#include "vecmat.h"
#include "game.h"
#include "object.h"
#include "player.h"
#include "lighting.h"
#include "ogl_init.h"
#include "internal.h"		/* last_width, last_height */
#include "gr.h"
#include "args.h"
#ifdef __ANDROID__
#include <EGL/egl.h>
#endif

#include "vr_descent.h"
#include "vr_xr.h"
#endif

/* ---- the layout ------------------------------------------------------------------
 * In the flat cockpit's picture: u across 0..1, v down 0..1 (640 x 480 for
 * Descent 2's hi-res cockpit; Descent's 320 x 200 scales to the same). */
#define PX(x) ((x) / 640.0f)
#define PY(y) ((y) / 480.0f)

#define V_TOP   0.555f		/* the console's top edge, under the ledge */
#define SPAN    1.75f		/* radians the picture's width wraps round the player */
#define R_TOP   0.66f		/* metres out, at the top edge ... */
#define R_BOT   0.50f		/* ... and at the bottom: the face leans back */
#define Y_TOP   (-0.28f)	/* metres, from the eyes */
#define Y_BOT   (-0.60f)

/* The gauges, from the engine's own cockpit positions (gauges.c, hi-res). */
#define PRI_U0 PX(121)
#define PRI_U1 PX(242)
#define SEC_U0 PX(403)
#define SEC_U1 PX(530)
#define BOX_V0 PY(364)
#define BOX_V1 PY(470)
#define HUB_U  0.5f
#define HUB_V  PY(400)
#define HUB_RU PX(56)		/* the hub ring's radius in u ... */
#define HUB_RV PY(56)		/* ... and in v (round in the picture) */
#define NUM_U0 PX(296)		/* the shield and energy numbers over the hub */
#define NUM_U1 PX(344)
#define NUM_V0 PY(310)
#define NUM_V1 PY(338)
#define ENL_U0 PX(134)		/* the energy bars over the screens */
#define ENL_U1 PX(260)
#define ENR_U0 PX(377)
#define ENR_U1 PX(505)
#define EN_V0  PY(310)
#define EN_V1  PY(334)
#define LOCK_U0 PX(8)		/* the homing warning, low on the left */
#define LOCK_U1 PX(62)
#define LOCK_V0 PY(410)
#define LOCK_V1 PY(452)
#ifdef VRD_DESCENT2
#define KEY_U0 PX(531)		/* the key lights: Descent 2's on the right */
#define KEY_U1 PX(566)
#define AB_U0  PX(84)		/* its afterburner gauge, on the left */
#define AB_U1  PX(112)
#define AB_V0  PY(374)
#define AB_V1  PY(440)
#else
#define KEY_U0 PX(83)		/* Descent's on the left */
#define KEY_U1 PX(118)
#endif
#define KEY_V0 PY(370)
#define KEY_V1 PY(440)

/* ---- the mesh ------------------------------------------------------------------
 * Built once, in metres, in the cockpit: x right, y up, -z ahead, the
 * player's head at the origin when they last recentered. */

#define MAX_VERTS 120000

static GLfloat s_pos[MAX_VERTS * 3];
static GLfloat s_base[MAX_VERTS * 4];	/* shaded color before the mine's light */
static unsigned char s_glow[MAX_VERTS];	/* 1 = lights itself */
static GLfloat s_uv[MAX_VERTS * 2];	/* into the metal texture (vr_cockpit_metal) */
static GLfloat s_lit[MAX_VERTS * 4];	/* what is drawn this frame */
static int s_n, s_built;

/* The gauge picture's surface: positions and picture coordinates. */
#define DECAL_NU 48
#define DECAL_NV 10
static GLfloat s_dpos[DECAL_NU * DECAL_NV * 6 * 3];
static GLfloat s_duv[DECAL_NU * DECAL_NV * 6 * 2];
static int s_dn;

static void v3(float o[3], float x, float y, float z) { o[0] = x; o[1] = y; o[2] = z; }
static void sub(float o[3], const float a[3], const float b[3]) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
static void cross(float o[3], const float a[3], const float b[3])
{
	const float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
	o[0] = x; o[1] = y; o[2] = z;
}
static void norm(float v[3])
{
	const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	if (l > 1e-6f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static void lerp3(float o[3], const float a[3], const float b[3], float t)
{
	o[0] = a[0] + (b[0] - a[0]) * t; o[1] = a[1] + (b[1] - a[1]) * t; o[2] = a[2] + (b[2] - a[2]) * t;
}

/* The metal: plates with seams, rivets along them, fine brushing and a
 * little wear, in grey, made once; the vertex colors tint and light it.
 * A white patch in its corner is for the lights. */
#define TEX_N 512
#define TEX_WHITE_U (4.0f / TEX_N)
#define TEX_WHITE_V (4.0f / TEX_N)
static unsigned char s_tex[TEX_N * TEX_N * 4];

static unsigned int rnd(unsigned int *st) { *st = *st * 1664525u + 1013904223u; return *st >> 8; }

static void make_metal(void)
{
	unsigned int st = 1995u;
	static float row[TEX_N];
	int x, y, i;
	/* brushing: a value per row, a little noise per texel */
	for (y = 0; y < TEX_N; y++)
		row[y] = 0.78f + 0.06f * ((rnd(&st) & 255) / 255.0f - 0.5f);
	for (y = 0; y < TEX_N; y++)
		for (x = 0; x < TEX_N; x++) {
			float v = row[y] + 0.05f * ((rnd(&st) & 255) / 255.0f - 0.5f);
			/* the plate seams: every 128 texels, a dark groove with a lit edge */
			const int sx = x % 128, sy = y % 128;
			if (sx == 0 || sy == 0) v *= 0.42f;
			else if (sx == 1 || sy == 1) v *= 0.62f;
			else if (sx == 2 || sy == 2) v *= 1.14f;
			/* a smaller panel line inside each plate */
			if ((sx == 64 && sy > 8 && sy < 120) || (sy == 64 && sx > 8 && sx < 56)) v *= 0.80f;
			if (v > 1.0f) v = 1.0f;
			i = 4 * (y * TEX_N + x);
			s_tex[i] = s_tex[i + 1] = s_tex[i + 2] = (unsigned char)(v * 255.0f);
			s_tex[i + 3] = 255;
		}
	/* rivets along the seams: a dark ring, a lit dome */
	for (y = 10; y < TEX_N; y += 128)
		for (x = 12; x < TEX_N; x += 20) {
			int k, dx, dy;
			for (k = 0; k < 2; k++) {
				const int cx = k ? y : x, cy = k ? x : y;
				for (dy = -3; dy <= 3; dy++)
					for (dx = -3; dx <= 3; dx++) {
						const int r2 = dx * dx + dy * dy, px = (cx + dx) & (TEX_N - 1), py = (cy + dy) & (TEX_N - 1);
						unsigned char *t = &s_tex[4 * (py * TEX_N + px)];
						float v = t[0] / 255.0f;
						if (r2 > 9) continue;
						if (r2 >= 6) v *= 0.55f;
						else v = 0.92f - 0.06f * (dx + dy);
						t[0] = t[1] = t[2] = (unsigned char)((v > 1 ? 1 : v) * 255.0f);
					}
			}
		}
	/* wear: soft dark smudges */
	for (i = 0; i < 40; i++) {
		const int cx = rnd(&st) % TEX_N, cy = rnd(&st) % TEX_N, r = 6 + rnd(&st) % 18;
		int dx, dy;
		for (dy = -r; dy <= r; dy++)
			for (dx = -r; dx <= r; dx++) {
				const float d = (float)(dx * dx + dy * dy) / (float)(r * r);
				unsigned char *t = &s_tex[4 * (((cy + dy) & (TEX_N - 1)) * TEX_N + ((cx + dx) & (TEX_N - 1)))];
				if (d < 1.0f) {
					const float v = t[0] * (1.0f - 0.10f * (1.0f - d));
					t[0] = t[1] = t[2] = (unsigned char)v;
				}
			}
	}
	/* the white patch for the lights */
	for (y = 0; y < 9; y++)
		for (x = 0; x < 9; x++) {
			unsigned char *t = &s_tex[4 * (y * TEX_N + x)];
			t[0] = t[1] = t[2] = 255;
		}
}

/* A point on the console's face: u, v in the picture, `out` metres towards
 * the player off the surface. */
static void face_pt(float o[3], float u, float v, float out)
{
	const float th = (u - 0.5f) * SPAN, t = (v - V_TOP) / (1.0f - V_TOP);
	const float r = R_TOP + (R_BOT - R_TOP) * t - out;
	o[0] = r * sinf(th);
	o[1] = Y_TOP + (Y_BOT - Y_TOP) * t + out * 0.45f;	/* the face leans back: out is also a little up */
	o[2] = -r * cosf(th);
}

/* A point in cockpit polar terms: angle (0 ahead, + right), radius, height. */
static void polar(float o[3], float th, float r, float y)
{
	o[0] = r * sinf(th); o[1] = y; o[2] = -r * cosf(th);
}

/* The point inside the solid being built: each face's normal is turned to
 * point away from it, whichever way its corners run. */
static float s_inside[3];
static int s_inside_head;	/* 1: faces point towards the head instead (the console's face) */

/* One triangle, flat shaded: light from above, a little ahead and to the
 * right, with enough ambient that the shadowed sides still read. */
static void tri(const float a[3], const float b[3], const float c[3], const float col[3], int glow)
{
	static const float L[3] = { 0.25f, 0.85f, -0.45f };
	float e1[3], e2[3], n[3], mid[3], k;
	const float *p[3];
	int i;
	if (s_n + 3 > MAX_VERTS)
		return;
	sub(e1, b, a); sub(e2, c, a); cross(n, e1, e2); norm(n);
	for (i = 0; i < 3; i++)
		mid[i] = s_inside_head ? -((a[i] + b[i] + c[i]) / 3.0f) : (a[i] + b[i] + c[i]) / 3.0f - s_inside[i];
	if (n[0] * mid[0] + n[1] * mid[1] + n[2] * mid[2] < 0.0f) {
		n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2];
	}
	k = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
	if (k < 0.0f) k = -0.25f * k;		/* the far side catches a little bounce */
	k = glow ? 1.0f : 0.32f + 0.68f * k;
	p[0] = a; p[1] = b; p[2] = c;
	for (i = 0; i < 3; i++) {
		/* The metal texture laid on flat from whichever side the face looks
		 * most along, two plates to the metre; lights take its white patch. */
		const float ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
		float tu, tv;
		if (glow) { tu = TEX_WHITE_U; tv = TEX_WHITE_V; }
		else if (ay >= ax && ay >= az) { tu = p[i][0]; tv = p[i][2]; }
		else if (ax >= az) { tu = p[i][2]; tv = p[i][1]; }
		else { tu = p[i][0]; tv = p[i][1]; }
		if (!glow) { tu *= 2.0f; tv *= 2.0f; }
		s_uv[2 * s_n] = tu;
		s_uv[2 * s_n + 1] = tv;
		memcpy(&s_pos[3 * s_n], p[i], 3 * sizeof(float));
		s_base[4 * s_n] = col[0] * k;
		s_base[4 * s_n + 1] = col[1] * k;
		s_base[4 * s_n + 2] = col[2] * k;
		s_base[4 * s_n + 3] = 1.0f;
		s_glow[s_n] = (unsigned char)glow;
		s_n++;
	}
}

static void quad(const float a[3], const float b[3], const float c[3], const float d[3], const float col[3], int glow)
{
	tri(a, b, c, col, glow);
	tri(a, c, d, col, glow);
}

/* A length of section `prof` swept from p0 to p1 (u along the sweep crossed
 * with `up`, v the third axis), closed and capped. */
static void extrude(const float p0[3], const float p1[3], const float up[3],
	const float (*prof)[2], int n, const float col[3], int glow)
{
	float ax[3], side[3], vv[3], a[16][3], b[16][3], c0[3], c1[3];
	int i, k;
	sub(ax, p1, p0); norm(ax);
	cross(side, ax, up); norm(side);
	cross(vv, side, ax); norm(vv);
	v3(c0, 0, 0, 0); v3(c1, 0, 0, 0);
	for (i = 0; i < n && i < 16; i++)
		for (k = 0; k < 3; k++) {
			a[i][k] = p0[k] + side[k] * prof[i][0] + vv[k] * prof[i][1];
			b[i][k] = p1[k] + side[k] * prof[i][0] + vv[k] * prof[i][1];
			c0[k] += a[i][k] / n;
			c1[k] += b[i][k] / n;
		}
	s_inside_head = 0;
	for (i = 0; i < 3; i++)
		s_inside[i] = (c0[i] + c1[i]) * 0.5f;
	for (i = 0; i < n; i++) {
		const int j = (i + 1) % n;
		quad(a[i], a[j], b[j], b[i], col, glow);
		tri(c0, a[j], a[i], col, glow);
		tri(c1, b[i], b[j], col, glow);
	}
}

/* A beam's section: w x d with its corners cut, like machined metal. */
static void beam(const float p0[3], const float p1[3], const float up[3], float w, float d, const float col[3], int glow)
{
	float o[8][2];
	const float c = (w < d ? w : d) * 0.22f, x = w / 2, y = d / 2;
	o[0][0] = -x + c; o[0][1] = -y; o[1][0] = x - c; o[1][1] = -y;
	o[2][0] = x; o[2][1] = -y + c; o[3][0] = x; o[3][1] = y - c;
	o[4][0] = x - c; o[4][1] = y; o[5][0] = -x + c; o[5][1] = y;
	o[6][0] = -x; o[6][1] = y - c; o[7][0] = -x; o[7][1] = -y + c;
	extrude(p0, p1, up, (const float (*)[2])o, 8, col, glow);
}

/* A patch of the console's face, `out` off it, nu x nv quads. */
static void face_patch(float u0, float v0, float u1, float v1, float out, int nu, int nv, const float col[3], int glow)
{
	int i, j;
	s_inside_head = 1;
	for (i = 0; i < nu; i++)
		for (j = 0; j < nv; j++) {
			float a[3], b[3], c[3], d[3];
			const float ua = u0 + (u1 - u0) * i / nu, ub = u0 + (u1 - u0) * (i + 1) / nu;
			const float va = v0 + (v1 - v0) * j / nv, vb = v0 + (v1 - v0) * (j + 1) / nv;
			face_pt(a, ua, va, out); face_pt(b, ub, va, out);
			face_pt(c, ub, vb, out); face_pt(d, ua, vb, out);
			quad(a, b, c, d, col, glow);
		}
	s_inside_head = 0;
}

/* A wall standing off the face along a line in the picture, from `out0` to
 * `out1` off it: the sides of a raised frame. */
static void face_wall(float ua, float va, float ub, float vb, float out0, float out1, int n, const float col[3])
{
	int i;
	s_inside_head = 1;
	for (i = 0; i < n; i++) {
		float a[3], b[3], c[3], d[3];
		const float t0 = (float)i / n, t1 = (float)(i + 1) / n;
		face_pt(a, ua + (ub - ua) * t0, va + (vb - va) * t0, out0);
		face_pt(b, ua + (ub - ua) * t1, va + (vb - va) * t1, out0);
		face_pt(c, ua + (ub - ua) * t1, va + (vb - va) * t1, out1);
		face_pt(d, ua + (ub - ua) * t0, va + (vb - va) * t0, out1);
		quad(a, b, c, d, col, 0);
	}
	s_inside_head = 0;
}

/* A raised frame round a rectangle of the picture: `bu`, `bv` its rim widths,
 * `h` how far it stands off the face. */
static void frame(float u0, float v0, float u1, float v1, float bu, float bv, float h, const float col[3])
{
	const int nu = 6, nv = 3;
	face_patch(u0 - bu, v0 - bv, u1 + bu, v0, h, nu, 1, col, 0);	/* top rim */
	face_patch(u0 - bu, v1, u1 + bu, v1 + bv, h, nu, 1, col, 0);	/* bottom rim */
	face_patch(u0 - bu, v0, u0, v1, h, 1, nv, col, 0);		/* left rim */
	face_patch(u1, v0, u1 + bu, v1, h, 1, nv, col, 0);		/* right rim */
	/* outer walls */
	face_wall(u0 - bu, v0 - bv, u1 + bu, v0 - bv, 0.0f, h, nu, col);
	face_wall(u0 - bu, v1 + bv, u1 + bu, v1 + bv, 0.0f, h, nu, col);
	face_wall(u0 - bu, v0 - bv, u0 - bu, v1 + bv, 0.0f, h, nv, col);
	face_wall(u1 + bu, v0 - bv, u1 + bu, v1 + bv, 0.0f, h, nv, col);
	/* inner walls, down to the glass */
	face_wall(u0, v0, u1, v0, 0.0f, h, nu, col);
	face_wall(u0, v1, u1, v1, 0.0f, h, nu, col);
	face_wall(u0, v0, u0, v1, 0.0f, h, nv, col);
	face_wall(u1, v0, u1, v1, 0.0f, h, nv, col);
}

/* The hub's ring: an annulus round (HUB_U, HUB_V), raised, with its walls. */
static void hub_ring(float h, const float col[3])
{
	const int n = 28;
	const float r0 = 1.0f, r1 = 1.28f;
	int i;
	s_inside_head = 1;
	for (i = 0; i < n; i++) {
		const float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
		float p[4][3], q[4][3];
		face_pt(p[0], HUB_U + HUB_RU * r0 * cosf(a0), HUB_V + HUB_RV * r0 * sinf(a0), h);
		face_pt(p[1], HUB_U + HUB_RU * r0 * cosf(a1), HUB_V + HUB_RV * r0 * sinf(a1), h);
		face_pt(p[2], HUB_U + HUB_RU * r1 * cosf(a1), HUB_V + HUB_RV * r1 * sinf(a1), h);
		face_pt(p[3], HUB_U + HUB_RU * r1 * cosf(a0), HUB_V + HUB_RV * r1 * sinf(a0), h);
		quad(p[0], p[1], p[2], p[3], col, 0);
		face_pt(q[0], HUB_U + HUB_RU * r1 * cosf(a0), HUB_V + HUB_RV * r1 * sinf(a0), 0.0f);
		face_pt(q[1], HUB_U + HUB_RU * r1 * cosf(a1), HUB_V + HUB_RV * r1 * sinf(a1), 0.0f);
		quad(q[0], q[1], p[2], p[3], col, 0);
		face_pt(q[2], HUB_U + HUB_RU * r0 * cosf(a0), HUB_V + HUB_RV * r0 * sinf(a0), 0.0f);
		face_pt(q[3], HUB_U + HUB_RU * r0 * cosf(a1), HUB_V + HUB_RV * r0 * sinf(a1), 0.0f);
		quad(q[2], q[3], p[1], p[0], col, 0);
	}
	s_inside_head = 0;
}

/* Each game's metal: Descent's a blue-grey, Descent 2's a warmer grey. */
#ifdef VRD_DESCENT2
static const float METAL[3]  = { 0.36f, 0.37f, 0.36f };
static const float FACE[3]   = { 0.29f, 0.30f, 0.29f };
#else
static const float METAL[3]  = { 0.33f, 0.37f, 0.42f };
static const float FACE[3]   = { 0.27f, 0.30f, 0.35f };
#endif
static const float DARK[3]   = { 0.11f, 0.12f, 0.13f };
static const float GLASS[3]  = { 0.015f, 0.02f, 0.025f };
static const float ACCENT[3] = { 0.10f, 0.36f, 0.68f };

/* The ledge over the console: it runs out from the console's top edge
 * towards the windshield, with a drop at its front. */
#define LEDGE_R1 0.79f		/* its far edge, metres out */
#define LEDGE_Y  (Y_TOP + 0.012f)

/* A canopy strut through these points (angle, radius, height), riveted. */
static void strut(const float (*pt)[3], int n, int side)
{
	float a[3], b[3], up[3], prev[3];
	int i, k;
	for (i = 0; i + 1 < n; i++) {
		const int steps = 6;
		for (k = 0; k < steps; k++) {
			/* Catmull-Rom between pt[i] and pt[i + 1] */
			float p[2][3];
			int e;
			for (e = 0; e < 2; e++) {
				const float t = (float)(k + e) / steps;
				const float *P0 = pt[i > 0 ? i - 1 : 0], *P1 = pt[i], *P2 = pt[i + 1], *P3 = pt[i + 2 < n ? i + 2 : n - 1];
				float q[3];
				int c;
				for (c = 0; c < 3; c++)
					q[c] = 0.5f * ((2 * P1[c]) + (-P0[c] + P2[c]) * t + (2 * P0[c] - 5 * P1[c] + 4 * P2[c] - P3[c]) * t * t
						+ (-P0[c] + 3 * P1[c] - 3 * P2[c] + P3[c]) * t * t * t);
				polar(p[e], q[0] * side, q[1], q[2]);
			}
			memcpy(a, p[0], sizeof(a)); memcpy(b, p[1], sizeof(b));
			/* the section's v faces the cockpit's middle, above the head */
			v3(up, -a[0], 0.6f - a[1], -a[2]);
			norm(up);
			beam(a, b, up, 0.050f, 0.064f, METAL, 0);
			/* a collar at every other step, and a rivet on the inner face */
			if (k % 2 == 0) {
				float c0[3], c1[3];
				lerp3(c0, a, b, 0.0f); lerp3(c1, a, b, 0.18f);
				beam(c0, c1, up, 0.060f, 0.074f, DARK, 0);
			}
			{
				float r0[3], r1[3], m[3];
				lerp3(m, a, b, 0.55f);
				memcpy(r0, m, sizeof(m));
				r0[0] += up[0] * 0.030f; r0[1] += up[1] * 0.030f; r0[2] += up[2] * 0.030f;
				r1[0] = r0[0] + up[0] * 0.007f; r1[1] = r0[1] + up[1] * 0.007f; r1[2] = r0[2] + up[2] * 0.007f;
				sub(prev, b, a); norm(prev);
				beam(r0, r1, prev, 0.010f, 0.010f, DARK, 0);
			}
		}
	}
}

/* An arc of wall round the player, facing in: from angle th0 to th1 at
 * radius r, from height y0 up to y1. */
static void arc_wall(float th0, float th1, float r, float y0, float y1, int n, const float col[3])
{
	int i;
	s_inside_head = 1;
	for (i = 0; i < n; i++) {
		float a[3], b[3], c[3], d[3];
		const float ta = th0 + (th1 - th0) * i / n, tb = th0 + (th1 - th0) * (i + 1) / n;
		polar(a, ta, r, y0); polar(b, tb, r, y0); polar(c, tb, r, y1); polar(d, ta, r, y1);
		quad(a, b, c, d, col, 0);
	}
	s_inside_head = 0;
}

/* A flat ring between two radii at one height (a sill's top, the floor),
 * facing up. */
static void arc_flat(float th0, float th1, float r0, float r1, float y, int n, const float col[3])
{
	int i;
	for (i = 0; i < n; i++) {
		float a[3], b[3], c[3], d[3];
		const float ta = th0 + (th1 - th0) * i / n, tb = th0 + (th1 - th0) * (i + 1) / n;
		polar(a, ta, r0, y); polar(b, tb, r0, y); polar(c, tb, r1, y); polar(d, ta, r1, y);
		v3(s_inside, 0, y - 1.0f, 0);
		quad(a, b, c, d, col, 0);
	}
}

/* A rail along an arc: a beam per step. */
static void arc_rail(float th0, float th1, float r, float y, float w, float d, int n, const float col[3], int glow)
{
	static const float Y[3] = { 0, 1, 0 };
	int i;
	for (i = 0; i < n; i++) {
		float a[3], b[3];
		polar(a, th0 + (th1 - th0) * i / n, r, y);
		polar(b, th0 + (th1 - th0) * (i + 1) / n, r, y);
		beam(a, b, Y, w, d, col, glow);
	}
}

/* A box standing on a wall at angle th: depth out from the wall, its
 * centre at height y. */
static void wall_box(float th, float r, float y, float w, float h, float depth, const float col[3])
{
	float a[3], b[3], up[3];
	polar(a, th, r, y);
	polar(b, th, r - depth, y);
	v3(up, 0, 1, 0);
	beam(a, b, up, w, h, col, 0);
}

#define SILL_Y   (-0.13f)		/* the window sill, below the eyes */
#define FLOOR_Y  (-0.95f)
#define TUB_R    0.60f		/* the side walls' inner face */
#define REAR_TH  2.62f		/* where the side walls meet the rear bulkhead */

/* The tub round the player: the footwell under the console, the side walls
 * up to the window sill with a rail along it and a console on each, the rear
 * bulkhead, the seat and the floor.  Nothing open below the window line. */
static void build_tub(void)
{
	static const float LIT[3] = { 0.95f, 0.55f, 0.10f }, GRN[3] = { 0.20f, 0.85f, 0.35f };
	const float end = SPAN * 0.5f;		/* the console's ends */
	float a[3], b[3];
	int s, i;

	/* The footwell: from the console's folded-under edge down to the floor. */
	arc_wall(-end, end, R_BOT * 0.92f, FLOOR_Y, Y_BOT - 0.05f, 24, DARK);

	for (s = -1; s <= 1; s += 2) {
		const float t0 = s * (end - 0.02f), t1 = s * REAR_TH;
		/* the wall from the console's end back to the rear, floor to sill */
		arc_wall(t0, t1, TUB_R, FLOOR_Y, SILL_Y, 20, FACE);
		/* closing the gap between the console's end and the wall */
		arc_wall(t0, t0 + s * 0.001f, R_BOT * 0.92f, FLOOR_Y, LEDGE_Y, 1, DARK);
		{
			float p[4][3];
			polar(p[0], t0, R_BOT * 0.92f, FLOOR_Y); polar(p[1], t0, TUB_R + 0.01f, FLOOR_Y);
			polar(p[2], t0, TUB_R + 0.01f, LEDGE_Y); polar(p[3], t0, R_TOP, LEDGE_Y);
			v3(s_inside, s * -1.0f, -0.4f, -0.2f);
			quad(p[0], p[1], p[2], p[3], DARK, 0);
		}
		/* the sill: a flat top, a rounded rail along its inner edge, a lit seam */
		arc_flat(t0, t1, TUB_R - 0.07f, TUB_R + 0.02f, SILL_Y, 20, DARK);
		arc_rail(t0, t1, TUB_R - 0.07f, SILL_Y - 0.01f, 0.05f, 0.04f, 20, METAL, 0);
		arc_rail(t0 + s * 0.05f, t1 - s * 0.05f, TUB_R - 0.098f, SILL_Y - 0.03f, 0.003f, 0.003f, 16, ACCENT, 1);
		/* a console on the wall at hand height, with a few lit buttons */
		wall_box(s * 1.35f, TUB_R, -0.36f, 0.30f, 0.16f, 0.10f, DARK);
		wall_box(s * 1.35f, TUB_R - 0.10f, -0.36f, 0.26f, 0.12f, 0.012f, METAL);
		for (i = 0; i < 4; i++) {
			float c[3], d[3], up[3] = { 0, 1, 0 };
			const float th = s * (1.35f - 0.09f + 0.06f * i);
			polar(c, th, TUB_R - 0.112f, -0.33f);
			polar(d, th, TUB_R - 0.120f, -0.33f);
			beam(c, d, up, 0.016f, 0.012f, (i == 1 && s < 0) ? LIT : GRN, 1);
		}
		/* a lower panel line along the wall */
		arc_rail(t0, t1, TUB_R - 0.004f, -0.55f, 0.012f, 0.012f, 16, DARK, 0);
	}

	/* The rear bulkhead behind the seat, floor to above the head. */
	arc_wall(REAR_TH, 6.2831853f - REAR_TH, TUB_R, FLOOR_Y, 0.58f, 10, FACE);
	arc_rail(REAR_TH, 6.2831853f - REAR_TH, TUB_R - 0.03f, SILL_Y - 0.01f, 0.05f, 0.04f, 10, METAL, 0);
	arc_rail(REAR_TH, 6.2831853f - REAR_TH, TUB_R - 0.01f, 0.56f, 0.06f, 0.05f, 10, METAL, 0);

	/* The seat: its back behind the player, a headrest, the cushion below.
	 * The headrest's face is 38 cm behind the head, so leaning back in a real
	 * chair does not put the head through it. */
	{
		float p0[3], p1[3], up[3] = { 0, 0, 1 };
		v3(p0, -0.20f, -0.20f, 0.46f); v3(p1, 0.20f, -0.20f, 0.46f);
		beam(p0, p1, up, 0.70f, 0.07f, DARK, 0);		/* the back */
		v3(p0, -0.12f, 0.12f, 0.42f); v3(p1, 0.12f, 0.12f, 0.42f);
		beam(p0, p1, up, 0.16f, 0.07f, DARK, 0);		/* the headrest */
		v3(up, 0, 1, 0);
		v3(p0, -0.22f, -0.60f, 0.15f); v3(p1, 0.22f, -0.60f, 0.15f);
		beam(p0, p1, up, 0.48f, 0.10f, DARK, 0);		/* the cushion */
		for (s = -1; s <= 1; s += 2) {			/* the armrests */
			v3(p0, s * 0.27f, -0.42f, -0.10f); v3(p1, s * 0.27f, -0.42f, 0.41f);
			beam(p0, p1, up, 0.07f, 0.06f, METAL, 0);
		}
	}

	/* The floor, all round. */
	arc_flat(0.0f, 6.2831853f, 0.0f, TUB_R + 0.02f, FLOOR_Y, 36, DARK);
	(void)a; (void)b;
}

static void build(void)
{
	static const float Y[3] = { 0, 1, 0 };
	float a[3], b[3], c[3], d[3];
	int i, s;

	s_n = 0;

	/* The console's face, the full width of the picture. */
	face_patch(0.0f, V_TOP, 1.0f, 1.0f, 0.0f, DECAL_NU, 6, FACE, 0);
	/* Its lower edge, folding under towards the player. */
	for (i = 0; i < DECAL_NU; i++) {
		const float ua = (float)i / DECAL_NU, ub = (float)(i + 1) / DECAL_NU;
		face_pt(a, ua, 1.0f, 0.0f); face_pt(b, ub, 1.0f, 0.0f);
		c[0] = b[0] * 0.92f; c[1] = b[1] - 0.05f; c[2] = b[2] * 0.92f;
		d[0] = a[0] * 0.92f; d[1] = a[1] - 0.05f; d[2] = a[2] * 0.92f;
		v3(s_inside, 0, Y_BOT - 0.3f, 0);
		quad(a, b, c, d, DARK, 0);
	}

	/* The glass behind each gauge, then the raised frames round them. */
	face_patch(PRI_U0, BOX_V0, PRI_U1, BOX_V1, 0.001f, 6, 3, GLASS, 0);
	face_patch(SEC_U0, BOX_V0, SEC_U1, BOX_V1, 0.001f, 6, 3, GLASS, 0);
	frame(PRI_U0, BOX_V0, PRI_U1, BOX_V1, PX(10), PY(10), 0.016f, METAL);
	frame(SEC_U0, BOX_V0, SEC_U1, BOX_V1, PX(10), PY(10), 0.016f, METAL);
	face_patch(HUB_U - HUB_RU, HUB_V - HUB_RV, HUB_U + HUB_RU, HUB_V + HUB_RV, 0.001f, 4, 4, GLASS, 0);
	hub_ring(0.020f, METAL);
	face_patch(NUM_U0, NUM_V0, NUM_U1, NUM_V1, 0.001f, 2, 1, GLASS, 0);
	frame(NUM_U0, NUM_V0, NUM_U1, NUM_V1, PX(5), PY(5), 0.010f, DARK);
	face_patch(ENL_U0, EN_V0, ENL_U1, EN_V1, 0.001f, 4, 1, GLASS, 0);
	face_patch(ENR_U0, EN_V0, ENR_U1, EN_V1, 0.001f, 4, 1, GLASS, 0);
	frame(ENL_U0, EN_V0, ENL_U1, EN_V1, PX(5), PY(5), 0.008f, DARK);
	frame(ENR_U0, EN_V0, ENR_U1, EN_V1, PX(5), PY(5), 0.008f, DARK);
	face_patch(KEY_U0, KEY_V0, KEY_U1, KEY_V1, 0.001f, 1, 3, GLASS, 0);
	frame(KEY_U0, KEY_V0, KEY_U1, KEY_V1, PX(6), PY(6), 0.012f, METAL);
	face_patch(LOCK_U0, LOCK_V0, LOCK_U1, LOCK_V1, 0.001f, 2, 1, GLASS, 0);
	frame(LOCK_U0, LOCK_V0, LOCK_U1, LOCK_V1, PX(6), PY(6), 0.012f, METAL);
#ifdef VRD_DESCENT2
	face_patch(AB_U0, AB_V0, AB_U1, AB_V1, 0.001f, 1, 3, GLASS, 0);
	frame(AB_U0, AB_V0, AB_U1, AB_V1, PX(5), PY(5), 0.010f, DARK);
#endif

	/* The ledge: from the console's top edge out to the windshield, then a
	 * short drop, the full width. */
	for (i = 0; i < DECAL_NU; i++) {
		const float th0 = ((float)i / DECAL_NU - 0.5f) * SPAN, th1 = ((float)(i + 1) / DECAL_NU - 0.5f) * SPAN;
		float e[3], f[3];
		polar(a, th0, R_TOP + 0.004f, LEDGE_Y); polar(b, th1, R_TOP + 0.004f, LEDGE_Y);
		polar(c, th1, LEDGE_R1, LEDGE_Y + 0.006f); polar(d, th0, LEDGE_R1, LEDGE_Y + 0.006f);
		v3(s_inside, 0, LEDGE_Y - 0.4f, 0);
		quad(a, b, c, d, DARK, 0);
		polar(e, th1, LEDGE_R1, LEDGE_Y - 0.035f); polar(f, th0, LEDGE_R1, LEDGE_Y - 0.035f);
		v3(s_inside, 0, LEDGE_Y, 0);
		quad(d, c, e, f, DARK, 0);
		/* and the step down from the ledge to the console's top edge */
		face_pt(e, (float)(i + 1) / DECAL_NU, V_TOP, 0.0f);
		face_pt(f, (float)i / DECAL_NU, V_TOP, 0.0f);
		v3(s_inside, 0, LEDGE_Y, -2.0f);
		quad(a, b, e, f, FACE, 0);
	}
	/* a lit edge along the ledge's lip, nearest the player */
	for (i = 0; i < 16; i++) {
		const float th0 = ((float)i / 16 - 0.5f) * SPAN * 0.96f, th1 = ((float)(i + 1) / 16 - 0.5f) * SPAN * 0.96f;
		polar(a, th0, R_TOP + 0.002f, LEDGE_Y + 0.002f);
		polar(b, th1, R_TOP + 0.002f, LEDGE_Y + 0.002f);
		beam(a, b, Y, 0.003f, 0.003f, ACCENT, 1);
	}

	/* The vent grilles on the ledge, either side of the bracket: a dark well
	 * with slats running out towards the windshield. */
	for (s = 0; s < 2; s++) {
		const float u0 = s ? 0.585f : 0.215f, u1 = s ? 0.785f : 0.415f;
		const float th0 = (u0 - 0.5f) * SPAN, th1 = (u1 - 0.5f) * SPAN;
		const int slats = 18;
		for (i = 0; i < 8; i++) {
			const float ta = th0 + (th1 - th0) * i / 8, tb = th0 + (th1 - th0) * (i + 1) / 8;
			polar(a, ta, 0.685f, LEDGE_Y + 0.0015f); polar(b, tb, 0.685f, LEDGE_Y + 0.0015f);
			polar(c, tb, 0.765f, LEDGE_Y + 0.0060f); polar(d, ta, 0.765f, LEDGE_Y + 0.0060f);
			v3(s_inside, 0, LEDGE_Y - 0.4f, 0);
			quad(a, b, c, d, GLASS, 0);
		}
		for (i = 0; i <= slats; i++) {
			const float t = th0 + (th1 - th0) * i / slats;
			polar(a, t, 0.688f, LEDGE_Y + 0.006f);
			polar(b, t, 0.762f, LEDGE_Y + 0.010f);
			beam(a, b, Y, 0.0035f, 0.008f, METAL, 0);
		}
	}

	/* The bracket over the hub: a block on the ledge, and its arm down over
	 * the console's top edge, with a notch in the middle. */
	for (s = -1; s <= 1; s += 2) {
		const float th = s * 0.045f;
		polar(a, th, R_TOP - 0.01f, LEDGE_Y + 0.02f);
		polar(b, th, LEDGE_R1 - 0.03f, LEDGE_Y + 0.02f);
		beam(a, b, Y, 0.035f, 0.04f, METAL, 0);
		face_pt(c, 0.5f + s * 0.026f, V_TOP + 0.002f, 0.024f);
		face_pt(d, 0.5f + s * 0.026f, PY(300), 0.020f);
		beam(c, d, Y, 0.030f, 0.026f, METAL, 0);
		polar(a, th, R_TOP - 0.012f, LEDGE_Y + 0.02f);
		beam(a, c, Y, 0.032f, 0.03f, METAL, 0);
	}
	polar(a, -0.07f, LEDGE_R1 - 0.03f, LEDGE_Y + 0.035f);
	polar(b, 0.07f, LEDGE_R1 - 0.03f, LEDGE_Y + 0.035f);
	beam(a, b, Y, 0.03f, 0.03f, METAL, 0);

	build_tub();

	/* The canopy struts: up from the ledge either side of the vents, leaning
	 * out, then arching back over the player's shoulders. */
	{
		/* standing on the ledge just outside the vents (which end at 0.50
		 * radians), not on them */
		static const float arch[6][3] = {	/* angle, radius, height */
			{ 0.62f, 0.73f, LEDGE_Y + 0.01f },
			{ 0.70f, 0.69f, -0.08f },
			{ 0.88f, 0.62f,  0.20f },
			{ 1.18f, 0.53f,  0.44f },
			{ 1.75f, 0.47f,  0.56f },
			{ 2.55f, 0.52f,  0.55f },
		};
		strut(arch, 6, -1);
		strut(arch, 6, 1);
	}

	s_built = 1;
}

/* The gauge picture's surface: the console's face just off the glass,
 * picture v running down, the texture's rows bottom up. */
static void build_decal(void)
{
	int i, j, k;
	s_dn = 0;
	for (i = 0; i < DECAL_NU; i++)
		for (j = 0; j < DECAL_NV; j++) {
			const float u[2] = { (float)i / DECAL_NU, (float)(i + 1) / DECAL_NU };
			const float v[2] = { V_TOP + (1.0f - V_TOP) * j / DECAL_NV, V_TOP + (1.0f - V_TOP) * (j + 1) / DECAL_NV };
			static const int cu[6] = { 0, 1, 1, 0, 1, 0 }, cv[6] = { 0, 0, 1, 0, 1, 1 };
			for (k = 0; k < 6; k++) {
				face_pt(&s_dpos[3 * s_dn], u[cu[k]], v[cv[k]], 0.004f);
				s_duv[2 * s_dn] = u[cu[k]];
				s_duv[2 * s_dn + 1] = 1.0f - v[cv[k]];
				s_dn++;
			}
		}
}

#ifndef VRD_COCKPIT_MESH_ONLY
/* ---- the gauge picture ---------------------------------------------------------- */

/* The gauge picture: the flat cockpit's 4:3 screen, a whole number of
 * pixels for each pixel of the game's art, so that drawn unfiltered every
 * one is the same crisp square.  Descent's PC data has the 320x200 cockpit,
 * Descent 2's the 640x480 one.  Modern: 4 per 320x200 pixel; Classic,
 * which fills the view, 8. */
static int s_gw, s_gh;
static void gauge_size(int *w, int *h)
{
#ifdef VRD_DESCENT2
	const int hires = GameArg.GfxHiresGFXAvailable;
#else
	const int hires = HiresGFXAvailable;
#endif
	const int k = VrSet.cockpit == 2 ? 8 : 4;
	*w = 320 * k;
	*h = (hires ? 240 : 200) * k;
}

static GLuint s_gtex, s_gfbo, s_mtex;
static GLint s_prev_fbo;
static int s_gauges_ok;		/* the picture holds this frame's gauges */

#ifdef __ANDROID__
/* OpenGL ES 1 has framebuffers as an extension: through EGL. */
typedef void (*fn_gen)(GLsizei, GLuint *);
typedef void (*fn_bind)(GLenum, GLuint);
typedef void (*fn_tex)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*fn_status)(GLenum);
static fn_gen p_gen;
static fn_bind p_bind;
static fn_tex p_tex;
static fn_status p_status;
#define FBO_TARGET 0x8D40	/* GL_FRAMEBUFFER(_OES) */
#define FBO_COLOR0 0x8CE0	/* GL_COLOR_ATTACHMENT0(_OES) */
#define FBO_BINDING 0x8CA6	/* GL_FRAMEBUFFER_BINDING(_OES) */
#define FBO_COMPLETE 0x8CD5
static int fbo_procs(void)
{
	if (!p_gen) {
		p_gen = (fn_gen)eglGetProcAddress("glGenFramebuffersOES");
		p_bind = (fn_bind)eglGetProcAddress("glBindFramebufferOES");
		p_tex = (fn_tex)eglGetProcAddress("glFramebufferTexture2DOES");
		p_status = (fn_status)eglGetProcAddress("glCheckFramebufferStatusOES");
	}
	return p_gen && p_bind && p_tex && p_status;
}
#define GEN_FBO(n, p) p_gen(n, p)
#define BIND_FBO(f) p_bind(FBO_TARGET, f)
#define TEX_FBO(t) p_tex(FBO_TARGET, FBO_COLOR0, GL_TEXTURE_2D, t, 0)
#define STATUS_FBO() p_status(FBO_TARGET)
#else
#define FBO_TARGET GL_FRAMEBUFFER
#define FBO_BINDING GL_FRAMEBUFFER_BINDING
#define FBO_COMPLETE GL_FRAMEBUFFER_COMPLETE
static int fbo_procs(void) { return glGenFramebuffers != NULL; }
#define GEN_FBO(n, p) glGenFramebuffers(n, p)
#define BIND_FBO(f) glBindFramebuffer(GL_FRAMEBUFFER, f)
#define TEX_FBO(t) glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0)
#define STATUS_FBO() glCheckFramebufferStatus(GL_FRAMEBUFFER)
#endif

/* Points the engine's drawing at the gauge picture, cleared to nothing;
 * 0 if it cannot be made. */
int vrd_cockpit_gauges_begin(int *w, int *h)
{
	static int failed;
	s_gauges_ok = 0;
	if (failed || !fbo_procs())
		return 0;
	glGetIntegerv(FBO_BINDING, &s_prev_fbo);
	gauge_size(w, h);
	if (s_gfbo && (*w != s_gw || *h != s_gh)) {
		/* the other cockpit: the picture's new size, same framebuffer */
		GLint bound = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		glBindTexture(GL_TEXTURE_2D, s_gtex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *w, *h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
		s_gw = *w; s_gh = *h;
		BIND_FBO(s_gfbo);
		TEX_FBO(s_gtex);
		vrxr_log("VR: cockpit: gauge picture %dx%d", s_gw, s_gh);
	}
	if (!s_gfbo) {
		GLint bound = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
		glGenTextures(1, &s_gtex);
		glBindTexture(GL_TEXTURE_2D, s_gtex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *w, *h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		s_gw = *w; s_gh = *h;
		glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
		GEN_FBO(1, &s_gfbo);
		BIND_FBO(s_gfbo);
		TEX_FBO(s_gtex);
		if (STATUS_FBO() != FBO_COMPLETE) {
			vrxr_log("VR: cockpit: the gauge picture could not be made; the cockpit has no gauges");
			BIND_FBO((GLuint)s_prev_fbo);
			failed = 1;
			return 0;
		}
		vrxr_log("VR: cockpit: gauge picture %dx%d", s_gw, s_gh);
	} else
		BIND_FBO(s_gfbo);
	glViewport(0, 0, s_gw, s_gh);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	return 1;
}

void vrd_cockpit_gauges_end(void)
{
	BIND_FBO((GLuint)s_prev_fbo);
	last_width = last_height = -1;
	s_gauges_ok = 1;
}

/* ---- each frame ----------------------------------------------------------------- */

static float s_light[3] = { 1, 1, 1 };

/* The mine's light at the ship, once a frame (both eyes share it). */
static void sample_light(void)
{
	g3s_lrgb l;
	int i;
	float c[3];
	if (!ConsoleObject)
		return;
	l = compute_object_light(ConsoleObject, NULL);
	c[0] = f2fl(l.r); c[1] = f2fl(l.g); c[2] = f2fl(l.b);
	for (i = 0; i < 3; i++) {
		/* Never black (the frame must stay visible), a little over bright. */
		float v = c[i];
		if (v < 0.0f) v = 0.0f;
		if (v > 1.3f) v = 1.3f;
		s_light[i] = 0.24f + 0.76f * v;
	}
}

static void eye_matrices(const vrxr_eye *e, float swivel)
{
	const float n = 0.02f, f = 20.0f;
	const float x = -e->orient[0], y = -e->orient[1], z = -e->orient[2], w = e->orient[3];
	GLfloat m[16];
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glFrustum(e->tan_l * n, e->tan_r * n, e->tan_d * n, e->tan_u * n, n, f);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	m[0] = 1 - 2 * (y * y + z * z); m[4] = 2 * (x * y - z * w);     m[8] = 2 * (x * z + y * w);      m[12] = 0;
	m[1] = 2 * (x * y + z * w);     m[5] = 1 - 2 * (x * x + z * z); m[9] = 2 * (y * z - x * w);      m[13] = 0;
	m[2] = 2 * (x * z - y * w);     m[6] = 2 * (y * z + x * w);     m[10] = 1 - 2 * (x * x + y * y); m[14] = 0;
	m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
	glLoadMatrixf(m);
	glTranslatef(-e->pos[0], -e->pos[1], -e->pos[2]);
	/* the cockpit turns with the ship under Swivel chair, as the HUD does */
	glRotatef(-swivel * 57.2957795f, 0.0f, 1.0f, 0.0f);
}

/* A rotation as a GL matrix (column major). */
static void quat_matrix(const float q[4], GLfloat m[16])
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	m[0] = 1 - 2 * (y * y + z * z); m[4] = 2 * (x * y - z * w);     m[8] = 2 * (x * z + y * w);      m[12] = 0;
	m[1] = 2 * (x * y + z * w);     m[5] = 1 - 2 * (x * x + z * z); m[9] = 2 * (y * z - x * w);      m[13] = 0;
	m[2] = 2 * (x * z - y * w);     m[6] = 2 * (y * z + x * w);     m[10] = 1 - 2 * (x * x + y * y); m[14] = 0;
	m[3] = 0; m[7] = 0; m[11] = 0; m[15] = 1;
}

/* The Classic cockpit: the game's own cockpit picture with its gauges, its
 * window clear, on a flat screen 1.2 m away.  At 100% it is as wide as the
 * headset's view of one eye, so its frame is round the edges of what the
 * player sees, as it was round the edges of the monitor.  It follows the
 * head (the flat game's screen), or stays ahead of the ship.  Centred it
 * ends a little above the bottom of what can be seen, so it sits 5.5 degrees
 * lower; Classic cockpit height moves it 1.5 degrees a step from there. */
#define CLASSIC_DIST 1.2f
static void draw_classic(const vrxr_eye *e, float swivel)
{
	const float span = 0.5f * (e->tan_r - e->tan_l);
	const float hw = CLASSIC_DIST * span * (0.7f + 0.1f * VrSet.classic_size), hh = hw * 0.75f;
	const float drop = (5.5f + 1.5f * (4 - VrSet.classic_height)) * 0.0174533f;
	const float bot = -CLASSIC_DIST * (float)tan(atan(hh / CLASSIC_DIST) + drop), top = bot + 2.0f * hh;
	const GLfloat v[12] = { -hw, top, -CLASSIC_DIST,  hw, top, -CLASSIC_DIST,
		hw, bot, -CLASSIC_DIST,  -hw, bot, -CLASSIC_DIST };
	static const GLfloat uv[8] = { 0, 1,  1, 1,  1, 0,  0, 0 };	/* the picture is bottom up */
	float q[4], p[3];
	GLfloat m[16];
	if (!s_gauges_ok || !s_gtex)
		return;
	if (VrSet.classic_follow && vrxr_head(q, p)) {
		eye_matrices(e, 0.0f);
		glTranslatef(p[0], p[1], p[2]);
		quat_matrix(q, m);
		glMultMatrixf(m);
	} else
		eye_matrices(e, swivel);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_gtex);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, v);
	glTexCoordPointer(2, GL_FLOAT, 0, uv);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisable(GL_BLEND);
	glDisable(GL_TEXTURE_2D);
	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW); glPopMatrix();
}

int vrd_cockpit_wanted(void)
{
	return VrSet.cockpit && !VrSet.third_person && ConsoleObject && Viewer == ConsoleObject && !Player_is_dead;
}

void vrd_draw_cockpit(void)
{
	vrxr_eye e;
	float swivel;
	GLint vp[4], dfunc = GL_LESS, bound = 0;
	GLboolean depth, tex, blend, cull, alpha, dmask = GL_TRUE;
	int i;
	/* the engine's note of its viewport, kept true for what it draws next
	 * (the crosshair is placed by it) */
	const int lw = last_width, lh = last_height;

	if (!vrd_cockpit_wanted() || !vrd_cockpit_view(&e, &swivel))
		return;		/* the death and exit cameras are outside the ship */
	if (VrSet.cockpit == 2) {
		GLint cvp[4], cbound = 0;
		GLboolean cdepth, ctex, cblend;
		glGetIntegerv(GL_VIEWPORT, cvp);
		cdepth = glIsEnabled(GL_DEPTH_TEST); ctex = glIsEnabled(GL_TEXTURE_2D); cblend = glIsEnabled(GL_BLEND);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &cbound);
		glViewport(0, 0, e.width, e.height);
		draw_classic(&e, swivel);
		glBindTexture(GL_TEXTURE_2D, (GLuint)cbound);
		if (cdepth) glEnable(GL_DEPTH_TEST);
		if (ctex) glEnable(GL_TEXTURE_2D);
		if (cblend) glEnable(GL_BLEND);
		glViewport(cvp[0], cvp[1], cvp[2], cvp[3]);
		last_width = lw; last_height = lh;
		return;
	}
	if (!s_built) {
		make_metal();
		build();
		build_decal();
		vrxr_log("VR: cockpit: %d triangles", s_n / 3);
	}
	if (!vrd_repeat_eye()) {
		/* the mine's light on the metal, once a frame for both eyes */
		sample_light();
		for (i = 0; i < s_n; i++) {
			const float *b = &s_base[4 * i];
			GLfloat *o = &s_lit[4 * i];
			if (s_glow[i]) { o[0] = b[0]; o[1] = b[1]; o[2] = b[2]; }
			else { o[0] = b[0] * s_light[0]; o[1] = b[1] * s_light[1]; o[2] = b[2] * s_light[2]; }
			o[3] = 1.0f;
		}
	}

	glGetIntegerv(GL_VIEWPORT, vp);
	depth = glIsEnabled(GL_DEPTH_TEST); tex = glIsEnabled(GL_TEXTURE_2D); blend = glIsEnabled(GL_BLEND);
	cull = glIsEnabled(GL_CULL_FACE); alpha = glIsEnabled(GL_ALPHA_TEST);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &dmask);
	glGetIntegerv(GL_DEPTH_FUNC, &dfunc);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);

	glViewport(0, 0, e.width, e.height);
	eye_matrices(&e, swivel);
	glClear(GL_DEPTH_BUFFER_BIT);		/* the cockpit is nearer than any of the mine */
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_ALPHA_TEST);
	if (!s_mtex) {
		/* the metal, uploaded once with its mipmaps */
		glGenTextures(1, &s_mtex);
		glBindTexture(GL_TEXTURE_2D, s_mtex);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_N, TEX_N, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_tex);
	}
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, s_mtex);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, s_pos);
	glColorPointer(4, GL_FLOAT, 0, s_lit);
	glTexCoordPointer(2, GL_FLOAT, 0, s_uv);
	glDrawArrays(GL_TRIANGLES, 0, s_n);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_COLOR_ARRAY);
	glDisable(GL_TEXTURE_2D);

	/* The gauges, over the glass: the engine's own picture, lit by itself. */
	if (s_gauges_ok && s_gtex) {
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, s_gtex);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDepthMask(GL_FALSE);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glVertexPointer(3, GL_FLOAT, 0, s_dpos);
		glTexCoordPointer(2, GL_FLOAT, 0, s_duv);
		glDrawArrays(GL_TRIANGLES, 0, s_dn);
		glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glDisable(GL_TEXTURE_2D);
	}
	glDisableClientState(GL_VERTEX_ARRAY);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

	glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(GL_MODELVIEW); glPopMatrix();
	glClear(GL_DEPTH_BUFFER_BIT);
	glDepthMask(dmask);
	glDepthFunc((GLenum)dfunc);
	glBindTexture(GL_TEXTURE_2D, (GLuint)bound);
	if (!depth) glDisable(GL_DEPTH_TEST);
	if (tex) glEnable(GL_TEXTURE_2D);
	if (blend) glEnable(GL_BLEND);
	if (cull) glEnable(GL_CULL_FACE);
	if (alpha) glEnable(GL_ALPHA_TEST);
	glViewport(vp[0], vp[1], vp[2], vp[3]);
	last_width = lw; last_height = lh;
}
#endif	/* VRD_COCKPIT_MESH_ONLY */
