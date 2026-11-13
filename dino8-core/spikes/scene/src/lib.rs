//! Spike A: entity-component-relationship scene store with structural-sharing
//! snapshots. See RESULTS.md next to this crate for measurements.

pub mod bvh;
pub mod history;
pub mod ptable;
pub mod query;
pub mod rng;
pub mod scene;
pub mod sorted;
pub mod types;

pub use history::History;
pub use query::{Component, EntityView};
pub use scene::{Config, EntityParts, Scene, SceneBuilder, CT_DISTANCE, CT_FIXED_OFFSET};
pub use types::*;
