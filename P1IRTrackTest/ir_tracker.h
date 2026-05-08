#pragma once
// =============================================================================
//  ir_tracker.h  —  Fast IR blob detector for ESP32-CAM
//
//  Expects PIXFORMAT_GRAYSCALE frames.
//  Uses a row-run accumulator + centroid merge instead of flood-fill,
//  which is fast enough to stay comfortably above 30 fps at QVGA.
// =============================================================================

#include "esp_camera.h"
#include <math.h>

// ── Tuning constants ─────────────────────────────────────────────────────────
#define IR_THRESHOLD      80   // 0-255 brightness floor. Raise if ambient IR
                                // causes noise; lower if LEDs appear dim.
                                // Start at 220 and work down in a dark room.

#define MIN_BLOB_AREA       3   // Minimum pixel-run area — ignores single hot
                                // pixels from sensor noise.

#define MAX_BLOB_AREA     300   // Maximum pixel area — rejects large reflections
                                // or window glare. Tune for your room.

#define MERGE_RADIUS_SQ  (18*18)  // px² — runs closer than this are merged
                                  // into one candidate blob. Increase if the
                                  // same LED splits into 2 blobs at distance.

#define MAX_BLOBS           4   // Exactly 4 corner LEDs — any frame with more
                                // or fewer blobs is rejected by identifyCorners.

// ── Data types ───────────────────────────────────────────────────────────────
struct Blob {
  float cx;    // centroid X in pixel coords
  float cy;    // centroid Y in pixel coords
  int   area;  // weighted pixel count (useful for sorting / confidence)
};

struct BlobResult {
  Blob blobs[MAX_BLOBS];
  int  count;       // number of valid blobs found this frame
  int  rawCount;    // blobs before area filtering (diagnostic)
};

// ── Implementation ───────────────────────────────────────────────────────────

/**
 * findBlobs()
 *
 * Scans every row of the grayscale frame buffer for contiguous bright runs,
 * accumulates weighted centroids, then merges nearby candidates.
 *
 * Call with a PIXFORMAT_GRAYSCALE camera_fb_t.
 * Returns immediately; does NOT release the framebuffer (caller's job).
 */
static BlobResult findBlobs(camera_fb_t *fb) {
  BlobResult result;
  result.count    = 0;
  result.rawCount = 0;

  if (!fb || fb->format != PIXFORMAT_GRAYSCALE) {
    return result;
  }

  const int W = (int)fb->width;
  const int H = (int)fb->height;
  const uint8_t *pixels = fb->buf;

  // Working pool for candidates.
  // Each entry accumulates (sum_x*weight, sum_y*weight, total_weight).
  // Weight = run length so centroid is properly area-weighted.
  struct Candidate {
    float sx;    // sum of (cx * runLen)
    float sy;    // sum of (cy * runLen)
    int   n;     // total pixel count
    bool  alive;
  };

  // Static to avoid stack pressure on ESP32 — this function is called
  // from a FreeRTOS task, stack is limited.
  static Candidate pool[MAX_BLOBS * 6];
  int nPool = 0;

  // ── Row scan ────────────────────────────────────────────────────────────
  for (int y = 0; y < H; y++) {
    const uint8_t *row = pixels + y * W;
    int runStart = -1;

    for (int x = 0; x <= W; x++) {
      bool bright = (x < W) && (row[x] >= IR_THRESHOLD);

      if (bright && runStart < 0) {
        runStart = x;           // run begins
      } else if (!bright && runStart >= 0) {
        // run ends at x-1
        int runLen = x - runStart;
        float rcx = runStart + runLen * 0.5f;
        float rcy = (float)y;

        // Try to merge into an existing nearby candidate
        bool merged = false;
        for (int i = 0; i < nPool; i++) {
          if (!pool[i].alive) continue;
          float ecx = pool[i].sx / pool[i].n;
          float ecy = pool[i].sy / pool[i].n;
          float dx = rcx - ecx;
          float dy = rcy - ecy;
          if ((dx * dx + dy * dy) < MERGE_RADIUS_SQ) {
            pool[i].sx += rcx * runLen;
            pool[i].sy += rcy * runLen;
            pool[i].n  += runLen;
            merged = true;
            break;
          }
        }

        if (!merged) {
          if (nPool < MAX_BLOBS * 6) {
            pool[nPool++] = { rcx * runLen, rcy * runLen, runLen, true };
          }
          // If pool is full we silently drop — means >96 raw blobs,
          // which usually means threshold is too low.
        }

        runStart = -1;
      }
    }
  }

  // ── Collect valid blobs ──────────────────────────────────────────────────
  result.rawCount = nPool;

  for (int i = 0; i < nPool && result.count < MAX_BLOBS; i++) {
    if (!pool[i].alive) continue;
    int area = pool[i].n;
    if (area < MIN_BLOB_AREA || area > MAX_BLOB_AREA) continue;

    result.blobs[result.count++] = {
      pool[i].sx / area,
      pool[i].sy / area,
      area
    };
  }

  return result;
}
