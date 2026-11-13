#![allow(dead_code)]
use dino8_scene::*;
use std::collections::{BTreeMap, BTreeSet};

pub const BASE: u128 = 0x1234_5678_9abc_def0_0fed_cba9_8765_4321;
pub const STEP: u128 = 0x1_0000_0000_0000_0003;

pub fn uid(label: u32) -> Uuid {
    Uuid(BASE.wrapping_add(STEP.wrapping_mul(label as u128 + 1)))
}

pub fn label_of(id: Uuid) -> u32 {
    (id.0.wrapping_sub(BASE).wrapping_div(STEP) - 1) as u32
}

pub type EntRow = (Option<Geometry>, Option<Semantic>, Option<Procedural>, Option<Presentation>, Option<u64>);

/// Canonical, comparable description of a scene over a small label domain.
#[derive(Debug, Clone, PartialEq, Default)]
pub struct Dump {
    pub ents: BTreeMap<u32, EntRow>,
    pub hosting: BTreeSet<(u32, u32)>,
    pub containment: BTreeSet<(u32, u32)>,
    pub connection: BTreeSet<(u32, u32)>,
    pub reference: BTreeSet<(u32, u32)>,
    pub constraint: BTreeMap<(u32, u32), (u32, u64)>,
}

pub fn dump_scene(s: &Scene, domain: u32, pkey: Option<PropKey>) -> Dump {
    let mut d = Dump::default();
    let l = |ix: Ix| label_of(s.uuid_of(ix).unwrap());
    for lab in 0..domain {
        let Some(ix) = s.ix_of(uid(lab)) else { continue };
        let v = s.view(ix);
        let p = pkey.and_then(|k| s.prop(ix, k)).map(|v| v.to_raw());
        d.ents.insert(lab, (v.geometry, v.semantic, v.procedural, v.presentation, p));
        for b in s.out(RelKind::Hosting, ix) {
            d.hosting.insert((lab, l(b)));
        }
        for b in s.out(RelKind::Containment, ix) {
            d.containment.insert((lab, l(b)));
        }
        for b in s.out(RelKind::Connection, ix) {
            let lb = l(b); d.connection.insert((lab.min(lb), lab.max(lb))); // undirected
        }
        for b in s.out(RelKind::Reference, ix) {
            d.reference.insert((lab, l(b)));
        }
        for (b, ct, val, src) in s.constraints_of(ix) {
            if src {
                d.constraint.insert((lab, l(b)), (ct, val.to_bits()));
            }
        }
    }
    d
}
