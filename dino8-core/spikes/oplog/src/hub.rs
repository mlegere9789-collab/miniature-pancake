//! A tiny in-process relay: sessions running on different threads (each with its own
//! [`Workspace`](crate::Workspace)) publish their operations to a shared sequencer and pull
//! everyone else's. The relay never interprets operations; it only orders them causally.

use crate::engine::Packet;
use crate::error::{Error, Result};
use crate::ids::{Hash, OpId, DOM_OP};
use crate::op::Operation;
use crate::shared::{AppendError, Arc, Entry, SharedLog};

pub struct HubEntry {
    pub id: OpId,
    pub parents: Vec<OpId>,
    pub packet: Packet,
}

impl Entry for HubEntry {
    type Id = OpId;
    fn id(&self) -> OpId {
        self.id
    }
    fn parents(&self) -> &[OpId] {
        &self.parents
    }
}

#[derive(Default)]
pub struct Hub {
    log: SharedLog<HubEntry>,
}

impl Hub {
    pub fn new() -> Hub {
        Hub::default()
    }

    /// Publish one operation. Returns its position, or `None` if it was already published.
    pub fn submit(&self, packet: Packet) -> Result<Option<usize>> {
        let id = OpId(Hash::digest(DOM_OP, &packet.record));
        let parents = Operation::decode(&packet.record)?.parents;
        match self.log.append(HubEntry { id, parents, packet }) {
            Ok(pos) => Ok(Some(pos)),
            Err(AppendError::Duplicate(_)) => Ok(None),
            Err(AppendError::MissingParent(p)) => Err(Error::MissingParent(p)),
        }
    }

    pub fn len(&self) -> usize {
        self.log.len()
    }

    pub fn is_empty(&self) -> bool {
        self.log.is_empty()
    }

    pub fn since(&self, from: usize) -> Vec<Arc<HubEntry>> {
        self.log.since(from)
    }
}
