//! Read API: by component, by relationship, by property range, spatial.

use crate::scene::*;
use crate::types::*;

/// All components of one entity.
#[derive(Copy, Clone, Debug)]
pub struct EntityView {
    pub ix: Ix,
    pub id: Uuid,
    pub geometry: Option<Geometry>,
    pub semantic: Option<Semantic>,
    pub procedural: Option<Procedural>,
    pub presentation: Option<Presentation>,
}

#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum Component {
    Geometry,
    Semantic,
    Procedural,
    Presentation,
}

impl Scene {
    // ---------- by identity ----------

    pub fn entity(&self, id: Uuid) -> Option<EntityView> {
        let ix = self.ix_of(id)?;
        Some(self.view(ix))
    }

    pub fn view(&self, ix: Ix) -> EntityView {
        EntityView {
            ix,
            id: self.uuid_of(ix).unwrap_or_default(),
            geometry: self.geom.get(ix).copied(),
            semantic: self.sem.get(ix).copied(),
            procedural: self.proc_.get(ix).copied(),
            presentation: self.pres.get(ix).copied(),
        }
    }

    pub fn geometry(&self, ix: Ix) -> Option<&Geometry> {
        self.geom.get(ix)
    }
    pub fn semantic(&self, ix: Ix) -> Option<&Semantic> {
        self.sem.get(ix)
    }
    pub fn procedural(&self, ix: Ix) -> Option<&Procedural> {
        self.proc_.get(ix)
    }
    pub fn presentation(&self, ix: Ix) -> Option<&Presentation> {
        self.pres.get(ix)
    }

    pub fn prop(&self, ix: Ix, key: PropKey) -> Option<PropVal> {
        let kind = self.schema.props.get(key.0 as usize)?.1;
        self.props.get(key.0 as usize)?.get(ix).map(|r| PropVal::from_raw(kind, *r))
    }

    // ---------- by component ----------

    pub fn with_component(&self, c: Component) -> Vec<Ix> {
        let mut out = Vec::new();
        match c {
            Component::Geometry => self.geom.scan(|i, _| out.push(i)),
            Component::Semantic => self.sem.scan(|i, _| out.push(i)),
            Component::Procedural => self.proc_.scan(|i, _| out.push(i)),
            Component::Presentation => self.pres.scan(|i, _| out.push(i)),
        }
        out
    }

    pub fn count_with_component(&self, c: Component) -> usize {
        match c {
            Component::Geometry => self.geom.len(),
            Component::Semantic => self.sem.len(),
            Component::Procedural => self.proc_.len(),
            Component::Presentation => self.pres.len(),
        }
    }

    /// Entities of an IFC class (any predefined type). Uses the inverted
    /// index when enabled, otherwise scans the semantic table.
    pub fn by_class(&self, class: u16) -> Vec<Ix> {
        if self.cfg.class_index {
            let lo = (class as u64) << 48;
            self.class_idx.range(lo, lo | ((1u64 << 48) - 1)).map(|k| (k & 0xFFFF_FFFF) as Ix).collect()
        } else {
            let mut out = Vec::new();
            self.sem.scan(|i, s| {
                if s.class == class {
                    out.push(i)
                }
            });
            out
        }
    }

    pub fn by_class_type(&self, class: u16, ptype: u16) -> Vec<Ix> {
        if self.cfg.class_index {
            let lo = ((class as u64) << 48) | ((ptype as u64) << 32);
            self.class_idx.range(lo, lo | 0xFFFF_FFFF).map(|k| (k & 0xFFFF_FFFF) as Ix).collect()
        } else {
            let mut out = Vec::new();
            self.sem.scan(|i, s| {
                if s.class == class && s.ptype == ptype {
                    out.push(i)
                }
            });
            out
        }
    }

    pub fn by_classification(&self, code: u32) -> Vec<Ix> {
        if self.cfg.classification_index && code != 0 {
            let lo = (code as u64) << 32;
            self.cls_idx.range(lo, lo | 0xFFFF_FFFF).map(|k| (k & 0xFFFF_FFFF) as Ix).collect()
        } else {
            let mut out = Vec::new();
            self.sem.scan(|i, s| {
                if s.classification == code {
                    out.push(i)
                }
            });
            out
        }
    }

    /// Phase and design option filter (full scan of the semantic table).
    pub fn by_phase_option(&self, phase: u8, option: u8) -> Vec<Ix> {
        let mut out = Vec::new();
        self.sem.scan(|i, s| {
            if s.phase == phase && s.option == option {
                out.push(i)
            }
        });
        out
    }

    // ---------- by property range ----------

    pub fn prop_range_f64(&self, key: PropKey, lo: f64, hi: f64) -> Vec<Ix> {
        let mut out = Vec::new();
        if let Some(c) = self.props.get(key.0 as usize) {
            c.scan(|i, r| {
                let v = f64::from_bits(*r);
                if v >= lo && v <= hi {
                    out.push(i)
                }
            });
        }
        out
    }

    pub fn prop_range_i64(&self, key: PropKey, lo: i64, hi: i64) -> Vec<Ix> {
        let mut out = Vec::new();
        if let Some(c) = self.props.get(key.0 as usize) {
            c.scan(|i, r| {
                let v = *r as i64;
                if v >= lo && v <= hi {
                    out.push(i)
                }
            });
        }
        out
    }

    /// Combined: class (index) then property range (probe by slot).
    pub fn by_class_and_prop_range(&self, class: u16, key: PropKey, lo: f64, hi: f64) -> Vec<Ix> {
        let col = match self.props.get(key.0 as usize) {
            Some(c) => c,
            None => return Vec::new(),
        };
        self.by_class(class)
            .into_iter()
            .filter(|&i| matches!(col.get(i), Some(r) if { let v = f64::from_bits(*r); v >= lo && v <= hi }))
            .collect()
    }

    // ---------- by relationship ----------

    pub fn host_of(&self, ix: Ix) -> Option<Ix> {
        self.parent(RelKind::Hosting, ix)
    }
    pub fn hosted_by(&self, host: Ix) -> Vec<Ix> {
        self.inn(RelKind::Hosting, host).collect()
    }
    pub fn container_of(&self, ix: Ix) -> Option<Ix> {
        self.parent(RelKind::Containment, ix)
    }
    pub fn contents_of(&self, container: Ix) -> Vec<Ix> {
        self.inn(RelKind::Containment, container).collect()
    }
    pub fn count_contents(&self, container: Ix) -> usize {
        self.inn(RelKind::Containment, container).count()
    }
    /// Undirected neighbours (connection edges are stored smaller-first).
    pub fn connected(&self, ix: Ix) -> Vec<Ix> {
        let mut v: Vec<Ix> = self.inn(RelKind::Connection, ix).collect();
        v.extend(self.out(RelKind::Connection, ix));
        v
    }
    pub fn references_from(&self, ix: Ix) -> Vec<Ix> {
        self.out(RelKind::Reference, ix).collect()
    }
    pub fn references_to(&self, ix: Ix) -> Vec<Ix> {
        self.inn(RelKind::Reference, ix).collect()
    }
    /// Constraints touching `ix`: (other end, ctype, value, ix_is_source).
    pub fn constraints_of(&self, ix: Ix) -> Vec<(Ix, u32, f64, bool)> {
        let mut v = Vec::new();
        for b in self.out(RelKind::Constraint, ix) {
            if let Some(r) = self.cons.get(ek(RelKind::Constraint, ix, b)) {
                v.push((b, r.ctype, r.value, true));
            }
        }
        for a in self.inn(RelKind::Constraint, ix) {
            if let Some(r) = self.cons.get(ek(RelKind::Constraint, a, ix)) {
                v.push((a, r.ctype, r.value, false));
            }
        }
        v
    }

    /// Transitive closure below `root` through a single-parent relation
    /// (hosting or containment), breadth first, excluding `root`.
    pub fn descendants(&self, kind: RelKind, root: Ix) -> Vec<Ix> {
        let mut out = Vec::new();
        let mut i = 0;
        let mut frontier = vec![root];
        while i < frontier.len() {
            let cur = frontier[i];
            i += 1;
            for c in self.inn(kind, cur) {
                out.push(c);
                frontier.push(c);
            }
        }
        out
    }

    // ---------- spatial ----------

    pub fn query_aabb(&self, q: &Aabb) -> Vec<Ix> {
        let mut out = Vec::new();
        match &self.spatial.bvh {
            None => self.geom.scan(|i, g| {
                if g.bbox.intersects(q) {
                    out.push(i)
                }
            }),
            Some(b) => {
                b.query_aabb(q, |ix, _| {
                    if !self.spatial.dirty.contains(ix) {
                        out.push(ix);
                    }
                    true
                });
                for d in self.spatial.dirty.iter() {
                    if d.bbox.intersects(q) {
                        out.push(d.ix);
                    }
                }
            }
        }
        out
    }

    /// All boxes the ray enters, nearest first.
    pub fn raycast(&self, origin: [f32; 3], dir: [f32; 3]) -> Vec<(Ix, f32)> {
        let mut out: Vec<(Ix, f32)> = Vec::new();
        let inv = [1.0 / dir[0], 1.0 / dir[1], 1.0 / dir[2]];
        match &self.spatial.bvh {
            None => self.geom.scan(|i, g| {
                if let Some(t) = g.bbox.ray_hit(origin, inv) {
                    out.push((i, t))
                }
            }),
            Some(b) => {
                b.query_ray(origin, dir, |ix, t| {
                    if !self.spatial.dirty.contains(ix) {
                        out.push((ix, t));
                    }
                });
                for d in self.spatial.dirty.iter() {
                    if let Some(t) = d.bbox.ray_hit(origin, inv) {
                        out.push((d.ix, t));
                    }
                }
            }
        }
        out.sort_by(|a, b| a.1.partial_cmp(&b.1).unwrap_or(std::cmp::Ordering::Equal));
        out
    }
}
