//! Undo/redo by snapshot swap. Every state is a structurally shared `Scene`;
//! undo moves a pointer-sized value, independent of model size.

use crate::scene::Scene;

pub struct History {
    current: Scene,
    undo: Vec<Scene>,
    redo: Vec<Scene>,
}

impl History {
    pub fn new(scene: Scene) -> History {
        History { current: scene, undo: Vec::new(), redo: Vec::new() }
    }

    pub fn scene(&self) -> &Scene {
        &self.current
    }

    /// Run one undoable edit: snapshot first (O(1)), then mutate.
    pub fn edit<R>(&mut self, f: impl FnOnce(&mut Scene) -> R) -> R {
        self.undo.push(self.current.clone());
        self.redo.clear();
        f(&mut self.current)
    }

    pub fn can_undo(&self) -> bool {
        !self.undo.is_empty()
    }
    pub fn can_redo(&self) -> bool {
        !self.redo.is_empty()
    }
    pub fn undo_depth(&self) -> usize {
        self.undo.len()
    }

    pub fn undo(&mut self) -> bool {
        match self.undo.pop() {
            Some(prev) => {
                let cur = std::mem::replace(&mut self.current, prev);
                self.redo.push(cur);
                true
            }
            None => false,
        }
    }

    pub fn redo(&mut self) -> bool {
        match self.redo.pop() {
            Some(next) => {
                let cur = std::mem::replace(&mut self.current, next);
                self.undo.push(cur);
                true
            }
            None => false,
        }
    }
}
