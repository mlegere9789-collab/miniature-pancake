//! Static bounding-volume hierarchy over entity boxes, flattened in depth-first
//! order. Shared between snapshots through an `Arc`; entities edited after the
//! build are tracked by the scene's dirty overlay (see `Scene`).

use crate::types::{Aabb, Ix};

const LEAF_MAX: usize = 4;

#[derive(Copy, Clone)]
struct Node {
    bb: Aabb,
    /// Inner node: index of the right child (left child is `self + 1`), `count == 0`.
    /// Leaf: first item, `count > 0`.
    a: u32,
    count: u32,
}

pub struct Bvh {
    nodes: Vec<Node>,
    items_ix: Vec<Ix>,
    items_bb: Vec<Aabb>,
}

#[derive(Copy, Clone)]
struct Item {
    bb: Aabb,
    ix: Ix,
}

impl Bvh {
    pub fn build(items: impl Iterator<Item = (Ix, Aabb)>) -> Bvh {
        let mut v: Vec<Item> = items.map(|(ix, bb)| Item { bb, ix }).collect();
        let n = v.len();
        let mut nodes = Vec::with_capacity(n / 2 + 1);
        if n > 0 {
            build_rec(&mut v, 0, n, &mut nodes);
        }
        let items_ix = v.iter().map(|i| i.ix).collect();
        let items_bb = v.iter().map(|i| i.bb).collect();
        Bvh { nodes, items_ix, items_bb }
    }

    pub fn len(&self) -> usize {
        self.items_ix.len()
    }

    pub fn is_empty(&self) -> bool {
        self.items_ix.is_empty()
    }

    pub fn heap_bytes(&self) -> usize {
        self.nodes.capacity() * std::mem::size_of::<Node>()
            + self.items_ix.capacity() * 4
            + self.items_bb.capacity() * std::mem::size_of::<Aabb>()
    }

    /// Visit every item whose box intersects `q`; `f` returns false to stop.
    pub fn query_aabb(&self, q: &Aabb, mut f: impl FnMut(Ix, &Aabb) -> bool) {
        if self.nodes.is_empty() {
            return;
        }
        let mut stack = [0u32; 64];
        let mut sp = 1usize;
        stack[0] = 0;
        while sp > 0 {
            sp -= 1;
            let ni = stack[sp] as usize;
            let n = &self.nodes[ni];
            if !n.bb.intersects(q) {
                continue;
            }
            if n.count > 0 {
                for k in n.a as usize..(n.a + n.count) as usize {
                    if self.items_bb[k].intersects(q) && !f(self.items_ix[k], &self.items_bb[k]) {
                        return;
                    }
                }
            } else {
                stack[sp] = n.a;
                stack[sp + 1] = ni as u32 + 1;
                sp += 2;
            }
        }
    }

    /// Visit every item whose box the ray enters, with the entry distance.
    pub fn query_ray(&self, o: [f32; 3], dir: [f32; 3], mut f: impl FnMut(Ix, f32)) {
        if self.nodes.is_empty() {
            return;
        }
        let inv = [1.0 / dir[0], 1.0 / dir[1], 1.0 / dir[2]];
        let mut stack = [0u32; 64];
        let mut sp = 1usize;
        while sp > 0 {
            sp -= 1;
            let ni = stack[sp] as usize;
            let n = &self.nodes[ni];
            if n.bb.ray_hit(o, inv).is_none() {
                continue;
            }
            if n.count > 0 {
                for k in n.a as usize..(n.a + n.count) as usize {
                    if let Some(t) = self.items_bb[k].ray_hit(o, inv) {
                        f(self.items_ix[k], t);
                    }
                }
            } else {
                stack[sp] = n.a;
                stack[sp + 1] = ni as u32 + 1;
                sp += 2;
            }
        }
    }

    /// Items as (ix, box), for integrity checks.
    pub fn items(&self) -> impl Iterator<Item = (Ix, Aabb)> + '_ {
        self.items_ix.iter().copied().zip(self.items_bb.iter().copied())
    }
}

fn build_rec(v: &mut [Item], lo: usize, hi: usize, nodes: &mut Vec<Node>) -> u32 {
    let me = nodes.len() as u32;
    let mut bb = Aabb::empty();
    let mut cb = Aabb::empty();
    for it in &v[lo..hi] {
        bb = bb.union(&it.bb);
        let c = [
            (it.bb.min[0] + it.bb.max[0]) * 0.5,
            (it.bb.min[1] + it.bb.max[1]) * 0.5,
            (it.bb.min[2] + it.bb.max[2]) * 0.5,
        ];
        cb = cb.union(&Aabb { min: c, max: c });
    }
    if hi - lo <= LEAF_MAX {
        nodes.push(Node { bb, a: lo as u32, count: (hi - lo) as u32 });
        return me;
    }
    let ext = [cb.max[0] - cb.min[0], cb.max[1] - cb.min[1], cb.max[2] - cb.min[2]];
    let ax = if ext[0] >= ext[1] && ext[0] >= ext[2] {
        0
    } else if ext[1] >= ext[2] {
        1
    } else {
        2
    };
    let mid = (lo + hi) / 2;
    v[lo..hi].select_nth_unstable_by(mid - lo, |a, b| {
        let ka = a.bb.min[ax] + a.bb.max[ax];
        let kb = b.bb.min[ax] + b.bb.max[ax];
        ka.partial_cmp(&kb).unwrap_or(std::cmp::Ordering::Equal)
    });
    nodes.push(Node { bb, a: 0, count: 0 });
    build_rec(v, lo, mid, nodes);
    let right = build_rec(v, mid, hi, nodes);
    nodes[me as usize].a = right;
    me
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_brute_force() {
        let mut x = 12345u64;
        let mut rnd = || {
            x = x.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            ((x >> 33) as f32) / (1u64 << 31) as f32
        };
        let items: Vec<(Ix, Aabb)> = (0..5000u32)
            .map(|i| {
                let p = [rnd() * 100.0, rnd() * 100.0, rnd() * 100.0];
                (i, Aabb::new(p, [p[0] + rnd() * 3.0, p[1] + rnd() * 3.0, p[2] + rnd() * 3.0]))
            })
            .collect();
        let bvh = Bvh::build(items.iter().copied());
        for _ in 0..50 {
            let p = [rnd() * 100.0, rnd() * 100.0, rnd() * 100.0];
            let q = Aabb::new(p, [p[0] + 15.0, p[1] + 15.0, p[2] + 15.0]);
            let mut got = vec![];
            bvh.query_aabb(&q, |i, _| {
                got.push(i);
                true
            });
            got.sort();
            let mut want: Vec<Ix> = items.iter().filter(|(_, b)| b.intersects(&q)).map(|(i, _)| *i).collect();
            want.sort();
            assert_eq!(got, want);
        }
        // ray: compare hit sets
        let o = [-5.0, 50.0, 50.0];
        let d = [1.0, 0.01, 0.02];
        let inv = [1.0 / d[0], 1.0 / d[1], 1.0 / d[2]];
        let mut got = vec![];
        bvh.query_ray(o, d, |i, _| got.push(i));
        got.sort();
        let mut want: Vec<Ix> = items.iter().filter(|(_, b)| b.ray_hit(o, inv).is_some()).map(|(i, _)| *i).collect();
        want.sort();
        assert_eq!(got, want);
    }
}
