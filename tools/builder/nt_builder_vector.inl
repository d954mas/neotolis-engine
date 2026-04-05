// NFP Vector Packing Implementation
// Minkowski-sum based No-Fit Polygon packer for atlas sprite placement.
// Included from nt_builder_atlas.c (has access to Point2D, polygon_inflate, etc.)

static inline int64_t vpack_cross(int64_t ax, int64_t ay, int64_t bx, int64_t by, int64_t cx, int64_t cy) { return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax); }

/* Negate polygon vertices (no reversal needed).
 * 2D negation = 180deg rotation, preserves winding (CCW stays CCW). */
static void vpack_negate(const Point2D *in, uint32_t count, Point2D *out) {
    for (uint32_t i = 0; i < count; i++) {
        out[i].x = -in[i].x;
        out[i].y = -in[i].y;
    }
}

/* Minkowski sum of two convex CCW polygons.
 * Classic merge-by-edge-angle algorithm. Both A and B must be CCW.
 * Result is CCW convex polygon with at most nA + nB vertices. */
static uint32_t vpack_minkowski(const Point2D *A, uint32_t nA, const Point2D *B, uint32_t nB, Point2D *out) {
    uint32_t i = 0, j = 0;
    for (uint32_t k = 1; k < nA; k++) {
        if (A[k].y < A[i].y || (A[k].y == A[i].y && A[k].x < A[i].x))
            i = k;
    }
    for (uint32_t k = 1; k < nB; k++) {
        if (B[k].y < B[j].y || (B[k].y == B[j].y && B[k].x < B[j].x))
            j = k;
    }

    uint32_t start_i = i, start_j = j;
    uint32_t count = 0;
    do {
        out[count].x = A[i].x + B[j].x;
        out[count].y = A[i].y + B[j].y;
        count++;

        Point2D edgeA = {A[(i + 1) % nA].x - A[i].x, A[(i + 1) % nA].y - A[i].y};
        Point2D edgeB = {B[(j + 1) % nB].x - B[j].x, B[(j + 1) % nB].y - B[j].y};

        int64_t cross = (int64_t)edgeA.x * edgeB.y - (int64_t)edgeA.y * edgeB.x;
        if (cross > 0) {
            i = (i + 1) % nA;
        } else if (cross < 0) {
            j = (j + 1) % nB;
        } else {
            i = (i + 1) % nA;
            j = (j + 1) % nB;
        }
    } while ((i != start_i || j != start_j) && count < nA + nB);
    return count;
}

static bool vpack_intersect(Point2D p1, Point2D p2, Point2D p3, Point2D p4, float *ox, float *oy) {
    int64_t s1_x = p2.x - p1.x;
    int64_t s1_y = p2.y - p1.y;
    int64_t s2_x = p4.x - p3.x;
    int64_t s2_y = p4.y - p3.y;

    int64_t d = -s2_x * s1_y + s1_x * s2_y;
    if (d == 0)
        return false;

    int64_t s_num = -s1_y * (p1.x - p3.x) + s1_x * (p1.y - p3.y);
    int64_t t_num = s2_x * (p1.y - p3.y) - s2_y * (p1.x - p3.x);

    if (d > 0) {
        if (s_num < 0 || s_num > d || t_num < 0 || t_num > d)
            return false;
    } else {
        if (s_num > 0 || s_num < d || t_num > 0 || t_num < d)
            return false;
    }

    *ox = (float)p1.x + (float)(t_num * s1_x) / (float)d;
    *oy = (float)p1.y + (float)(t_num * s1_y) / (float)d;
    return true;
}

static bool vpack_intersect_axis(Point2D p1, Point2D p2, bool is_x_axis, float margin, float *out_val) {
    if (is_x_axis) {
        if (p1.x == p2.x) {
            return false;
        }
        if ((p1.x < margin && p2.x >= margin) || (p1.x >= margin && p2.x < margin)) {
            float t = (margin - (float)p1.x) / (float)(p2.x - p1.x);
            *out_val = (float)p1.y + t * (float)(p2.y - p1.y);
            return true;
        }
    } else {
        if (p1.y == p2.y) {
            return false;
        }
        if ((p1.y < margin && p2.y >= margin) || (p1.y >= margin && p2.y < margin)) {
            float t = (margin - (float)p1.y) / (float)(p2.y - p1.y);
            *out_val = (float)p1.x + t * (float)(p2.x - p1.x);
            return true;
        }
    }
    return false;
}

/* Strictly-inside test for CCW convex polygon (all cross products > 0). */
static bool vpack_point_in_poly(int32_t px, int32_t py, const Point2D *poly, uint32_t count) {
    if (count < 3)
        return false;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t j = (i + 1 == count) ? 0 : i + 1;
        if (vpack_cross(poly[i].x, poly[i].y, poly[j].x, poly[j].y, px, py) <= 0) {
            return false;
        }
    }
    return true;
}

typedef struct {
    Point2D verts[64];
    uint32_t count;
    int32_t min_x, min_y, max_x, max_y;
} VPackNFP;

typedef struct {
    int32_t x, y;
    uint64_t dist;
} VPackCand;

static int vpack_cand_cmp(const void *a, const void *b) {
    const VPackCand *ca = (const VPackCand *)a;
    const VPackCand *cb = (const VPackCand *)b;
    if (ca->dist < cb->dist)
        return -1;
    if (ca->dist > cb->dist)
        return 1;
    return 0;
}

/* Bounds for early candidate rejection (set per-sprite before candidate generation) */
typedef struct {
    int32_t min_x, min_y, max_x, max_y;
} VPackBounds;

static inline void vpack_add_cand(VPackCand **cands, uint32_t *c_count, uint32_t *c_cap, int32_t x, int32_t y, const VPackBounds *b) {
    if (x < b->min_x || y < b->min_y || x > b->max_x || y > b->max_y)
        return;
    if (*c_count >= *c_cap) {
        *c_cap = (*c_cap == 0) ? 1024 : (*c_cap * 2);
        *cands = (VPackCand *)realloc(*cands, *c_cap * sizeof(VPackCand));
    }
    (*cands)[*c_count].x = x;
    (*cands)[*c_count].y = y;
    (*cands)[*c_count].dist = 0; /* scored later with page context */
    (*c_count)++;
}

static inline void vpack_add_float_cand(VPackCand **cands, uint32_t *c_count, uint32_t *c_cap, float fx, float fy, const VPackBounds *b) {
    int32_t ix = (int32_t)floorf(fx);
    int32_t iy = (int32_t)floorf(fy);
    vpack_add_cand(cands, c_count, c_cap, ix, iy, b);
    vpack_add_cand(cands, c_count, c_cap, ix + 1, iy, b);
    vpack_add_cand(cands, c_count, c_cap, ix, iy + 1, b);
    vpack_add_cand(cands, c_count, c_cap, ix + 1, iy + 1, b);
}

static inline bool vpack_rect_overlap(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1, int32_t bx0, int32_t by0, int32_t bx1, int32_t by1) {
    return !(ax1 < bx0 || ax0 > bx1 || ay1 < by0 || ay0 > by1);
}

typedef struct {
    Point2D poly[32];
    uint32_t count;
    int32_t x, y;
    int32_t aabb_min_x, aabb_min_y, aabb_max_x, aabb_max_y; /* polygon AABB (relative, not offset by x,y) */
} VPackPlaced;

static void vpack_calc_aabb(const Point2D *poly, uint32_t count, int32_t *min_x, int32_t *min_y, int32_t *max_x, int32_t *max_y) {
    if (count == 0)
        return;
    *min_x = *max_x = poly[0].x;
    *min_y = *max_y = poly[0].y;
    for (uint32_t i = 1; i < count; i++) {
        if (poly[i].x < *min_x)
            *min_x = poly[i].x;
        if (poly[i].x > *max_x)
            *max_x = poly[i].x;
        if (poly[i].y < *min_y)
            *min_y = poly[i].y;
        if (poly[i].y > *max_y)
            *max_y = poly[i].y;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static uint32_t vector_pack(const uint32_t *trim_w, const uint32_t *trim_h, Point2D **hull_verts, const uint32_t *hull_counts, uint32_t sprite_count, const nt_atlas_opts_t *opts,
                            AtlasPlacement *out_placements, uint32_t *out_page_count, uint32_t *out_page_w, uint32_t *out_page_h, PackStats *stats) {
    uint32_t extrude = opts->extrude;
    uint32_t padding = opts->padding;
    uint32_t margin = opts->margin;
    uint32_t max_size = opts->max_size;

    // #region Initialize stats
    stats->or_count = 0;
    stats->test_count = 0;
    stats->yskip_count = 0;
    // #endregion

    // #region Inflate polygons by extrude+padding/2 (same spacing as tile_pack)
    float dilate = (float)extrude + ((float)padding * 0.5F);
    Point2D **inf_polys = (Point2D **)malloc(sprite_count * sizeof(Point2D *));
    uint32_t *inf_counts = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(inf_polys && inf_counts && "vector_pack: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        inf_polys[i] = (Point2D *)malloc(hull_counts[i] * sizeof(Point2D));
        NT_BUILD_ASSERT(inf_polys[i] && "vector_pack: alloc failed");
        if (dilate > 0.0F) {
            inf_counts[i] = polygon_inflate(hull_verts[i], hull_counts[i], dilate, inf_polys[i]);
        } else {
            memcpy(inf_polys[i], hull_verts[i], hull_counts[i] * sizeof(Point2D));
            inf_counts[i] = hull_counts[i];
        }
    }
    // #endregion

    // #region Sort by area descending
    AreaSortEntry *sorted = (AreaSortEntry *)malloc(sprite_count * sizeof(AreaSortEntry));
    NT_BUILD_ASSERT(sorted && "vector_pack: alloc failed");
    for (uint32_t i = 0; i < sprite_count; i++) {
        sorted[i].index = i;
        sorted[i].area = trim_w[i] * trim_h[i];
    }
    qsort(sorted, sprite_count, sizeof(AreaSortEntry), area_sort_cmp);
    // #endregion

    VPackPlaced *placed = (VPackPlaced *)malloc(sprite_count * sizeof(VPackPlaced));
    VPackNFP *nfps = (VPackNFP *)malloc(sprite_count * sizeof(VPackNFP));
    uint32_t cand_cap = 1024;
    VPackCand *cands = (VPackCand *)malloc(cand_cap * sizeof(VPackCand));
    NT_BUILD_ASSERT(placed && nfps && cands && "vector_pack: alloc failed");

    uint32_t current_page = 0;
    uint32_t placed_on_page = 0;
    uint32_t pages_used_w[16] = {0};
    uint32_t pages_used_h[16] = {0};

    for (uint32_t s = 0; s < sprite_count; s++) {
        uint32_t idx = sorted[s].index;

        Point2D sprite_neg[32];
        vpack_negate(inf_polys[idx], inf_counts[idx], sprite_neg);

        // #region Compute inflated polygon AABB (once per sprite, reused in all checks)
        int32_t poly_min_x, poly_min_y, poly_max_x, poly_max_y;
        vpack_calc_aabb(inf_polys[idx], inf_counts[idx], &poly_min_x, &poly_min_y, &poly_max_x, &poly_max_y);

        int32_t min_edge = (int32_t)margin > (int32_t)extrude ? (int32_t)margin : (int32_t)extrude;
        int32_t min_cand_x = min_edge - poly_min_x;
        if (min_cand_x < min_edge)
            min_cand_x = min_edge;
        int32_t min_cand_y = min_edge - poly_min_y;
        if (min_cand_y < min_edge)
            min_cand_y = min_edge;

        int32_t max_cand_x = (int32_t)max_size - (int32_t)margin - poly_max_x - 1;
        int32_t max_cand_y = (int32_t)max_size - (int32_t)margin - poly_max_y - 1;
        // #endregion

    try_place:;
        uint32_t cand_count = 0;
        VPackBounds bounds = {min_cand_x, min_cand_y, max_cand_x, max_cand_y};
        vpack_add_cand(&cands, &cand_count, &cand_cap, min_cand_x, min_cand_y, &bounds);

        // #region Build NFPs against placed sprites (with AABB pre-filter)
        uint32_t nfp_count = 0;
        for (uint32_t i = 0; i < placed_on_page; i++) {
            /* AABB pre-filter: estimate NFP bounding box from placed + sprite AABBs.
             * If estimated NFP doesn't overlap feasible region, the placed sprite
             * is too far away -- no candidate in [min_cand, max_cand] can collide. */
            int32_t est_min_x = placed[i].x + placed[i].aabb_min_x - poly_max_x;
            int32_t est_max_x = placed[i].x + placed[i].aabb_max_x - poly_min_x;
            int32_t est_min_y = placed[i].y + placed[i].aabb_min_y - poly_max_y;
            int32_t est_max_y = placed[i].y + placed[i].aabb_max_y - poly_min_y;
            if (est_max_x < min_cand_x || est_min_x > max_cand_x || est_max_y < min_cand_y || est_min_y > max_cand_y) {
                stats->yskip_count++;
                continue;
            }

            VPackNFP *nfp = &nfps[nfp_count];
            nfp->count = vpack_minkowski(placed[i].poly, placed[i].count, sprite_neg, inf_counts[idx], nfp->verts);
            stats->or_count++;

            for (uint32_t v = 0; v < nfp->count; v++) {
                nfp->verts[v].x += placed[i].x;
                nfp->verts[v].y += placed[i].y;
            }
            vpack_calc_aabb(nfp->verts, nfp->count, &nfp->min_x, &nfp->min_y, &nfp->max_x, &nfp->max_y);

            for (uint32_t v = 0; v < nfp->count; v++) {
                vpack_add_cand(&cands, &cand_count, &cand_cap, nfp->verts[v].x, nfp->verts[v].y, &bounds);
            }

            for (uint32_t e = 0; e < nfp->count; e++) {
                uint32_t en = (e + 1 == nfp->count) ? 0 : e + 1;
                float out_val;
                if (vpack_intersect_axis(nfp->verts[e], nfp->verts[en], true, (float)min_cand_x, &out_val)) {
                    vpack_add_float_cand(&cands, &cand_count, &cand_cap, (float)min_cand_x, out_val, &bounds);
                }
                if (vpack_intersect_axis(nfp->verts[e], nfp->verts[en], false, (float)min_cand_y, &out_val)) {
                    vpack_add_float_cand(&cands, &cand_count, &cand_cap, out_val, (float)min_cand_y, &bounds);
                }
            }
            nfp_count++;
        }
        // #endregion

        /* NFP-NFP edge intersections skipped: O(nfp^2 * edges^2) cost for marginal
         * quality gain. NFP vertices + axis intersections provide sufficient candidates. */

        // #region Score candidates by resulting POT page area, then sort
        {
            uint32_t cur_w = pages_used_w[current_page];
            uint32_t cur_h = pages_used_h[current_page];
            for (uint32_t c = 0; c < cand_count; c++) {
                uint32_t nw = (uint32_t)cands[c].x + poly_max_x;
                uint32_t nh = (uint32_t)cands[c].y + poly_max_y;
                if (nw < cur_w)
                    nw = cur_w;
                if (nh < cur_h)
                    nh = cur_h;
                nw += margin;
                nh += margin;
                if (opts->power_of_two) {
                    uint32_t pw = 1;
                    while (pw < nw)
                        pw <<= 1;
                    uint32_t ph = 1;
                    while (ph < nh)
                        ph <<= 1;
                    nw = pw;
                    nh = ph;
                }
                /* Primary: POT area (24 bits). Secondary: Manhattan distance (16 bits). */
                uint64_t area = (uint64_t)nw * nh;
                uint64_t manh = (uint64_t)(cands[c].x + cands[c].y);
                cands[c].dist = (area << 16) | (manh & 0xFFFF);
            }
        }
        qsort(cands, cand_count, sizeof(VPackCand), vpack_cand_cmp);

        bool found = false;
        int32_t best_x = 0;
        int32_t best_y = 0;
        for (uint32_t c = 0; c < cand_count; c++) {
            int32_t cx = cands[c].x;
            int32_t cy = cands[c].y;

            /* Deduplicate: skip identical (x,y) — duplicates are adjacent after sort-by-distance */
            if (c > 0 && cx == cands[c - 1].x && cy == cands[c - 1].y)
                continue;

            /* Bounds check (precomputed poly AABB — no per-candidate recalc) */
            if (cx < min_cand_x || cy < min_cand_y)
                continue;
            if (cx + poly_min_x < 0 || cy + poly_min_y < 0)
                continue;
            if (cx < (int32_t)extrude || cy < (int32_t)extrude)
                continue;
            if (cx > max_cand_x || cy > max_cand_y)
                continue;

            /* Collision: candidate must be outside ALL NFPs */
            bool safe = true;
            for (uint32_t i = 0; i < nfp_count; i++) {
                if (cx >= nfps[i].min_x && cx <= nfps[i].max_x && cy >= nfps[i].min_y && cy <= nfps[i].max_y) {
                    stats->test_count++;
                    if (vpack_point_in_poly(cx, cy, nfps[i].verts, nfps[i].count)) {
                        safe = false;
                        break;
                    }
                }
            }
            if (safe) {
                best_x = cx;
                best_y = cy;
                found = true;
                break;
            }
        }
        // #endregion

        // #region Page overflow
        if (!found) {
            current_page++;
            placed_on_page = 0;
            if (current_page >= 16) {
                NT_LOG_ERROR("NFP packing failed: Out of pages (>= 16) for sprite %u!", s);
                for (uint32_t r = s; r < sprite_count; r++) {
                    uint32_t ridx = sorted[r].index;
                    out_placements[ridx].sprite_index = ridx;
                    out_placements[ridx].page = 0;
                    out_placements[ridx].x = opts->margin;
                    out_placements[ridx].y = opts->margin;
                    out_placements[ridx].rotation = 0;
                }
                break;
            }
            goto try_place;
        }
        // #endregion

        // #region Save placement
        placed[placed_on_page].count = inf_counts[idx];
        placed[placed_on_page].x = best_x;
        placed[placed_on_page].y = best_y;
        for (uint32_t v = 0; v < inf_counts[idx]; v++) {
            placed[placed_on_page].poly[v] = inf_polys[idx][v];
        }
        vpack_calc_aabb(inf_polys[idx], inf_counts[idx], &placed[placed_on_page].aabb_min_x, &placed[placed_on_page].aabb_min_y, &placed[placed_on_page].aabb_max_x,
                        &placed[placed_on_page].aabb_max_y);
        placed_on_page++;

        out_placements[idx].sprite_index = idx;
        out_placements[idx].page = current_page;
        /* pipeline_serialize does: inner_x = pl->x + extrude, atlas_px = inner_x + lx.
         * For atlas_px to equal (best_x + lx), we need pl->x = best_x - extrude. */
        NT_BUILD_ASSERT(best_x >= (int32_t)extrude && best_y >= (int32_t)extrude && "vector_pack: placement too close to edge for extrude");
        out_placements[idx].x = (uint32_t)(best_x - (int32_t)extrude);
        out_placements[idx].y = (uint32_t)(best_y - (int32_t)extrude);
        out_placements[idx].rotation = 0;

        if (best_x + poly_max_x > (int32_t)pages_used_w[current_page])
            pages_used_w[current_page] = best_x + poly_max_x;
        if (best_y + poly_max_y > (int32_t)pages_used_h[current_page])
            pages_used_h[current_page] = best_y + poly_max_y;
        // #endregion
    }

    // #region POT expansion
    *out_page_count = current_page + 1;
    for (uint32_t i = 0; i <= current_page; i++) {
        uint32_t final_w = pages_used_w[i] + margin;
        uint32_t final_h = pages_used_h[i] + margin;
        if (opts->power_of_two) {
            uint32_t pot_w = 1;
            while (pot_w < final_w)
                pot_w <<= 1;
            uint32_t pot_h = 1;
            while (pot_h < final_h)
                pot_h <<= 1;
            final_w = pot_w;
            final_h = pot_h;
        }
        out_page_w[i] = final_w;
        out_page_h[i] = final_h;
    }
    // #endregion

    // #region Cleanup
    for (uint32_t i = 0; i < sprite_count; i++)
        free(inf_polys[i]);
    free(inf_polys);
    free(inf_counts);
    free(sorted);
    free(placed);
    free(nfps);
    free(cands);
    // #endregion

    return sprite_count;
}
