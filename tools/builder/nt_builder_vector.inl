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

typedef struct {
    VPackPlaced *placed;
    uint32_t count;
    uint32_t used_w;
    uint32_t used_h;
} VPackPage;

#define VPACK_GRID_CELL 64
#define VPACK_GRID_DIM ((4096 / VPACK_GRID_CELL) + 1)
#define VPACK_GRID_WORDS 24

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

static uint64_t vpack_score_candidate(int32_t cx, int32_t cy, int32_t poly_max_x, int32_t poly_max_y, uint32_t cur_w, uint32_t cur_h, uint32_t margin, bool power_of_two) {
    uint32_t nw = (uint32_t)cx + (uint32_t)poly_max_x;
    uint32_t nh = (uint32_t)cy + (uint32_t)poly_max_y;
    if (nw < cur_w)
        nw = cur_w;
    if (nh < cur_h)
        nh = cur_h;
    nw += margin;
    nh += margin;
    if (power_of_two) {
        uint32_t pw = 1;
        while (pw < nw)
            pw <<= 1;
        uint32_t ph = 1;
        while (ph < nh)
            ph <<= 1;
        nw = pw;
        nh = ph;
    }
    return ((uint64_t)nw * nh << 16) | ((uint64_t)(cx + cy) & 0xFFFF);
}

static uint64_t vpack_page_lower_bound(const int32_t orient_aabb[8][4], const int32_t orient_min_cand[8][2], uint32_t orient_count, uint32_t page_used_w, uint32_t page_used_h, uint32_t margin,
                                       bool power_of_two) {
    uint64_t best = UINT64_MAX;
    for (uint32_t ori = 0; ori < orient_count; ori++) {
        uint64_t score = vpack_score_candidate(orient_min_cand[ori][0], orient_min_cand[ori][1], orient_aabb[ori][2], orient_aabb[ori][3], page_used_w, page_used_h, margin, power_of_two);
        if (score < best)
            best = score;
    }
    return best;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static bool vpack_try_page(const VPackPage *page, const Point2D orient_neg[8][32], const uint32_t orient_counts[8], const int32_t orient_aabb[8][4], const int32_t orient_min_cand[8][2],
                           const int32_t orient_max_cand[8][2], uint32_t orient_count, int32_t worst_poly_min_x, int32_t worst_poly_min_y, int32_t worst_poly_max_x, int32_t worst_poly_max_y,
                           int32_t global_min_cand_x, int32_t global_min_cand_y, int32_t global_max_cand_x, int32_t global_max_cand_y, uint32_t extrude, uint32_t margin, bool power_of_two,
                           VPackNFP *nfps, uint64_t (*nfp_grid)[VPACK_GRID_WORDS], VPackCand **cands, uint32_t *cand_cap, uint32_t *relevant_buf, PackStats *stats, uint64_t *io_best_score,
                           uint32_t page_index, uint32_t *out_best_page, int32_t *out_best_x, int32_t *out_best_y, uint8_t *out_best_orient, uint32_t *out_best_orient_idx) {
    uint32_t relevant_count = 0;
    for (uint32_t i = 0; i < page->count; i++) {
        int32_t est_min_x = page->placed[i].x + page->placed[i].aabb_min_x - worst_poly_max_x;
        int32_t est_max_x = page->placed[i].x + page->placed[i].aabb_max_x - worst_poly_min_x;
        int32_t est_min_y = page->placed[i].y + page->placed[i].aabb_min_y - worst_poly_max_y;
        int32_t est_max_y = page->placed[i].y + page->placed[i].aabb_max_y - worst_poly_min_y;
        if (est_max_x < global_min_cand_x || est_min_x > global_max_cand_x || est_max_y < global_min_cand_y || est_min_y > global_max_cand_y) {
            stats->yskip_count++;
            continue;
        }
        relevant_buf[relevant_count++] = i;
    }
    stats->relevant_count += relevant_count;

    bool found_on_page = false;
    for (uint32_t ori = 0; ori < orient_count; ori++) {
        uint32_t cur_count = orient_counts[ori];
        int32_t poly_min_x = orient_aabb[ori][0];
        int32_t poly_min_y = orient_aabb[ori][1];
        int32_t poly_max_x = orient_aabb[ori][2];
        int32_t poly_max_y = orient_aabb[ori][3];
        int32_t min_cand_x = orient_min_cand[ori][0];
        int32_t min_cand_y = orient_min_cand[ori][1];
        int32_t max_cand_x = orient_max_cand[ori][0];
        int32_t max_cand_y = orient_max_cand[ori][1];

        uint32_t cand_count = 0;
        VPackBounds bounds = {min_cand_x, min_cand_y, max_cand_x, max_cand_y};
        vpack_add_cand(cands, &cand_count, cand_cap, min_cand_x, min_cand_y, &bounds);

        uint32_t nfp_count = 0;
        for (uint32_t ri = 0; ri < relevant_count; ri++) {
            uint32_t i = relevant_buf[ri];
            int32_t est_min_x = page->placed[i].x + page->placed[i].aabb_min_x - poly_max_x;
            int32_t est_max_x = page->placed[i].x + page->placed[i].aabb_max_x - poly_min_x;
            int32_t est_min_y = page->placed[i].y + page->placed[i].aabb_min_y - poly_max_y;
            int32_t est_max_y = page->placed[i].y + page->placed[i].aabb_max_y - poly_min_y;
            if (est_max_x < min_cand_x || est_min_x > max_cand_x || est_max_y < min_cand_y || est_min_y > max_cand_y)
                continue;

            bool need_candidates = true;
            if (*io_best_score != UINT64_MAX) {
                uint64_t opt_score = vpack_score_candidate((est_min_x > 0) ? est_min_x : 0, (est_min_y > 0) ? est_min_y : 0, poly_max_x, poly_max_y, page->used_w, page->used_h, margin, power_of_two);
                if (opt_score > *io_best_score)
                    need_candidates = false;
            }

            VPackNFP *nfp = &nfps[nfp_count];
            nfp->count = vpack_minkowski(page->placed[i].poly, page->placed[i].count, orient_neg[ori], cur_count, nfp->verts);
            stats->or_count++;
            for (uint32_t v = 0; v < nfp->count; v++) {
                nfp->verts[v].x += page->placed[i].x;
                nfp->verts[v].y += page->placed[i].y;
            }
            vpack_calc_aabb(nfp->verts, nfp->count, &nfp->min_x, &nfp->min_y, &nfp->max_x, &nfp->max_y);

            if (need_candidates) {
                for (uint32_t v = 0; v < nfp->count; v++) {
                    vpack_add_cand(cands, &cand_count, cand_cap, nfp->verts[v].x, nfp->verts[v].y, &bounds);
                }
                for (uint32_t e = 0; e < nfp->count; e++) {
                    uint32_t en = (e + 1 == nfp->count) ? 0 : e + 1;
                    float out_val;
                    if (vpack_intersect_axis(nfp->verts[e], nfp->verts[en], true, (float)min_cand_x, &out_val)) {
                        vpack_add_float_cand(cands, &cand_count, cand_cap, (float)min_cand_x, out_val, &bounds);
                    }
                    if (vpack_intersect_axis(nfp->verts[e], nfp->verts[en], false, (float)min_cand_y, &out_val)) {
                        vpack_add_float_cand(cands, &cand_count, cand_cap, out_val, (float)min_cand_y, &bounds);
                    }
                }
            }
            nfp_count++;
        }
        stats->candidate_count += cand_count;

        uint32_t nfp_words = (nfp_count + 63) / 64;
        uint32_t dirty_cells[4096];
        uint32_t dirty_count = 0;
        if (nfp_count > 0 && nfp_words <= VPACK_GRID_WORDS) {
            for (uint32_t n = 0; n < nfp_count; n++) {
                int32_t gx0 = nfps[n].min_x / VPACK_GRID_CELL;
                int32_t gy0 = nfps[n].min_y / VPACK_GRID_CELL;
                int32_t gx1 = nfps[n].max_x / VPACK_GRID_CELL;
                int32_t gy1 = nfps[n].max_y / VPACK_GRID_CELL;
                if (gx0 < 0)
                    gx0 = 0;
                if (gy0 < 0)
                    gy0 = 0;
                if (gx1 >= VPACK_GRID_DIM)
                    gx1 = VPACK_GRID_DIM - 1;
                if (gy1 >= VPACK_GRID_DIM)
                    gy1 = VPACK_GRID_DIM - 1;
                uint32_t word = n / 64;
                uint64_t bit = (uint64_t)1 << (n % 64);
                for (int32_t gy = gy0; gy <= gy1; gy++) {
                    for (int32_t gx = gx0; gx <= gx1; gx++) {
                        uint32_t ci = (uint32_t)gy * VPACK_GRID_DIM + (uint32_t)gx;
                        if (nfp_grid[ci][0] == 0 && word == 0) {
                            if (dirty_count < 4096)
                                dirty_cells[dirty_count++] = ci;
                        }
                        nfp_grid[ci][word] |= bit;
                    }
                }
            }
        } else if (nfp_count > 0) {
            stats->grid_fallback_count++;
        }
        bool use_grid = (nfp_count > 0 && nfp_words <= VPACK_GRID_WORDS);

        for (uint32_t c = 0; c < cand_count; c++) {
            int32_t cx = (*cands)[c].x;
            int32_t cy = (*cands)[c].y;
            if (cx < min_cand_x || cy < min_cand_y)
                continue;
            if (cx + poly_min_x < 0 || cy + poly_min_y < 0)
                continue;
            if (cx < (int32_t)extrude || cy < (int32_t)extrude)
                continue;
            if (cx > max_cand_x || cy > max_cand_y)
                continue;

            uint64_t score = vpack_score_candidate(cx, cy, poly_max_x, poly_max_y, page->used_w, page->used_h, margin, power_of_two);
            if (score >= *io_best_score)
                continue;

            bool safe = true;
            if (use_grid && cx >= 0 && cy >= 0) {
                int32_t gcx = cx / VPACK_GRID_CELL;
                int32_t gcy = cy / VPACK_GRID_CELL;
                if (gcx < VPACK_GRID_DIM && gcy < VPACK_GRID_DIM) {
                    uint64_t *cell = nfp_grid[gcy * VPACK_GRID_DIM + gcx];
                    for (uint32_t w = 0; w < nfp_words && safe; w++) {
                        uint64_t bits = cell[w];
                        while (bits) {
                            uint32_t bit_idx = (uint32_t)__builtin_ctzll(bits);
                            uint32_t i = w * 64 + bit_idx;
                            stats->test_count++;
                            if (vpack_point_in_poly(cx, cy, nfps[i].verts, nfps[i].count)) {
                                safe = false;
                            }
                            bits &= bits - 1;
                        }
                    }
                }
            } else {
                for (uint32_t i = 0; i < nfp_count; i++) {
                    if (cx >= nfps[i].min_x && cx <= nfps[i].max_x && cy >= nfps[i].min_y && cy <= nfps[i].max_y) {
                        stats->test_count++;
                        if (vpack_point_in_poly(cx, cy, nfps[i].verts, nfps[i].count)) {
                            safe = false;
                            break;
                        }
                    }
                }
            }

            if (safe) {
                *out_best_page = page_index;
                *out_best_x = cx;
                *out_best_y = cy;
                *out_best_orient = (uint8_t)ori;
                *out_best_orient_idx = ori;
                *io_best_score = score;
                found_on_page = true;
            }
        }

        for (uint32_t d = 0; d < dirty_count; d++) {
            memset(nfp_grid[dirty_cells[d]], 0, (size_t)nfp_words * sizeof(uint64_t));
        }

        if (found_on_page && *out_best_page == page_index && *out_best_x == min_cand_x && *out_best_y == min_cand_y)
            break;
    }

    return found_on_page;
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
    stats->used_area = 0;
    stats->page_scan_count = 0;
    stats->page_prune_count = 0;
    stats->page_existing_hit_count = 0;
    stats->page_backfill_count = 0;
    stats->page_new_count = 0;
    stats->relevant_count = 0;
    stats->candidate_count = 0;
    stats->grid_fallback_count = 0;
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

    /* Pre-allocate NFP spatial grid on heap (reused across orientations+sprites) */
    uint64_t(*nfp_grid)[VPACK_GRID_WORDS] = (uint64_t(*)[VPACK_GRID_WORDS])calloc((size_t)VPACK_GRID_DIM * VPACK_GRID_DIM, sizeof(uint64_t[VPACK_GRID_WORDS]));
    NT_BUILD_ASSERT(nfp_grid && "vector_pack: grid alloc failed");

    VPackPage pages[ATLAS_MAX_PAGES];
    memset(pages, 0, sizeof(pages));
    pages[0].placed = placed;
    uint32_t page_count = 1;

    uint32_t orient_count = opts->allow_rotate ? 8 : 1;
    uint32_t *relevant_buf = (uint32_t *)malloc(sprite_count * sizeof(uint32_t));
    NT_BUILD_ASSERT(relevant_buf && "vector_pack: alloc failed");

    for (uint32_t s = 0; s < sprite_count; s++) {
        uint32_t idx = sorted[s].index;

        /* Build transformed+inflated polygons for all orientations */
        Point2D orient_polys[8][32];
        uint32_t orient_counts[8];
        orient_counts[0] = inf_counts[idx];
        memcpy(orient_polys[0], inf_polys[idx], inf_counts[idx] * sizeof(Point2D));
        for (uint32_t r = 1; r < orient_count; r++) {
            Point2D raw[32];
            polygon_transform(hull_verts[idx], hull_counts[idx], (uint8_t)r, (int32_t)trim_w[idx], (int32_t)trim_h[idx], raw);
            if (dilate > 0.0F) {
                orient_counts[r] = polygon_inflate(raw, hull_counts[idx], dilate, orient_polys[r]);
            } else {
                memcpy(orient_polys[r], raw, hull_counts[idx] * sizeof(Point2D));
                orient_counts[r] = hull_counts[idx];
            }
        }

        int32_t min_edge = (int32_t)margin > (int32_t)extrude ? (int32_t)margin : (int32_t)extrude;

        /* Try all orientations, pick best placement across all */
        bool found_any = false;
        uint32_t best_page = 0;
        int32_t best_x = 0;
        int32_t best_y = 0;
        uint8_t best_orient = 0;
        uint64_t best_score = UINT64_MAX;
        uint32_t best_orient_idx = 0; /* which orient_polys[] was used */

        /* Pre-compute per-orientation AABBs, bounds, negated polys */
        Point2D orient_neg[8][32];
        int32_t orient_aabb[8][4];     /* min_x, min_y, max_x, max_y */
        int32_t orient_min_cand[8][2]; /* min_cand_x, min_cand_y */
        int32_t orient_max_cand[8][2]; /* max_cand_x, max_cand_y */
        /* Worst-case AABB across all orientations for shared placed-sprite pre-filter */
        int32_t worst_poly_max_x = 0, worst_poly_max_y = 0;
        int32_t worst_poly_min_x = 0, worst_poly_min_y = 0;
        int32_t global_min_cand_x = INT32_MAX, global_min_cand_y = INT32_MAX;
        int32_t global_max_cand_x = 0, global_max_cand_y = 0;
        for (uint32_t ori = 0; ori < orient_count; ori++) {
            vpack_negate(orient_polys[ori], orient_counts[ori], orient_neg[ori]);
            vpack_calc_aabb(orient_polys[ori], orient_counts[ori], &orient_aabb[ori][0], &orient_aabb[ori][1], &orient_aabb[ori][2], &orient_aabb[ori][3]);
            int32_t mcx = min_edge - orient_aabb[ori][0];
            if (mcx < min_edge)
                mcx = min_edge;
            int32_t mcy = min_edge - orient_aabb[ori][1];
            if (mcy < min_edge)
                mcy = min_edge;
            orient_min_cand[ori][0] = mcx;
            orient_min_cand[ori][1] = mcy;
            orient_max_cand[ori][0] = (int32_t)max_size - (int32_t)margin - orient_aabb[ori][2] - 1;
            orient_max_cand[ori][1] = (int32_t)max_size - (int32_t)margin - orient_aabb[ori][3] - 1;
            if (orient_aabb[ori][2] > worst_poly_max_x)
                worst_poly_max_x = orient_aabb[ori][2];
            if (orient_aabb[ori][3] > worst_poly_max_y)
                worst_poly_max_y = orient_aabb[ori][3];
            if (orient_aabb[ori][0] < worst_poly_min_x)
                worst_poly_min_x = orient_aabb[ori][0];
            if (orient_aabb[ori][1] < worst_poly_min_y)
                worst_poly_min_y = orient_aabb[ori][1];
            if (mcx < global_min_cand_x)
                global_min_cand_x = mcx;
            if (mcy < global_min_cand_y)
                global_min_cand_y = mcy;
            if (orient_max_cand[ori][0] > global_max_cand_x)
                global_max_cand_x = orient_max_cand[ori][0];
            if (orient_max_cand[ori][1] > global_max_cand_y)
                global_max_cand_y = orient_max_cand[ori][1];
        }

        uint32_t page_count_before = page_count;
        for (uint32_t pi = 0; pi < page_count_before; pi++) {
            if (best_score != UINT64_MAX) {
                uint64_t page_lb = vpack_page_lower_bound(orient_aabb, orient_min_cand, orient_count, pages[pi].used_w, pages[pi].used_h, margin, opts->power_of_two);
                if (page_lb >= best_score) {
                    stats->page_prune_count++;
                    continue;
                }
            }
            stats->page_scan_count++;
            if (vpack_try_page(&pages[pi], orient_neg, orient_counts, orient_aabb, orient_min_cand, orient_max_cand, orient_count, worst_poly_min_x, worst_poly_min_y, worst_poly_max_x,
                               worst_poly_max_y, global_min_cand_x, global_min_cand_y, global_max_cand_x, global_max_cand_y, extrude, margin, opts->power_of_two, nfps, nfp_grid, &cands, &cand_cap,
                               relevant_buf, stats, &best_score, pi, &best_page, &best_x, &best_y, &best_orient, &best_orient_idx)) {
                found_any = true;
            }
        }

        if (!found_any) {
            if (page_count >= ATLAS_MAX_PAGES) {
                NT_LOG_ERROR("NFP packing failed: Out of pages (>=%u) for sprite %u!", ATLAS_MAX_PAGES, s);
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

            uint32_t new_page = page_count++;
            pages[new_page].placed = (VPackPlaced *)malloc(sprite_count * sizeof(VPackPlaced));
            NT_BUILD_ASSERT(pages[new_page].placed && "vector_pack: alloc failed");
            pages[new_page].count = 0;
            pages[new_page].used_w = 0;
            pages[new_page].used_h = 0;
            stats->page_new_count++;
            stats->page_scan_count++;
            found_any = vpack_try_page(&pages[new_page], orient_neg, orient_counts, orient_aabb, orient_min_cand, orient_max_cand, orient_count, worst_poly_min_x, worst_poly_min_y, worst_poly_max_x,
                                       worst_poly_max_y, global_min_cand_x, global_min_cand_y, global_max_cand_x, global_max_cand_y, extrude, margin, opts->power_of_two, nfps, nfp_grid, &cands,
                                       &cand_cap, relevant_buf, stats, &best_score, new_page, &best_page, &best_x, &best_y, &best_orient, &best_orient_idx);
            NT_BUILD_ASSERT(found_any && "vector_pack: empty page should accept placement");
        }

        // #region Save placement (using winning orientation's polygon)
        {
            Point2D *win_poly = orient_polys[best_orient_idx];
            uint32_t win_count = orient_counts[best_orient_idx];
            int32_t win_poly_max_x, win_poly_max_y, win_trash;
            vpack_calc_aabb(win_poly, win_count, &win_trash, &win_trash, &win_poly_max_x, &win_poly_max_y);

            if (pages[best_page].count > 0) {
                stats->page_existing_hit_count++;
                if (best_page + 1 < page_count_before) {
                    stats->page_backfill_count++;
                }
            }

            pages[best_page].placed[pages[best_page].count].count = win_count;
            pages[best_page].placed[pages[best_page].count].x = best_x;
            pages[best_page].placed[pages[best_page].count].y = best_y;
            for (uint32_t v = 0; v < win_count; v++) {
                pages[best_page].placed[pages[best_page].count].poly[v] = win_poly[v];
            }
            vpack_calc_aabb(win_poly, win_count, &pages[best_page].placed[pages[best_page].count].aabb_min_x, &pages[best_page].placed[pages[best_page].count].aabb_min_y,
                            &pages[best_page].placed[pages[best_page].count].aabb_max_x, &pages[best_page].placed[pages[best_page].count].aabb_max_y);
            pages[best_page].count++;

            out_placements[idx].sprite_index = idx;
            out_placements[idx].page = best_page;
            NT_BUILD_ASSERT(best_x >= (int32_t)extrude && best_y >= (int32_t)extrude && "vector_pack: placement too close to edge for extrude");
            out_placements[idx].x = (uint32_t)(best_x - (int32_t)extrude);
            out_placements[idx].y = (uint32_t)(best_y - (int32_t)extrude);
            out_placements[idx].rotation = best_orient;

            if (best_x + win_poly_max_x > (int32_t)pages[best_page].used_w)
                pages[best_page].used_w = (uint32_t)(best_x + win_poly_max_x);
            if (best_y + win_poly_max_y > (int32_t)pages[best_page].used_h)
                pages[best_page].used_h = (uint32_t)(best_y + win_poly_max_y);
        }
        // #endregion
    }

    // #region POT expansion
    *out_page_count = page_count;
    for (uint32_t i = 0; i < page_count; i++) {
        uint32_t final_w = pages[i].used_w + margin;
        uint32_t final_h = pages[i].used_h + margin;
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
    for (uint32_t i = 1; i < page_count; i++)
        free(pages[i].placed);
    free(placed);
    free(nfps);
    free(cands);
    free(nfp_grid);
    free(relevant_buf);
    // #endregion

    return sprite_count;
}
