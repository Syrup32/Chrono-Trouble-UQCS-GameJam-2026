#pragma once
// =============================================================================
//  ir_tracker.h  —  Fast IR blob detector for ESP32-CAM
//
//  Expects PIXFORMAT_GRAYSCALE frames.
//  Uses a row-run accumulator + centroid merge instead of flood-fill,
//  which is fast enough to stay comfortably above 30 fps at QVGA.
//
//  When more than 4 blobs pass the area filter, the 4 with the largest
//  area (brightest) are kept and passed to identifyCorners(). Stray
//  reflections and noise blobs tend to be smaller than real IR LEDs,
//  so area is a reliable brightness proxy.
// =============================================================================

#include "esp_camera.h"
#include <math.h>

// ── Tuning constants ─────────────────────────────────────────────────────────
#define IR_THRESHOLD      170   // 0-255 brightness floor. Raise if ambient IR
                                // causes noise; lower if LEDs appear dim.

#define MIN_BLOB_AREA      10   // Minimum pixel-run area — ignores single hot
                                // pixels from sensor noise.

#define MAX_BLOB_AREA     320   // Maximum pixel area — rejects large reflections
                                // or window glare. Tune for your room.

#define MERGE_RADIUS_SQ  (18*18)  // px² — runs closer than this are merged
                                  // into one candidate blob. Increase if the
                                  // same LED splits into 2 blobs at distance.

#define MAX_BLOBS           4   // Final output is always capped at 4.
                                // If more than 4 pass area filtering, the
                                // 4 with the largest area are chosen.

// ── Data types ───────────────────────────────────────────────────────────────
struct Blob {
  float cx;    // centroid X in pixel coords
  float cy;    // centroid Y in pixel coords
  int   area;  // weighted pixel count — proxy for brightness
};

struct BlobResult {
  Blob blobs[MAX_BLOBS];
  int  count;       // number of valid blobs returned (0–4)
  int  rawCount;    // candidates before area filtering (diagnostic)
  int  filteredCount; // blobs after area filter, before brightest-4 cull
};

// ── Implementation ───────────────────────────────────────────────────────────

/**
 * findBlobs()
 *
 * Scans every row of the grayscale frame buffer for contiguous bright runs,
 * accumulates weighted centroids, merges nearby candidates, then returns
 * the 4 brightest blobs that pass the area filter.
 *
 * Call with a PIXFORMAT_GRAYSCALE camera_fb_t.
 * Returns immediately; does NOT release the framebuffer (caller's job).
 */
static BlobResult findBlobs(camera_fb_t *fb) {
  BlobResult result;
  result.count         = 0;
  result.rawCount      = 0;
  result.filteredCount = 0;

  if (!fb || fb->format != PIXFORMAT_GRAYSCALE) {
    return result;
  }

  const int W = (int)fb->width;
  const int H = (int)fb->height;
  const uint8_t *pixels = fb->buf;

  struct Candidate {
    float sx;
    float sy;
    int   n;
    bool  alive;
  };

  // Pool is larger than MAX_BLOBS to allow extra candidates before culling.
  // 24 slots = enough for stray noise + 4 real LEDs with room to spare.
  static Candidate pool[24];
  int nPool = 0;

  // ── Row scan ──────────────────────────────────────────────────────────────
  for (int y = 0; y < H; y++) {
    const uint8_t *row = pixels + y * W;
    int runStart = -1;

    for (int x = 0; x <= W; x++) {
      bool bright = (x < W) && (row[x] >= IR_THRESHOLD);

      if (bright && runStart < 0) {
        runStart = x;
      } else if (!bright && runStart >= 0) {
        int runLen = x - runStart;
        float rcx = runStart + runLen * 0.5f;
        float rcy = (float)y;

        bool merged = false;
        for (int i = 0; i < nPool; i++) {
          if (!pool[i].alive) continue;
          float ecx = pool[i].sx / pool[i].n;
          float ecy = pool[i].sy / pool[i].n;
          float dx  = rcx - ecx;
          float dy  = rcy - ecy;
          if ((dx * dx + dy * dy) < MERGE_RADIUS_SQ) {
            pool[i].sx += rcx * runLen;
            pool[i].sy += rcy * runLen;
            pool[i].n  += runLen;
            merged = true;
            break;
          }
        }

        if (!merged && nPool < 24) {
          pool[nPool++] = { rcx * runLen, rcy * runLen, runLen, true };
        }

        runStart = -1;
      }
    }
  }

  result.rawCount = nPool;

  // ── Area filter — collect all blobs that pass min/max area ───────────────
  // Temporary store — up to 24 candidates can pass before we cull to 4
  struct ValidBlob { float cx, cy; int area; };
  static ValidBlob valid[24];
  int nValid = 0;

  for (int i = 0; i < nPool; i++) {
    if (!pool[i].alive) continue;
    int area = pool[i].n;
    if (area < MIN_BLOB_AREA || area > MAX_BLOB_AREA) continue;
    if (nValid < 24) {
      valid[nValid++] = {
        pool[i].sx / area,
        pool[i].sy / area,
        area
      };
    }
  }

  result.filteredCount = nValid;

  // ── Brightest-4 selection ─────────────────────────────────────────────────
  // If ≤4 blobs passed the filter, use them all.
  // If >4, pick the 4 with the largest area using a simple selection sort.
  // Selection sort is fine here — we're sorting at most ~20 items, once per frame.
  int needed = nValid < MAX_BLOBS ? nValid : MAX_BLOBS;

  for (int pick = 0; pick < needed; pick++) {
    // Find the largest area among remaining unpicked blobs
    int bestIdx  = -1;
    int bestArea = -1;
    for (int i = pick; i < nValid; i++) {
      if (valid[i].area > bestArea) {
        bestArea = valid[i].area;
        bestIdx  = i;
      }
    }
    if (bestIdx < 0) break;

    // Swap chosen blob into position [pick]
    ValidBlob tmp    = valid[pick];
    valid[pick]      = valid[bestIdx];
    valid[bestIdx]   = tmp;

    result.blobs[result.count++] = {
      valid[pick].cx,
      valid[pick].cy,
      valid[pick].area
    };
  }

  return result;
}