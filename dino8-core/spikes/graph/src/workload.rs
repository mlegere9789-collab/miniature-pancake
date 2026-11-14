//! Synthetic Revit-like regeneration workload for the Spike C benchmark.
//!
//! The model is a stack of levels; every level is a grid of rooms with four
//! walls each.  Derived data, all pure functions of the edited inputs:
//!
//! * `geom(w)`   regenerated wall geometry (length, area, volume, extents, direction)
//! * `join(w)`   wall join with the previous, next and the adjoining room's wall (4 geometries)
//! * `tag(w)`    annotation of every second wall (geometry plus tag style)
//! * `tile(v,t)` linework of one tile of the plan view of level `v` (joins and tags of the
//!   walls the spatial index lists for that tile, cut at the view height)
//! * `view(v)`   the plan view (reduction over its tiles)
//! * schedule    wall quantities by wall type: leaf chunks over `geom` and a reduction tree
//!
//! The same kernels run in the incremental engine and in the global-recompute
//! baseline, so outputs can be compared bit for bit.

use crate::engine::{Config, Db, EditReport, Kind, NodeId};
use crate::hash::{ContentHash, ContentHasher, Hash128};
use rayon::prelude::*;
use std::sync::Arc;

pub const K_WALL: Kind = Kind(0);
pub const K_WTYPE: Kind = Kind(1);
pub const K_LEVEL: Kind = Kind(2);
pub const K_VIEWDEF: Kind = Kind(3);
pub const K_TAGSTYLE: Kind = Kind(4);
pub const K_MEMBERS: Kind = Kind(5);
pub const K_GEOM: Kind = Kind(6);
pub const K_JOIN: Kind = Kind(7);
pub const K_TAG: Kind = Kind(8);
pub const K_TILE: Kind = Kind(9);
pub const K_VIEW: Kind = Kind(10);
pub const K_CHUNK: Kind = Kind(11);
pub const K_RNODE: Kind = Kind(12);

const ROOM: f32 = 6.0;

// ---- plain data -------------------------------------------------------------------------------

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WallIn {
    pub x0: f32,
    pub y0: f32,
    pub x1: f32,
    pub y1: f32,
    pub h: f32,
    pub ty: u16,
    pub level: u16,
    pub flags: u32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WType {
    pub thickness: f32,
    pub density: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ViewDef {
    pub level: u32,
    pub cut: f32,
    pub scale: f32,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TagStyle {
    pub size: f32,
    pub offset: f32,
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Geom {
    pub len: f32,
    pub area: f32,
    pub vol: f32,
    pub z0: f32,
    pub z1: f32,
    pub bx0: f32,
    pub by0: f32,
    pub bx1: f32,
    pub by1: f32,
    pub dx: f32,
    pub dy: f32,
    pub ty: u16,
    pub level: u16,
    pub chk: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Join {
    pub miter0: f32,
    pub miter1: f32,
    pub trimmed: f32,
    pub z0: f32,
    pub z1: f32,
    pub chk: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Tag {
    pub px: f32,
    pub py: f32,
    pub rot: f32,
    pub text: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Tile {
    pub nseg: u32,
    pub hash: Hash128,
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct ViewOut {
    pub ntiles: u32,
    pub nseg: u32,
    pub hash: Hash128,
}

#[derive(Clone, Copy, Debug, PartialEq, Default)]
pub struct Acc {
    pub count: u32,
    pub len: f32,
    pub area: f32,
    pub vol: f32,
}

#[derive(Clone, Debug, PartialEq, Default)]
pub struct Partial {
    pub acc: Vec<Acc>,
}

/// The value type of the benchmark database.
#[derive(Clone, Debug, PartialEq)]
pub enum Val {
    Wall(WallIn),
    WType(WType),
    Level(f32),
    ViewDef(ViewDef),
    TagStyle(TagStyle),
    Members(Arc<[u32]>),
    Geom(Geom),
    Join(Join),
    Tag(Tag),
    Tile(Tile),
    View(ViewOut),
    Part(Arc<Partial>),
}

impl ContentHash for Val {
    fn content_hash(&self, h: &mut ContentHasher) {
        match self {
            Val::Wall(w) => {
                h.write_u64(1);
                for f in [w.x0, w.y0, w.x1, w.y1, w.h] {
                    h.write_f32(f);
                }
                h.write_u64(u64::from(w.ty) | (u64::from(w.level) << 16) | (u64::from(w.flags) << 32));
            }
            Val::WType(t) => {
                h.write_u64(2);
                h.write_f32(t.thickness);
                h.write_f32(t.density);
            }
            Val::Level(e) => {
                h.write_u64(3);
                h.write_f32(*e);
            }
            Val::ViewDef(v) => {
                h.write_u64(4);
                h.write_u32(v.level);
                h.write_f32(v.cut);
                h.write_f32(v.scale);
            }
            Val::TagStyle(s) => {
                h.write_u64(5);
                h.write_f32(s.size);
                h.write_f32(s.offset);
            }
            Val::Members(m) => {
                h.write_u64(6);
                h.write_u64(m.len() as u64);
                for &x in m.iter() {
                    h.write_u32(x);
                }
            }
            Val::Geom(g) => {
                h.write_u64(7);
                for f in [g.len, g.area, g.vol, g.z0, g.z1, g.bx0, g.by0, g.bx1, g.by1, g.dx, g.dy] {
                    h.write_f32(f);
                }
                h.write_u64(u64::from(g.ty) | (u64::from(g.level) << 16) | (u64::from(g.chk) << 32));
            }
            Val::Join(j) => {
                h.write_u64(8);
                for f in [j.miter0, j.miter1, j.trimmed, j.z0, j.z1] {
                    h.write_f32(f);
                }
                h.write_u32(j.chk);
            }
            Val::Tag(t) => {
                h.write_u64(9);
                for f in [t.px, t.py, t.rot] {
                    h.write_f32(f);
                }
                h.write_u32(t.text);
            }
            Val::Tile(t) => {
                h.write_u64(10);
                h.write_u32(t.nseg);
                t.hash.content_hash(h);
            }
            Val::View(v) => {
                h.write_u64(11);
                h.write_u32(v.ntiles);
                h.write_u32(v.nseg);
                v.hash.content_hash(h);
            }
            Val::Part(p) => {
                h.write_u64(12);
                h.write_u64(p.acc.len() as u64);
                for a in &p.acc {
                    h.write_u32(a.count);
                    h.write_f32(a.len);
                    h.write_f32(a.area);
                    h.write_f32(a.vol);
                }
            }
        }
    }
}

// ---- synthetic kernel cost ---------------------------------------------------------------------

/// Burns `iters` iterations of a dependent xorshift chain (a few nanoseconds each, not foldable by
/// the optimizer); stands in for the cost of a real kernel call.  Deterministic.
#[inline(never)]
pub fn burn(iters: u32, seed: u32) -> u32 {
    let mut x = u64::from(seed) | 0x9E37_79B9_0000_0001;
    for _ in 0..iters {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    std::hint::black_box((x >> 40) as u32)
}

/// Nanoseconds per `burn` iteration on this machine (best of several short runs, so that a
/// busy machine does not inflate the estimate).
pub fn calibrate_burn_ns() -> f64 {
    let mut best = f64::MAX;
    for _ in 0..9 {
        let t0 = std::time::Instant::now();
        std::hint::black_box(burn(500_000, 7));
        best = best.min(t0.elapsed().as_secs_f64() * 1e9 / 500_000.0);
    }
    best
}

/// Extra synthetic work (in `burn` iterations) per kernel call.
#[derive(Clone, Copy, Debug, Default)]
pub struct Costs {
    pub geom: u32,
    pub join: u32,
    pub tag: u32,
    /// Per element visited by a tile.
    pub tile_item: u32,
}

impl Costs {
    /// All kernels cost `iters` extra iterations except the cheap reductions.
    pub fn uniform(iters: u32) -> Self {
        Costs { geom: iters, join: iters, tag: iters / 2, tile_item: 0 }
    }
}

// ---- kernels (pure; shared by the engine and by the baseline) ---------------------------------

pub fn geom_kernel(w: &WallIn, t: &WType, elev: f32, costs: &Costs) -> Geom {
    let dx = w.x1 - w.x0;
    let dy = w.y1 - w.y0;
    let len = (dx * dx + dy * dy).sqrt().max(1e-6);
    let (ux, uy) = (dx / len, dy / len);
    let half = t.thickness * 0.5;
    let (nx, ny) = (-uy * half, ux * half);
    let area = len * w.h;
    Geom {
        len,
        area,
        vol: area * t.thickness,
        z0: elev,
        z1: elev + w.h,
        bx0: w.x0.min(w.x1) - nx.abs(),
        by0: w.y0.min(w.y1) - ny.abs(),
        bx1: w.x0.max(w.x1) + nx.abs(),
        by1: w.y0.max(w.y1) + ny.abs(),
        dx: ux,
        dy: uy,
        ty: w.ty,
        level: w.level,
        chk: burn(costs.geom, w.ty.into()),
    }
}

pub fn join_kernel(g: &Geom, next: &Geom, prev: &Geom, across: &Geom, costs: &Costs) -> Join {
    let ang = |a: &Geom, b: &Geom| (a.dx * b.dx + a.dy * b.dy).clamp(-1.0, 1.0).acos();
    let a_next = ang(g, next);
    let a_prev = ang(g, prev);
    let a_across = ang(g, across);
    let trim = (g.bx1 - g.bx0).min(g.by1 - g.by0) * (a_next * 0.5).tan().abs().min(4.0);
    Join {
        miter0: a_next,
        miter1: a_prev,
        trimmed: (g.len - trim - 0.01 * a_across).max(0.0),
        z0: g.z0,
        z1: g.z1,
        chk: burn(costs.join, g.ty.into()),
    }
}

pub fn tag_kernel(g: &Geom, style: &TagStyle, costs: &Costs) -> Tag {
    let cx = 0.5 * (g.bx0 + g.bx1);
    let cy = 0.5 * (g.by0 + g.by1);
    let rot = g.dy.atan2(g.dx);
    let mut h = ContentHasher::new();
    h.write_f32((g.len * 100.0).round());
    h.write_u32(u32::from(g.ty));
    Tag {
        px: cx - g.dy * style.offset,
        py: cy + g.dx * style.offset,
        rot,
        text: (h.finish().lo as u32) ^ burn(costs.tag, g.ty.into()),
    }
}

/// Accumulates the linework of one tile.
pub struct TileAcc {
    cut: f32,
    nseg: u32,
    last_cut: bool,
    h: ContentHasher,
    extra: u32,
}

impl TileAcc {
    pub fn new(view: &ViewDef, costs: &Costs) -> Self {
        let mut h = ContentHasher::new();
        h.write_f32(view.cut);
        h.write_f32(view.scale);
        TileAcc { cut: view.cut, nseg: 0, last_cut: false, h, extra: costs.tile_item }
    }

    pub fn push_join(&mut self, j: &Join) {
        self.last_cut = j.z0 <= self.cut && self.cut <= j.z1;
        if self.last_cut {
            self.nseg += 2;
            self.h.write_f32(j.miter0);
            self.h.write_f32(j.miter1);
            self.h.write_f32(j.trimmed);
            self.h.write_u32(j.chk ^ burn(self.extra, 3));
        }
    }

    pub fn push_tag(&mut self, t: &Tag) {
        if self.last_cut {
            self.nseg += 1;
            self.h.write_f32(t.px);
            self.h.write_f32(t.py);
            self.h.write_u32(t.text);
        }
    }

    pub fn finish(self) -> Tile {
        Tile { nseg: self.nseg, hash: self.h.finish() }
    }
}

pub fn view_fold(tiles: impl Iterator<Item = Tile>) -> ViewOut {
    let mut h = ContentHasher::new();
    let (mut n, mut seg) = (0u32, 0u32);
    for t in tiles {
        n += 1;
        seg += t.nseg;
        h.write_u32(t.nseg);
        t.hash.content_hash(&mut h);
    }
    ViewOut { ntiles: n, nseg: seg, hash: h.finish() }
}

pub fn chunk_kernel<'a>(geoms: impl Iterator<Item = &'a Geom>, types: usize) -> Partial {
    let mut acc = vec![Acc::default(); types];
    for g in geoms {
        let a = &mut acc[g.ty as usize];
        a.count += 1;
        a.len += g.len;
        a.area += g.area;
        a.vol += g.vol;
    }
    Partial { acc }
}

pub fn merge_kernel<'a>(parts: impl Iterator<Item = &'a Partial>, types: usize) -> Partial {
    let mut acc = vec![Acc::default(); types];
    for p in parts {
        for (a, b) in acc.iter_mut().zip(&p.acc) {
            a.count += b.count;
            a.len += b.len;
            a.area += b.area;
            a.vol += b.vol;
        }
    }
    Partial { acc }
}

// ---- layout --------------------------------------------------------------------------------------

#[derive(Clone, Debug)]
pub struct Params {
    pub walls: usize,
    pub levels: usize,
    pub types: usize,
    pub leaf: usize,
    pub fan_in: usize,
    pub walls_per_tile: usize,
    pub seed: u64,
    pub costs: Costs,
}

impl Default for Params {
    fn default() -> Self {
        Params { walls: 100_000, levels: 20, types: 16, leaf: 64, fan_in: 64, walls_per_tile: 32, seed: 1, costs: Costs::default() }
    }
}

/// Shape of the schedule reduction tree.
#[derive(Clone, Debug)]
pub struct Tree {
    pub fan: usize,
    /// Node count per level; level 0 holds the leaf chunks.
    pub counts: Vec<usize>,
    /// Flat index of the first node of each level >= 1 in the `K_RNODE` table.
    pub offsets: Vec<usize>,
}

impl Tree {
    fn new(leaves: usize, fan: usize) -> Tree {
        let mut counts = vec![leaves];
        while *counts.last().expect("level") > 1 {
            counts.push(counts.last().expect("level").div_ceil(fan));
        }
        let mut offsets = vec![0; counts.len()];
        let mut off = 0;
        for (l, &c) in counts.iter().enumerate().skip(1) {
            offsets[l] = off;
            off += c;
        }
        Tree { fan, counts, offsets }
    }

    pub fn inner_total(&self) -> usize {
        self.counts.iter().skip(1).sum()
    }

    pub fn depth(&self) -> usize {
        self.counts.len()
    }

    /// `(kind, arg)` of node `i` of level `l`.
    pub fn node(&self, l: usize, i: usize) -> (Kind, u64) {
        if l == 0 {
            (K_CHUNK, i as u64)
        } else {
            (K_RNODE, (self.offsets[l] + i) as u64)
        }
    }

    /// Level and index of a flat `K_RNODE` argument.
    pub fn locate(&self, flat: usize) -> (usize, usize) {
        for l in (1..self.counts.len()).rev() {
            if flat >= self.offsets[l] {
                return (l, flat - self.offsets[l]);
            }
        }
        unreachable!("flat index {flat} outside the tree")
    }

    pub fn root(&self) -> (Kind, u64) {
        self.node(self.counts.len() - 1, 0)
    }
}

/// Immutable topology of the model.
#[derive(Clone, Debug)]
pub struct Layout {
    pub p: Params,
    pub n: usize,
    pub rooms_per_level: usize,
    pub side: usize,
    pub tiles_side: usize,
    pub extent: f32,
    pub across: Vec<u32>,
    pub tree: Tree,
}

impl Layout {
    pub fn new(p: &Params) -> Layout {
        let mut p = p.clone();
        let unit = 4 * p.levels;
        p.walls = (p.walls / unit).max(1) * unit;
        let n = p.walls;
        let rooms_per_level = n / unit;
        let side = (rooms_per_level as f64).sqrt().ceil() as usize;
        let walls_per_level = n / p.levels;
        let tiles_side = ((walls_per_level as f64 / p.walls_per_tile as f64).sqrt().round() as usize).max(1);
        let extent = side as f32 * ROOM;
        let across = (0..n).map(|w| Self::across_of(w, rooms_per_level, side) as u32).collect();
        let tree = Tree::new(n.div_ceil(p.leaf), p.fan_in);
        Layout { p, n, rooms_per_level, side, tiles_side, extent, across, tree }
    }

    fn across_of(w: usize, rpl: usize, side: usize) -> usize {
        let room = w / 4;
        let k = w % 4;
        let level = room / rpl;
        let idx = room % rpl;
        let (i, j) = (idx % side, idx / side);
        let (ni, nj, nk) = match k {
            0 if j > 0 => (i, j - 1, 2),
            1 => (i + 1, j, 3),
            2 => (i, j + 1, 0),
            3 if i > 0 => (i - 1, j, 1),
            _ => return w,
        };
        let nidx = nj * side + ni;
        if ni >= side || nidx >= rpl {
            return w;
        }
        (level * rpl + nidx) * 4 + nk
    }

    pub fn next(w: usize) -> usize {
        (w & !3) | ((w + 1) & 3)
    }

    pub fn prev(w: usize) -> usize {
        (w & !3) | ((w + 3) & 3)
    }

    pub fn has_tag(w: usize) -> bool {
        w % 2 == 0
    }

    pub fn tiles2(&self) -> usize {
        self.tiles_side * self.tiles_side
    }

    /// Flat tile index (`level * tiles2 + ty * tiles_side + tx`) of a wall's midpoint.
    pub fn tile_of(&self, w: &WallIn) -> usize {
        let t = self.tiles_side;
        let mx = 0.5 * (w.x0 + w.x1);
        let my = 0.5 * (w.y0 + w.y1);
        let tx = ((mx / self.extent * t as f32).floor().max(0.0) as usize).min(t - 1);
        let ty = ((my / self.extent * t as f32).floor().max(0.0) as usize).min(t - 1);
        usize::from(w.level) * self.tiles2() + ty * t + tx
    }

    pub fn leaf_range(&self, c: usize) -> std::ops::Range<usize> {
        c * self.p.leaf..((c + 1) * self.p.leaf).min(self.n)
    }

    pub fn children(&self, l: usize, i: usize) -> std::ops::Range<usize> {
        let f = self.tree.fan;
        i * f..((i + 1) * f).min(self.tree.counts[l - 1])
    }
}

/// Tiny deterministic generator.
#[derive(Clone, Debug)]
pub struct Rng(pub u64);

impl Rng {
    pub fn next_u64(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    pub fn below(&mut self, n: usize) -> usize {
        (self.next_u64() % n as u64) as usize
    }

    pub fn unit(&mut self) -> f32 {
        (self.next_u64() >> 40) as f32 / (1u64 << 24) as f32
    }
}

/// All input data of the model (the baseline's copy; the engine holds the same values as nodes).
#[derive(Clone, Debug)]
pub struct Inputs {
    pub walls: Vec<WallIn>,
    pub wtypes: Vec<WType>,
    pub elev: Vec<f32>,
    pub views: Vec<ViewDef>,
    pub tag: TagStyle,
    pub members: Vec<Vec<u32>>,
}

impl Inputs {
    pub fn generate(l: &Layout) -> Inputs {
        let p = &l.p;
        let mut rng = Rng(p.seed);
        let wtypes = (0..p.types)
            .map(|i| WType { thickness: 0.1 + 0.02 * i as f32, density: 1800.0 + 25.0 * i as f32 })
            .collect();
        let elev = (0..p.levels).map(|i| 3.2 * i as f32).collect();
        let views = (0..p.levels).map(|i| ViewDef { level: i as u32, cut: 3.2 * i as f32 + 1.2, scale: 100.0 }).collect();
        let mut walls = Vec::with_capacity(l.n);
        for room in 0..l.n / 4 {
            let level = room / l.rooms_per_level;
            let idx = room % l.rooms_per_level;
            let (i, j) = ((idx % l.side) as f32, (idx / l.side) as f32);
            let (ox, oy) = (i * ROOM, j * ROOM);
            let s = ROOM - 0.2;
            let h = 2.8 + 0.4 * rng.unit();
            let ty = rng.below(p.types) as u16;
            let corners = [(ox, oy), (ox + s, oy), (ox + s, oy + s), (ox, oy + s)];
            for k in 0..4 {
                let (x0, y0) = corners[k];
                let (x1, y1) = corners[(k + 1) % 4];
                walls.push(WallIn {
                    x0,
                    y0,
                    x1,
                    y1,
                    h,
                    ty: if k % 2 == 0 { ty } else { rng.below(p.types) as u16 },
                    level: level as u16,
                    flags: 0,
                });
            }
        }
        let mut members = vec![Vec::new(); p.levels * l.tiles2()];
        for (w, wall) in walls.iter().enumerate() {
            members[l.tile_of(wall)].push(w as u32);
        }
        Inputs { walls, wtypes, elev, views, tag: TagStyle { size: 2.5, offset: 0.4 }, members }
    }
}

// ---- global-recompute baseline -----------------------------------------------------------------

/// Everything the baseline (and the engine's roots) produce.
#[derive(Clone, Debug, PartialEq)]
pub struct Outputs {
    pub views: Vec<ViewOut>,
    pub schedule: Partial,
}

impl Outputs {
    pub fn digest(&self) -> Hash128 {
        let mut h = ContentHasher::new();
        for v in &self.views {
            h.write_u32(v.ntiles);
            h.write_u32(v.nseg);
            v.hash.content_hash(&mut h);
        }
        Val::Part(Arc::new(self.schedule.clone())).content_hash(&mut h);
        h.finish()
    }
}

/// Recomputes everything from the inputs with no memoization and no dependency tracking, staged
/// and parallel (rayon) or serial.  This is what a global regeneration costs.
pub fn global_recompute(l: &Layout, inp: &Inputs, parallel: bool) -> Outputs {
    let costs = l.p.costs;
    let n = l.n;
    let types = l.p.types;
    let geom_of = |w: usize| {
        let wall = &inp.walls[w];
        geom_kernel(wall, &inp.wtypes[wall.ty as usize], inp.elev[wall.level as usize], &costs)
    };
    let geoms: Vec<Geom> = if parallel { (0..n).into_par_iter().map(geom_of).collect() } else { (0..n).map(geom_of).collect() };
    let join_of = |w: usize| {
        join_kernel(&geoms[w], &geoms[Layout::next(w)], &geoms[Layout::prev(w)], &geoms[l.across[w] as usize], &costs)
    };
    let joins: Vec<Join> = if parallel { (0..n).into_par_iter().map(join_of).collect() } else { (0..n).map(join_of).collect() };
    let tag_of = |w: usize| {
        if Layout::has_tag(w) {
            tag_kernel(&geoms[w], &inp.tag, &costs)
        } else {
            Tag::default()
        }
    };
    let tags: Vec<Tag> = if parallel { (0..n).into_par_iter().map(tag_of).collect() } else { (0..n).map(tag_of).collect() };
    let t2 = l.tiles2();
    let tile_of = |idx: usize| {
        let (v, t) = (idx / t2, idx % t2);
        let view = &inp.views[v];
        let mut acc = TileAcc::new(view, &costs);
        for &m in &inp.members[view.level as usize * t2 + t] {
            let m = m as usize;
            acc.push_join(&joins[m]);
            if Layout::has_tag(m) {
                acc.push_tag(&tags[m]);
            }
        }
        acc.finish()
    };
    let total_tiles = l.p.levels * t2;
    let tiles: Vec<Tile> = if parallel {
        (0..total_tiles).into_par_iter().map(tile_of).collect()
    } else {
        (0..total_tiles).map(tile_of).collect()
    };
    let views: Vec<ViewOut> = (0..l.p.levels).map(|v| view_fold(tiles[v * t2..(v + 1) * t2].iter().copied())).collect();
    let leaf_of = |c: usize| chunk_kernel(l.leaf_range(c).map(|w| &geoms[w]), types);
    let mut level: Vec<Partial> = if parallel {
        (0..l.tree.counts[0]).into_par_iter().map(leaf_of).collect()
    } else {
        (0..l.tree.counts[0]).map(leaf_of).collect()
    };
    for lv in 1..l.tree.depth() {
        let prev = level;
        let node_of = |i: usize| merge_kernel(prev[l.children(lv, i)].iter(), types);
        level = if parallel {
            (0..l.tree.counts[lv]).into_par_iter().map(node_of).collect()
        } else {
            (0..l.tree.counts[lv]).map(node_of).collect()
        };
    }
    Outputs { views, schedule: level.pop().expect("schedule root") }
}

// ---- the engine side --------------------------------------------------------------------------

/// One change of an input, applied to both the baseline's data and the engine.
#[derive(Clone, Debug)]
pub enum Change {
    Wall(usize, WallIn),
    WType(usize, WType),
    Level(usize, f32),
    View(usize, ViewDef),
    Tag(TagStyle),
    Members(usize, Arc<[u32]>),
}

pub struct Workload {
    pub db: Db<Val>,
    pub layout: Arc<Layout>,
    pub inputs: Inputs,
    pub view_roots: Vec<NodeId>,
    pub sched_root: NodeId,
}

fn as_geom(v: &Val) -> Geom {
    match v {
        Val::Geom(g) => *g,
        other => panic!("expected geom, got {other:?}"),
    }
}

impl Workload {
    pub fn build(p: &Params, cfg: Config) -> Workload {
        let layout = Arc::new(Layout::new(p));
        let inputs = Inputs::generate(&layout);
        let mut cfg = cfg;
        let est = layout.n * 6 + layout.p.levels * layout.tiles2() * 2 + layout.tree.inner_total() + layout.tree.counts[0] + 4096;
        cfg.max_nodes = cfg.max_nodes.max(est);
        let mut db = Db::<Val>::new(cfg);
        let t2 = layout.tiles2();
        let n = layout.n;
        let kinds = [
            db.register_input("wall", Some(n)),
            db.register_input("wtype", Some(layout.p.types)),
            db.register_input("level", Some(layout.p.levels)),
            db.register_input("viewdef", Some(layout.p.levels)),
            db.register_input("tagstyle", Some(1)),
            db.register_input("members", Some(layout.p.levels * t2)),
        ];
        assert_eq!(kinds, [K_WALL, K_WTYPE, K_LEVEL, K_VIEWDEF, K_TAGSTYLE, K_MEMBERS]);
        Self::register_queries(&mut db, &layout);
        for (i, w) in inputs.walls.iter().enumerate() {
            db.create_input(K_WALL, i as u64, Val::Wall(*w));
        }
        for (i, t) in inputs.wtypes.iter().enumerate() {
            db.create_input(K_WTYPE, i as u64, Val::WType(*t));
        }
        for (i, e) in inputs.elev.iter().enumerate() {
            db.create_input(K_LEVEL, i as u64, Val::Level(*e));
        }
        for (i, v) in inputs.views.iter().enumerate() {
            db.create_input(K_VIEWDEF, i as u64, Val::ViewDef(*v));
        }
        db.create_input(K_TAGSTYLE, 0, Val::TagStyle(inputs.tag));
        for (i, m) in inputs.members.iter().enumerate() {
            db.create_input(K_MEMBERS, i as u64, Val::Members(m.as_slice().into()));
        }
        let view_roots = (0..layout.p.levels).map(|v| db.node(K_VIEW, v as u64)).collect();
        let (rk, ra) = layout.tree.root();
        let sched_root = db.node(rk, ra);
        Workload { db, layout, inputs, view_roots, sched_root }
    }

    fn register_queries(db: &mut Db<Val>, layout: &Arc<Layout>) {
        let l = layout.clone();
        let n = l.n;
        let t2 = l.tiles2();
        let costs = l.p.costs;
        let types = l.p.types;
        // geom(w): reads the wall, its type and its level.
        let k = db.register_query("geom", Some(n), move |ctx| {
            let w = ctx.arg();
            let wall = ctx.read(K_WALL, w)?;
            let Val::Wall(wi) = wall else { unreachable!("wall input") };
            let ty = ctx.read(K_WTYPE, u64::from(wi.ty))?;
            let lv = ctx.read(K_LEVEL, u64::from(wi.level))?;
            let (Val::WType(t), Val::Level(e)) = (ty, lv) else { unreachable!("type/level input") };
            Ok(Val::Geom(geom_kernel(&wi, &t, e, &costs)))
        });
        assert_eq!(k, K_GEOM);
        // join(w): own, next, previous and adjoining geometry.
        let l2 = l.clone();
        let k = db.register_query("join", Some(n), move |ctx| {
            let w = ctx.arg() as usize;
            let ids = [
                ctx.node(K_GEOM, w as u64),
                ctx.node(K_GEOM, Layout::next(w) as u64),
                ctx.node(K_GEOM, Layout::prev(w) as u64),
                ctx.node(K_GEOM, u64::from(l2.across[w])),
            ];
            let mut g = [Geom::default(); 4];
            ctx.visit_all(&ids, |i, v| g[i] = as_geom(v))?;
            Ok(Val::Join(join_kernel(&g[0], &g[1], &g[2], &g[3], &costs)))
        });
        assert_eq!(k, K_JOIN);
        let k = db.register_query("tag", Some(n), move |ctx| {
            let w = ctx.arg();
            let gid = ctx.node(K_GEOM, w);
            let sid = ctx.node(K_TAGSTYLE, 0);
            let mut g = Geom::default();
            let mut style = TagStyle { size: 0.0, offset: 0.0 };
            ctx.visit_all(&[gid, sid], |i, v| match (i, v) {
                (0, v) => g = as_geom(v),
                (_, Val::TagStyle(s)) => style = *s,
                _ => unreachable!("tag inputs"),
            })?;
            Ok(Val::Tag(tag_kernel(&g, &style, &costs)))
        });
        assert_eq!(k, K_TAG);
        // tile(v, t): linework of one tile of a plan view.
        let k = db.register_query("tile", Some(l.p.levels * t2), move |ctx| {
            let a = ctx.arg() as usize;
            let (v, t) = (a / t2, a % t2);
            let vd = ctx.read(K_VIEWDEF, v as u64)?;
            let Val::ViewDef(view) = vd else { unreachable!("view definition") };
            let mem = ctx.read(K_MEMBERS, u64::from(view.level) * t2 as u64 + t as u64)?;
            let Val::Members(list) = mem else { unreachable!("members") };
            let mut ids = Vec::with_capacity(list.len() * 3 / 2 + 1);
            for &m in list.iter() {
                ids.push(ctx.node(K_JOIN, u64::from(m)));
                if Layout::has_tag(m as usize) {
                    ids.push(ctx.node(K_TAG, u64::from(m)));
                }
            }
            let mut acc = TileAcc::new(&view, &costs);
            ctx.visit_all(&ids, |_, v| match v {
                Val::Join(j) => acc.push_join(j),
                Val::Tag(tg) => acc.push_tag(tg),
                _ => unreachable!("tile inputs"),
            })?;
            Ok(Val::Tile(acc.finish()))
        });
        assert_eq!(k, K_TILE);
        // view(v): reduction over the tiles of a view.
        let k = db.register_query("view", Some(l.p.levels), move |ctx| {
            let v = ctx.arg() as usize;
            let ids: Vec<NodeId> = (0..t2).map(|t| ctx.node(K_TILE, (v * t2 + t) as u64)).collect();
            let mut tiles = Vec::with_capacity(t2);
            ctx.visit_all(&ids, |_, val| match val {
                Val::Tile(t) => tiles.push(*t),
                _ => unreachable!("view inputs"),
            })?;
            Ok(Val::View(view_fold(tiles.into_iter())))
        });
        assert_eq!(k, K_VIEW);
        // Schedule: leaf chunks over geometries, then a reduction tree.
        let l3 = l.clone();
        let k = db.register_query("chunk", Some(l.tree.counts[0]), move |ctx| {
            let c = ctx.arg() as usize;
            let range = l3.leaf_range(c);
            let ids: Vec<NodeId> = range.map(|w| ctx.node(K_GEOM, w as u64)).collect();
            let mut gs = Vec::with_capacity(ids.len());
            ctx.visit_all(&ids, |_, v| gs.push(as_geom(v)))?;
            Ok(Val::Part(Arc::new(chunk_kernel(gs.iter(), types))))
        });
        assert_eq!(k, K_CHUNK);
        let l4 = l.clone();
        let k = db.register_query("rnode", Some(l.tree.inner_total().max(1)), move |ctx| {
            let (lv, i) = l4.tree.locate(ctx.arg() as usize);
            let ids: Vec<NodeId> = l4
                .children(lv, i)
                .map(|c| {
                    let (kd, a) = l4.tree.node(lv - 1, c);
                    ctx.node(kd, a)
                })
                .collect();
            let mut parts: Vec<Arc<Partial>> = Vec::with_capacity(ids.len());
            ctx.visit_all(&ids, |_, v| match v {
                Val::Part(p) => parts.push(p.clone()),
                _ => unreachable!("rnode inputs"),
            })?;
            Ok(Val::Part(Arc::new(merge_kernel(parts.iter().map(|p| &**p), types))))
        });
        assert_eq!(k, K_RNODE);
    }

    /// All roots: every view and the schedule.
    pub fn all_roots(&self) -> Vec<NodeId> {
        let mut r = self.view_roots.clone();
        r.push(self.sched_root);
        r
    }

    /// Reads the current outputs of the engine (every root must be clean).
    pub fn outputs(&self) -> Outputs {
        let views = self
            .view_roots
            .iter()
            .map(|&r| match self.db.value(r) {
                Some(Val::View(v)) => v,
                other => panic!("view root not clean: {other:?}"),
            })
            .collect();
        let schedule = match self.db.value(self.sched_root) {
            Some(Val::Part(p)) => (*p).clone(),
            other => panic!("schedule root not clean: {other:?}"),
        };
        Outputs { views, schedule }
    }

    /// Applies input changes to the baseline's copy and to the engine in one edit transaction.
    pub fn apply(&mut self, label: &str, changes: &[Change]) -> EditReport {
        let mut node_sets: Vec<(Kind, u64, Val)> = Vec::with_capacity(changes.len());
        for c in changes {
            match c {
                Change::Wall(w, v) => {
                    self.inputs.walls[*w] = *v;
                    node_sets.push((K_WALL, *w as u64, Val::Wall(*v)));
                }
                Change::WType(t, v) => {
                    self.inputs.wtypes[*t] = *v;
                    node_sets.push((K_WTYPE, *t as u64, Val::WType(*v)));
                }
                Change::Level(l, e) => {
                    self.inputs.elev[*l] = *e;
                    node_sets.push((K_LEVEL, *l as u64, Val::Level(*e)));
                }
                Change::View(v, d) => {
                    self.inputs.views[*v] = *d;
                    node_sets.push((K_VIEWDEF, *v as u64, Val::ViewDef(*d)));
                }
                Change::Tag(s) => {
                    self.inputs.tag = *s;
                    node_sets.push((K_TAGSTYLE, 0, Val::TagStyle(*s)));
                }
                Change::Members(i, m) => {
                    self.inputs.members[*i] = m.to_vec();
                    node_sets.push((K_MEMBERS, *i as u64, Val::Members(m.clone())));
                }
            }
        }
        let ids: Vec<NodeId> = node_sets.iter().map(|(k, a, _)| self.db.node(*k, *a)).collect();
        let (_, rep) = self.db.edit(label, |tx| {
            for (id, (_, _, v)) in ids.iter().zip(node_sets) {
                tx.set(*id, v);
            }
        });
        rep
    }

    /// Changes that move wall `w` to `new`, including the spatial-index update when it changes tile.
    pub fn wall_changes(&self, w: usize, new: WallIn) -> Vec<Change> {
        let old = self.inputs.walls[w];
        let (t_old, t_new) = (self.layout.tile_of(&old), self.layout.tile_of(&new));
        let mut out = vec![Change::Wall(w, new)];
        if t_old != t_new {
            let wid = w as u32;
            let without: Vec<u32> = self.inputs.members[t_old].iter().copied().filter(|&x| x != wid).collect();
            let mut with = self.inputs.members[t_new].clone();
            let pos = with.partition_point(|&x| x < wid);
            with.insert(pos, wid);
            out.push(Change::Members(t_old, without.into()));
            out.push(Change::Members(t_new, with.into()));
        }
        out
    }
}

// ---- edit generators (shared by the tests and the benchmark) -----------------------------------

impl Workload {
    /// Nudges the far end of wall `w` by `dx` (a typical parametric edit).
    pub fn edit_move_wall(&self, w: usize, dx: f32) -> Vec<Change> {
        let mut v = self.inputs.walls[w];
        v.x1 += dx;
        self.wall_changes(w, v)
    }

    /// Moves wall `w` by one tile width so that the spatial index changes.
    pub fn edit_move_across_tile(&self, w: usize) -> Vec<Change> {
        let mut v = self.inputs.walls[w];
        let shift = self.layout.extent / self.layout.tiles_side as f32;
        let mx = 0.5 * (v.x0 + v.x1);
        let d = if mx + shift < self.layout.extent { shift } else { -shift };
        v.x0 += d;
        v.x1 += d;
        self.wall_changes(w, v)
    }

    pub fn edit_height(&self, w: usize, dh: f32) -> Vec<Change> {
        let mut v = self.inputs.walls[w];
        v.h += dh;
        self.wall_changes(w, v)
    }

    /// Changes a field no derived value depends on (early cutoff at the geometry).
    pub fn edit_cosmetic(&self, w: usize) -> Vec<Change> {
        let mut v = self.inputs.walls[w];
        v.flags = v.flags.wrapping_add(1);
        self.wall_changes(w, v)
    }

    pub fn edit_wall_type(&self, t: usize, dt: f32) -> Vec<Change> {
        let mut v = self.inputs.wtypes[t];
        v.thickness += dt;
        vec![Change::WType(t, v)]
    }

    pub fn edit_level(&self, l: usize, de: f32) -> Vec<Change> {
        vec![Change::Level(l, self.inputs.elev[l] + de)]
    }

    pub fn edit_view_cut(&self, v: usize, dc: f32) -> Vec<Change> {
        let mut d = self.inputs.views[v];
        d.cut += dc;
        vec![Change::View(v, d)]
    }

    pub fn edit_tag_style(&self, d: f32) -> Vec<Change> {
        let mut s = self.inputs.tag;
        s.offset += d;
        vec![Change::Tag(s)]
    }

    /// First wall of a level.
    pub fn level_first_wall(&self, level: usize) -> usize {
        level * self.layout.rooms_per_level * 4
    }

    pub fn walls_per_level(&self) -> usize {
        self.layout.rooms_per_level * 4
    }
}
