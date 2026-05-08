#pragma once
// =============================================================================
//  ir_position.h  —  Crosshair position solver for IR gun project
//
//  Takes a BlobResult from ir_tracker.h, identifies the 4 corner LEDs,
//  and returns the normalised {0..1, 0..1} aim position using an iterative
//  inverse bilinear interpolation on the detected quad.
//
//  The "aim point" is the camera optical centre — i.e. where the barrel
//  points. Unity receives these normalised coords and maps them to screen.
// =============================================================================

#include "ir_tracker.h"
#include <math.h>

struct Vec2 { float x; float y; };

// ── Corner identification ────────────────────────────────────────────────────

/**
 * identifyCorners()
 *
 * Finds the four extreme blobs that form the monitor corners using the
 * classic min/max of (cx+cy) and (cx-cy) diagonal projections.
 *
 *   TL = smallest (cx + cy)   →  nearest to image origin
 *   BR = largest  (cx + cy)   →  farthest from image origin
 *   TR = largest  (cx - cy)   →  rightmost, highest
 *   BL = smallest (cx - cy)   →  leftmost,  lowest
 *
 * Returns true when ≥ 4 blobs are present and all four corners are distinct.
 * When you have 8 blobs (4 corners + 4 midpoints) the function still works —
 * the midpoints will have intermediate sum/diff values and won't be selected.
 */
static bool identifyCorners(const BlobResult &blobs,
                            Vec2 &tl, Vec2 &tr, Vec2 &bl, Vec2 &br) {
  if (blobs.count != 4) return false;  // must be exactly 4 — no more, no less

  int idxTL = -1, idxTR = -1, idxBL = -1, idxBR = -1;
  float minSum =  1e9f, maxSum = -1e9f;
  float minDiff =  1e9f, maxDiff = -1e9f;

  for (int i = 0; i < blobs.count; i++) {
    float s = blobs.blobs[i].cx + blobs.blobs[i].cy;
    float d = blobs.blobs[i].cx - blobs.blobs[i].cy;

    if (s < minSum)  { minSum  = s; idxTL = i; }
    if (s > maxSum)  { maxSum  = s; idxBR = i; }
    if (d < minDiff) { minDiff = d; idxBL = i; }
    if (d > maxDiff) { maxDiff = d; idxTR = i; }
  }

  // All four must be found and distinct (different blob indices)
  if (idxTL < 0 || idxTR < 0 || idxBL < 0 || idxBR < 0) return false;
  if (idxTL == idxTR || idxTL == idxBL || idxTL == idxBR) return false;
  if (idxTR == idxBL || idxTR == idxBR || idxBL == idxBR) return false;

  tl = { blobs.blobs[idxTL].cx, blobs.blobs[idxTL].cy };
  tr = { blobs.blobs[idxTR].cx, blobs.blobs[idxTR].cy };
  bl = { blobs.blobs[idxBL].cx, blobs.blobs[idxBL].cy };
  br = { blobs.blobs[idxBR].cx, blobs.blobs[idxBR].cy };

  return true;
}

// ── Inverse bilinear interpolation ──────────────────────────────────────────

/**
 * inverseQuad()
 *
 * Given a point p inside (or near) the quad defined by tl/tr/bl/br,
 * returns the normalised (u, v) in [0, 1] x [0, 1] where:
 *   u = 0 → left edge,   u = 1 → right edge
 *   v = 0 → top edge,    v = 1 → bottom edge
 *
 * Uses Newton–Raphson iteration on the bilinear map. Converges in 4-6
 * steps for well-conditioned quads (i.e. a monitor viewed head-on ±45°).
 *
 * Clamps result to [0,1] so off-screen aim still gives valid edge coords.
 *
 * NOTE: pixel-space on the OV2640 has y increasing downward, so v=0 is the
 * top of the monitor in screen-space — Unity should flip Y if needed.
 */
static Vec2 inverseQuad(Vec2 p,
                        Vec2 tl, Vec2 tr, Vec2 bl, Vec2 br) {
  float u = 0.5f, v = 0.5f;

  for (int iter = 0; iter < 8; iter++) {
    float iu = 1.0f - u;
    float iv = 1.0f - v;

    // Bilinear position at current (u,v)
    float bx = iu * iv * tl.x  +  u * iv * tr.x
             + iu *  v * bl.x  +  u *  v * br.x;
    float by = iu * iv * tl.y  +  u * iv * tr.y
             + iu *  v * bl.y  +  u *  v * br.y;

    // Partial derivatives (Jacobian)
    float dbu_x = -iv * tl.x  +  iv * tr.x  -  v * bl.x  +  v * br.x;
    float dbu_y = -iv * tl.y  +  iv * tr.y  -  v * bl.y  +  v * br.y;
    float dbv_x = -iu * tl.x  -  u * tr.x   + iu * bl.x  +  u * br.x;
    float dbv_y = -iu * tl.y  -  u * tr.y   + iu * bl.y  +  u * br.y;

    float ex = p.x - bx;
    float ey = p.y - by;

    float det = dbu_x * dbv_y - dbu_y * dbv_x;
    if (fabsf(det) < 1e-6f) break;   // degenerate quad — stop

    u += ( dbv_y * ex - dbv_x * ey) / det;
    v += (-dbu_y * ex + dbu_x * ey) / det;
  }

  // Clamp to [0, 1]
  u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
  v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);

  return { u, v };
}

// ── Convenience: full solve in one call ─────────────────────────────────────

struct TrackingResult {
  bool  valid;    // false if fewer than 4 blobs visible
  Vec2  norm;     // normalised aim {x: 0..1, y: 0..1}
  Vec2  tl, tr, bl, br;   // corner positions this frame (pixels)
  int   blobCount;
};

/**
 * solveAim()
 *
 * Full pipeline: blob list → corners → normalised aim point.
 * The aim point is the camera optical centre (frame centre).
 * Pass the frame width and height so we can compute the centre.
 */
static TrackingResult solveAim(const BlobResult &blobs,
                               int frameW, int frameH) {
  TrackingResult res;
  res.blobCount = blobs.count;
  res.valid = false;
  res.norm  = { 0.5f, 0.5f };

  Vec2 tl, tr, bl, br;
  if (!identifyCorners(blobs, tl, tr, bl, br)) {
    return res;
  }

  res.tl = tl;  res.tr = tr;
  res.bl = bl;  res.br = br;

  Vec2 aim = { frameW * 0.5f, frameH * 0.5f };
  res.norm  = inverseQuad(aim, tl, tr, bl, br);
  res.valid = true;

  return res;
}
