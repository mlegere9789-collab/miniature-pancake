//! Uniform spatial grid built with counting-sort buckets.

use crate::store::{AgentId, AgentStore};
use core_math::{Fx, Vec2Fx};
use std::sync::atomic::{AtomicU64, Ordering};

/// Squared distance between two points as an exact integer in raw units
/// (Q64.64, i.e. the product of two raw Q32.32 values).
///
/// Each axis difference is at most 2^64 - 1 in magnitude, so a single square
/// always fits in `u128`; the sum saturates at `u128::MAX`, which is larger
/// than any legal `radius^2` (at most 2^126).
pub fn distance_sq_raw(a: Vec2Fx, b: Vec2Fx) -> u128 {
    let dx = u128::from(a.x.raw().abs_diff(b.x.raw()));
    let dy = u128::from(a.y.raw().abs_diff(b.y.raw()));
    (dx * dx).saturating_add(dy * dy)
}

#[derive(Clone, Copy, Debug)]
struct Entry {
    id: AgentId,
    pos: Vec2Fx,
}

/// A uniform grid over the plane, rebuilt from an [`AgentStore`] each tick.
///
/// Agents outside the grid are clamped into the border cells, so every alive
/// agent is always indexed. Within each cell, agents are stored in ascending
/// slot order, which makes the structure independent of insertion history.
/// Query results are sorted by `(distance_sq, slot index)`.
#[derive(Debug)]
pub struct SpatialGrid {
    cell_size: i64,
    width: u32,
    height: u32,
    origin: Vec2Fx,
    /// `cells + 1` offsets into `entries`.
    cell_start: Vec<u32>,
    entries: Vec<Entry>,
    scratch_cell: Vec<u32>,
    scratch_cursor: Vec<u32>,
    cells_touched: AtomicU64,
}

impl Clone for SpatialGrid {
    fn clone(&self) -> SpatialGrid {
        SpatialGrid {
            cell_size: self.cell_size,
            width: self.width,
            height: self.height,
            origin: self.origin,
            cell_start: self.cell_start.clone(),
            entries: self.entries.clone(),
            scratch_cell: Vec::new(),
            scratch_cursor: Vec::new(),
            cells_touched: AtomicU64::new(self.cells_touched.load(Ordering::Relaxed)),
        }
    }
}

impl SpatialGrid {
    /// Creates an empty grid of `width_cells` x `height_cells` cells of side
    /// `cell_size`, whose minimum corner is `origin`.
    ///
    /// A non-positive `cell_size` is raised to the smallest representable
    /// value (raw 1), and zero dimensions are raised to 1, so the grid is
    /// always valid.
    pub fn new(cell_size: Fx, width_cells: u32, height_cells: u32, origin: Vec2Fx) -> SpatialGrid {
        let width = width_cells.max(1);
        let height = height_cells.max(1);
        let cells = width as usize * height as usize;
        SpatialGrid {
            cell_size: cell_size.raw().max(1),
            width,
            height,
            origin,
            cell_start: vec![0; cells + 1],
            entries: Vec::new(),
            scratch_cell: Vec::new(),
            scratch_cursor: Vec::new(),
            cells_touched: AtomicU64::new(0),
        }
    }

    /// Grid width in cells.
    pub fn width_cells(&self) -> u32 {
        self.width
    }

    /// Grid height in cells.
    pub fn height_cells(&self) -> u32 {
        self.height
    }

    /// Number of agents currently indexed.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    /// Returns `true` if no agent is indexed.
    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }

    /// Cell column (or row) for a raw coordinate, clamped to `0..n`.
    fn axis_cell(&self, value: i128, origin: i64, n: u32) -> usize {
        let c = (value - i128::from(origin)).div_euclid(i128::from(self.cell_size));
        c.clamp(0, i128::from(n) - 1) as usize
    }

    fn cell_of(&self, p: Vec2Fx) -> usize {
        let cx = self.axis_cell(i128::from(p.x.raw()), self.origin.x.raw(), self.width);
        let cy = self.axis_cell(i128::from(p.y.raw()), self.origin.y.raw(), self.height);
        cy * self.width as usize + cx
    }

    /// Rebuilds the index from every alive agent in `store` in O(n + cells).
    ///
    /// The result depends only on the alive agents' slots and positions.
    pub fn rebuild(&mut self, store: &AgentStore) {
        let cells = self.width as usize * self.height as usize;
        self.scratch_cell.clear();
        self.cell_start.clear();
        self.cell_start.resize(cells + 1, 0);

        for (i, &alive) in store.alive.iter().enumerate() {
            if alive {
                let c = self.cell_of(store.pos[i]);
                self.scratch_cell.push(c as u32);
                self.cell_start[c + 1] += 1;
            }
        }
        for c in 0..cells {
            self.cell_start[c + 1] += self.cell_start[c];
        }
        self.scratch_cursor.clear();
        self.scratch_cursor
            .extend_from_slice(&self.cell_start[..cells]);

        let dummy = Entry {
            id: AgentId {
                index: 0,
                generation: 0,
            },
            pos: Vec2Fx::default(),
        };
        self.entries.clear();
        self.entries.resize(self.scratch_cell.len(), dummy);

        let mut k = 0usize;
        for (i, &alive) in store.alive.iter().enumerate() {
            if alive {
                let c = self.scratch_cell[k] as usize;
                k += 1;
                let slot = self.scratch_cursor[c] as usize;
                self.scratch_cursor[c] += 1;
                self.entries[slot] = Entry {
                    id: AgentId {
                        index: i as u32,
                        generation: store.generation[i],
                    },
                    pos: store.pos[i],
                };
            }
        }
    }

    /// Inclusive cell ranges `(x0, x1, y0, y1)` covering the circle's bounding box.
    fn range(&self, center: Vec2Fx, radius: i64) -> (usize, usize, usize, usize) {
        let r = i128::from(radius);
        let cx = i128::from(center.x.raw());
        let cy = i128::from(center.y.raw());
        (
            self.axis_cell(cx - r, self.origin.x.raw(), self.width),
            self.axis_cell(cx + r, self.origin.x.raw(), self.width),
            self.axis_cell(cy - r, self.origin.y.raw(), self.height),
            self.axis_cell(cy + r, self.origin.y.raw(), self.height),
        )
    }

    /// Calls `f` for every entry in the cells overlapping the query circle.
    fn for_candidates(&self, center: Vec2Fx, radius: i64, mut f: impl FnMut(&Entry)) {
        let (x0, x1, y0, y1) = self.range(center, radius);
        let w = self.width as usize;
        let mut touched = 0u64;
        for cy in y0..=y1 {
            for cx in x0..=x1 {
                touched += 1;
                let c = cy * w + cx;
                let (s, e) = (self.cell_start[c] as usize, self.cell_start[c + 1] as usize);
                for entry in &self.entries[s..e] {
                    f(entry);
                }
            }
        }
        self.cells_touched.fetch_add(touched, Ordering::Relaxed);
    }

    /// Clears `out` and fills it with every indexed agent whose squared
    /// distance to `center` is at most `radius^2`, sorted by
    /// `(distance_sq, slot index)`. A negative radius yields no agents.
    pub fn query_radius(&self, center: Vec2Fx, radius: Fx, out: &mut Vec<AgentId>) {
        out.clear();
        let r = radius.raw();
        if r < 0 {
            return;
        }
        let r2 = (r as u128) * (r as u128);
        let mut hits: Vec<(u128, u32, AgentId)> = Vec::new();
        self.for_candidates(center, r, |e| {
            let d = distance_sq_raw(e.pos, center);
            if d <= r2 {
                hits.push((d, e.id.index, e.id));
            }
        });
        hits.sort_unstable_by_key(|h| (h.0, h.1));
        out.extend(hits.into_iter().map(|h| h.2));
    }

    /// The closest indexed agent within `radius` of `center`, with the same
    /// `(distance_sq, slot index)` tie-break as [`SpatialGrid::query_radius`].
    pub fn nearest(&self, center: Vec2Fx, radius: Fx) -> Option<AgentId> {
        let r = radius.raw();
        if r < 0 {
            return None;
        }
        let r2 = (r as u128) * (r as u128);
        let mut best: Option<(u128, u32, AgentId)> = None;
        self.for_candidates(center, r, |e| {
            let d = distance_sq_raw(e.pos, center);
            if d <= r2 && best.is_none_or(|b| (d, e.id.index) < (b.0, b.1)) {
                best = Some((d, e.id.index, e.id));
            }
        });
        best.map(|b| b.2)
    }

    /// Total number of grid cells visited by queries since construction or the
    /// last [`SpatialGrid::reset_cells_touched`].
    pub fn cells_touched(&self) -> u64 {
        self.cells_touched.load(Ordering::Relaxed)
    }

    /// Resets the [`SpatialGrid::cells_touched`] counter to zero.
    pub fn reset_cells_touched(&self) {
        self.cells_touched.store(0, Ordering::Relaxed);
    }
}
