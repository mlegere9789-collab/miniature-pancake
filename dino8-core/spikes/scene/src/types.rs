//! Component and identity types.

use crate::sorted::Keyed;

/// Dense internal slot index. Not stable across processes; edges and tables
/// use it. The stable identity is [`Uuid`].
pub type Ix = u32;

/// Hard cap on slot indices (edge keys pack two indices in 60 bits).
pub const MAX_IX: u32 = (1 << 30) - 1;

/// Stable 128-bit entity identity.
#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Debug, Default)]
pub struct Uuid(pub u128);

impl Uuid {
    /// Build an RFC 4122 version-4 style id from 128 random bits.
    pub fn from_random_bits(bits: u128) -> Uuid {
        let mut b = bits;
        b &= !(0xF000u128 << 64);
        b |= 0x4000u128 << 64; // version 4
        b &= !(0xC000_0000_0000_0000u128);
        b |= 0x8000_0000_0000_0000u128; // variant 10
        Uuid(b)
    }
}

impl std::fmt::Display for Uuid {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let v = self.0;
        write!(
            f,
            "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
            (v >> 96) as u32,
            (v >> 80) as u16,
            (v >> 64) as u16,
            (v >> 48) as u16,
            (v & 0xFFFF_FFFF_FFFF) as u64
        )
    }
}

#[derive(Copy, Clone, PartialEq, Debug, Default)]
pub struct Aabb {
    pub min: [f32; 3],
    pub max: [f32; 3],
}

impl Aabb {
    pub fn new(min: [f32; 3], max: [f32; 3]) -> Aabb {
        Aabb { min, max }
    }
    pub fn intersects(&self, o: &Aabb) -> bool {
        self.min[0] <= o.max[0]
            && self.max[0] >= o.min[0]
            && self.min[1] <= o.max[1]
            && self.max[1] >= o.min[1]
            && self.min[2] <= o.max[2]
            && self.max[2] >= o.min[2]
    }
    pub fn union(&self, o: &Aabb) -> Aabb {
        Aabb {
            min: [self.min[0].min(o.min[0]), self.min[1].min(o.min[1]), self.min[2].min(o.min[2])],
            max: [self.max[0].max(o.max[0]), self.max[1].max(o.max[1]), self.max[2].max(o.max[2])],
        }
    }
    pub fn empty() -> Aabb {
        Aabb { min: [f32::INFINITY; 3], max: [f32::NEG_INFINITY; 3] }
    }
    pub fn translated(&self, d: [f32; 3]) -> Aabb {
        Aabb {
            min: [self.min[0] + d[0], self.min[1] + d[1], self.min[2] + d[2]],
            max: [self.max[0] + d[0], self.max[1] + d[1], self.max[2] + d[2]],
        }
    }
    /// Slab test. Returns the entry distance along the ray when it hits.
    pub fn ray_hit(&self, o: [f32; 3], inv_dir: [f32; 3]) -> Option<f32> {
        let mut t0 = 0.0f32;
        let mut t1 = f32::INFINITY;
        for a in 0..3 {
            let ta = (self.min[a] - o[a]) * inv_dir[a];
            let tb = (self.max[a] - o[a]) * inv_dir[a];
            let (lo, hi) = if ta < tb { (ta, tb) } else { (tb, ta) };
            // NaN (0 * inf) falls through: treat as inside the slab.
            if lo > t0 {
                t0 = lo;
            }
            if hi < t1 {
                t1 = hi;
            }
            if t0 > t1 {
                return None;
            }
        }
        Some(t0)
    }
}

/// Geometry component: opaque kernel handle plus world bounding box.
#[derive(Copy, Clone, PartialEq, Debug, Default)]
pub struct Geometry {
    pub bbox: Aabb,
    pub handle: u64,
}

/// Semantic component (IFC-aligned). Properties live in columns, see
/// `Scene::set_prop`.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Default)]
pub struct Semantic {
    /// Interned classification code (for example a Uniclass or OmniClass id).
    pub classification: u32,
    /// IFC class id (index into the IFC 4.3 entity list).
    pub class: u16,
    /// IFC predefined type enumeration value.
    pub ptype: u16,
    pub phase: u8,
    /// Design option (0 = main).
    pub option: u8,
}

/// Link to the procedural (Flow) node that produced the entity.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Default)]
pub struct Procedural {
    pub graph: u32,
    pub node: u32,
    pub output: u16,
}

/// Presentation component.
#[derive(Copy, Clone, PartialEq, Eq, Debug, Default)]
pub struct Presentation {
    pub material: u32,
    pub color: u32,
    pub layer: u16,
    pub flags: u16,
}

#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
#[repr(u8)]
pub enum RelKind {
    /// src (hosted element) is hosted by dst (host). One host per element.
    Hosting = 0,
    /// src is contained in dst (spatial structure). One container per element.
    Containment = 1,
    /// Undirected adjacency (stored canonically with the smaller index first).
    Connection = 2,
    /// Directed constraint src -> dst with payload (see `ConstraintRec`).
    Constraint = 3,
    /// Directed many-to-many reference.
    Reference = 4,
}

impl RelKind {
    pub const ALL: [RelKind; 5] =
        [RelKind::Hosting, RelKind::Containment, RelKind::Connection, RelKind::Constraint, RelKind::Reference];
    pub fn from_u8(v: u8) -> Option<RelKind> {
        RelKind::ALL.get(v as usize).copied()
    }
    pub fn single_parent(self) -> bool {
        matches!(self, RelKind::Hosting | RelKind::Containment)
    }
}

/// Spatial overlay entry: the current box of an entity edited since the BVH
/// was built (an empty box marks a deleted or geometry-less entity).
#[derive(Copy, Clone, Debug)]
pub struct DirtyEnt {
    pub ix: u32,
    pub bbox: Aabb,
}

impl Keyed for DirtyEnt {
    type K = u32;
    fn key(&self) -> u32 {
        self.ix
    }
}

/// Entry of the UUID map.
#[derive(Copy, Clone, Debug)]
pub struct IdEntry {
    pub hi: u64,
    pub lo: u64,
    pub ix: u32,
}

impl IdEntry {
    pub fn new(id: Uuid, ix: Ix) -> IdEntry {
        IdEntry { hi: (id.0 >> 64) as u64, lo: id.0 as u64, ix }
    }
}

impl Keyed for IdEntry {
    type K = (u64, u64);
    fn key(&self) -> (u64, u64) {
        (self.hi, self.lo)
    }
}

/// Payload of a constraint edge, keyed by the forward edge key.
#[derive(Copy, Clone, Debug, PartialEq)]
pub struct ConstraintRec {
    pub key: u64,
    pub value: f64,
    pub ctype: u32,
}

impl Keyed for ConstraintRec {
    type K = u64;
    fn key(&self) -> u64 {
        self.key
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Debug)]
pub enum PropKind {
    F64,
    I64,
    Bool,
    /// Interned string id.
    Str,
}

#[derive(Copy, Clone, PartialEq, Debug)]
pub enum PropVal {
    F64(f64),
    I64(i64),
    Bool(bool),
    Str(u32),
}

impl PropVal {
    pub fn kind(&self) -> PropKind {
        match self {
            PropVal::F64(_) => PropKind::F64,
            PropVal::I64(_) => PropKind::I64,
            PropVal::Bool(_) => PropKind::Bool,
            PropVal::Str(_) => PropKind::Str,
        }
    }
    pub fn to_raw(self) -> u64 {
        match self {
            PropVal::F64(v) => v.to_bits(),
            PropVal::I64(v) => v as u64,
            PropVal::Bool(v) => v as u64,
            PropVal::Str(v) => v as u64,
        }
    }
    pub fn from_raw(k: PropKind, r: u64) -> PropVal {
        match k {
            PropKind::F64 => PropVal::F64(f64::from_bits(r)),
            PropKind::I64 => PropVal::I64(r as i64),
            PropKind::Bool => PropVal::Bool(r != 0),
            PropKind::Str => PropVal::Str(r as u32),
        }
    }
}

#[derive(Copy, Clone, PartialEq, Eq, Debug, Hash)]
pub struct PropKey(pub u16);

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum SceneError {
    DuplicateUuid(Uuid),
    UnknownEntity(Uuid),
    CapacityExceeded,
    SelfRelation,
    Cycle,
    UnknownProperty,
    PropertyKind,
    Invalid(&'static str),
}

impl std::fmt::Display for SceneError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}
impl std::error::Error for SceneError {}
