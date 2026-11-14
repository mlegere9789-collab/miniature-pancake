//! A small CAD-flavoured command set used to exercise the log.
//!
//! These stand in for the 1,070 real commands: each one reads typed parameters, edits objects
//! through [`Ctx`] only, and is deterministic given its recorded inputs. Property types:
//!
//! | property | type     | meaning                                   |
//! |----------|----------|-------------------------------------------|
//! | `name`   | register | text                                      |
//! | `pos`    | register | 3 floats                                  |
//! | `size`   | register | 3 floats                                  |
//! | `color`  | register | integer                                   |
//! | `layer`  | register | text                                      |
//! | `geom`   | register | hash of a content-addressed geometry blob |
//! | `host`   | register | reference to another object               |
//! | `tags`   | set      | text elements (add wins)                  |
//! | `pts`    | list     | polyline vertices (3 floats each)         |

use crate::error::{Error, Result};
use crate::exec::{CmdSpec, Ctx, Registry};
use crate::ids::{Hash, ObjectId};
use crate::value::Value;
use std::sync::atomic::{AtomicU64, Ordering};

fn bad<T>(m: &str) -> Result<T> {
    Err(Error::BadParams(m.to_string()))
}

fn param<'a>(p: &'a Value, k: &str) -> Result<&'a Value> {
    p.get(k).ok_or_else(|| Error::BadParams(format!("missing `{k}`")))
}

fn p_str(p: &Value, k: &str) -> Result<String> {
    param(p, k)?.as_str().map(str::to_string).map_or_else(|| bad(&format!("`{k}` must be text")), Ok)
}

fn p_int(p: &Value, k: &str) -> Result<i64> {
    param(p, k)?.as_i64().map_or_else(|| bad(&format!("`{k}` must be an integer")), Ok)
}

fn p_f64(p: &Value, k: &str) -> Result<f64> {
    param(p, k)?.as_f64().map_or_else(|| bad(&format!("`{k}` must be a number")), Ok)
}

fn p_f3(p: &Value, k: &str) -> Result<[f64; 3]> {
    param(p, k)?.as_f3().map_or_else(|| bad(&format!("`{k}` must be three numbers")), Ok)
}

fn p_obj(p: &Value, k: &str) -> Result<ObjectId> {
    param(p, k)?.as_obj().map_or_else(|| bad(&format!("`{k}` must be an object reference")), Ok)
}

fn p_objs(p: &Value, k: &str) -> Result<Vec<ObjectId>> {
    let l = param(p, k)?.as_list().map_or_else(|| bad(&format!("`{k}` must be a list")), Ok)?;
    l.iter().map(|v| v.as_obj().map_or_else(|| bad("list of object references expected"), Ok)).collect()
}

fn p_index(p: &Value, k: &str) -> Result<usize> {
    usize::try_from(p_int(p, k)?).or_else(|_| bad(&format!("`{k}` must not be negative")))
}

fn add3(a: [f64; 3], b: [f64; 3]) -> [f64; 3] {
    [a[0] + b[0], a[1] + b[1], a[2] + b[2]]
}

fn geometry(ctx: &mut Ctx<'_>, bytes: usize) -> Option<Hash> {
    if bytes == 0 {
        return None;
    }
    let mut buf = vec![0u8; bytes];
    ctx.rng().fill(&mut buf);
    Some(ctx.put_blob(&buf))
}

fn create_box(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let name = p_str(p, "name")?;
    let pos = p_f3(p, "pos")?;
    let size = p_f3(p, "size")?;
    let layer = p_str(p, "layer")?;
    let color = p_int(p, "color")?;
    let geom_bytes = p_index(p, "geom_bytes")?;
    let tags: Vec<String> = match p.get("tags") {
        Some(Value::List(l)) => l.iter().filter_map(|v| v.as_str().map(str::to_string)).collect(),
        _ => Vec::new(),
    };
    let geom = geometry(ctx, geom_bytes);
    let (dot, lam) = (ctx.dot(), ctx.lamport());
    let (id, o) = ctx.create()?;
    o.reg_mut("name")?.write(dot, lam, Value::Str(name));
    o.reg_mut("pos")?.write(dot, lam, Value::f3(pos));
    o.reg_mut("size")?.write(dot, lam, Value::f3(size));
    o.reg_mut("layer")?.write(dot, lam, Value::Str(layer));
    o.reg_mut("color")?.write(dot, lam, Value::Int(color));
    if let Some(g) = geom {
        o.reg_mut("geom")?.write(dot, lam, Value::Hash(g));
    }
    for t in tags {
        o.set_mut("tags")?.add(Value::Str(t), dot);
    }
    let _ = id;
    Ok(())
}

fn move_cmd(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let ids = p_objs(p, "ids")?;
    let delta = p_f3(p, "delta")?;
    for id in ids {
        let cur = ctx
            .value(id, "pos")?
            .and_then(|v| v.as_f3())
            .ok_or_else(|| Error::Command(format!("object {id} has no position")))?;
        ctx.set(id, "pos", Value::f3(add3(cur, delta)))?;
    }
    Ok(())
}

fn set_prop(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let id = p_obj(p, "id")?;
    let prop = p_str(p, "prop")?;
    ctx.set(id, &prop, param(p, "value")?.clone())
}

fn tag_add(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    ctx.set_add(p_obj(p, "id")?, "tags", Value::Str(p_str(p, "tag")?))
}

fn tag_remove(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    ctx.set_remove(p_obj(p, "id")?, "tags", &Value::Str(p_str(p, "tag")?))
}

fn pts_insert(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    ctx.list_insert(p_obj(p, "id")?, "pts", p_index(p, "index")?, Value::f3(p_f3(p, "point")?))
}

fn pts_delete(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    ctx.list_delete(p_obj(p, "id")?, "pts", p_index(p, "index")?)
}

fn pts_move(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    ctx.list_set(p_obj(p, "id")?, "pts", p_index(p, "index")?, Value::f3(p_f3(p, "point")?))
}

fn delete_cmd(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    for id in p_objs(p, "ids")? {
        ctx.delete(id)?;
    }
    Ok(())
}

fn keep_cmd(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    for id in p_objs(p, "ids")? {
        ctx.keep(id)?;
    }
    Ok(())
}

/// Create `count` boxes at random positions drawn from the operation's recorded seed.
fn scatter(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let count = p_index(p, "count")?;
    let extent = p_f64(p, "extent")?;
    let layer = p_str(p, "layer")?;
    let (dot, lam) = (ctx.dot(), ctx.lamport());
    for i in 0..count {
        let pos = [ctx.rng().next_f64() * extent, ctx.rng().next_f64() * extent, ctx.rng().next_f64() * 3.0];
        let color = ctx.rng().below(256) as i64;
        let (_, o) = ctx.create()?;
        o.reg_mut("name")?.write(dot, lam, Value::Str(format!("s{i}")));
        o.reg_mut("pos")?.write(dot, lam, Value::f3(pos));
        o.reg_mut("size")?.write(dot, lam, Value::f3([1.0, 1.0, 1.0]));
        o.reg_mut("layer")?.write(dot, lam, Value::Str(layer.clone()));
        o.reg_mut("color")?.write(dot, lam, Value::Int(color));
    }
    Ok(())
}

/// Copy one object `n` times, shifting each copy; the copies share the geometry blob.
fn array(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let src = p_obj(p, "src")?;
    let n = p_index(p, "n")?;
    let delta = p_f3(p, "delta")?;
    let (dot, lam) = (ctx.dot(), ctx.lamport());
    let pos = ctx.value(src, "pos")?.and_then(|v| v.as_f3()).ok_or_else(|| Error::Command("no position".into()))?;
    let size = ctx.value(src, "size")?.unwrap_or(Value::Null);
    let layer = ctx.value(src, "layer")?.unwrap_or(Value::Null);
    let color = ctx.value(src, "color")?.unwrap_or(Value::Null);
    let geom = ctx.value(src, "geom")?;
    let name = ctx.value(src, "name")?.and_then(|v| v.as_str().map(str::to_string)).unwrap_or_default();
    for i in 1..=n {
        let k = i as f64;
        let at = add3(pos, [delta[0] * k, delta[1] * k, delta[2] * k]);
        let (_, o) = ctx.create()?;
        o.reg_mut("name")?.write(dot, lam, Value::Str(format!("{name}.{i}")));
        o.reg_mut("pos")?.write(dot, lam, Value::f3(at));
        o.reg_mut("size")?.write(dot, lam, size.clone());
        o.reg_mut("layer")?.write(dot, lam, layer.clone());
        o.reg_mut("color")?.write(dot, lam, color.clone());
        if let Some(g) = &geom {
            o.reg_mut("geom")?.write(dot, lam, g.clone());
        }
    }
    Ok(())
}

/// A stand-in for a kernel operation: the result's geometry is a pure function of the inputs.
fn union_cmd(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let a = p_obj(p, "a")?;
    let b = p_obj(p, "b")?;
    let ga = ctx.value(a, "geom")?.and_then(|v| v.as_hash()).ok_or_else(|| Error::Command("a has no geometry".into()))?;
    let gb = ctx.value(b, "geom")?.and_then(|v| v.as_hash()).ok_or_else(|| Error::Command("b has no geometry".into()))?;
    let (ba, bb) = (ctx.get_blob(&ga)?, ctx.get_blob(&gb)?);
    let mut h = blake3::Hasher::new();
    h.update(&ba);
    h.update(&bb);
    let mut out = vec![0u8; ba.len().max(bb.len())];
    h.finalize_xof().fill(&mut out);
    let g = ctx.put_blob(&out);
    let (na, nb) = (
        ctx.value(a, "name")?.and_then(|v| v.as_str().map(str::to_string)).unwrap_or_default(),
        ctx.value(b, "name")?.and_then(|v| v.as_str().map(str::to_string)).unwrap_or_default(),
    );
    let pos = ctx.value(a, "pos")?.unwrap_or(Value::Null);
    let (dot, lam) = (ctx.dot(), ctx.lamport());
    let (_, o) = ctx.create()?;
    o.reg_mut("name")?.write(dot, lam, Value::Str(format!("{na}+{nb}")));
    o.reg_mut("pos")?.write(dot, lam, pos);
    o.reg_mut("geom")?.write(dot, lam, Value::Hash(g));
    ctx.delete(a)?;
    ctx.delete(b)?;
    Ok(())
}

/// Deliberately broken: reads the system clock. Replay must catch it.
fn bad_clock(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let id = p_obj(p, "id")?;
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |d| d.subsec_nanos());
    ctx.set(id, "stamp", Value::Int(i64::from(nanos)))
}

static GLOBAL_COUNTER: AtomicU64 = AtomicU64::new(0);

/// Deliberately broken: mutates state outside the dispatcher (a process-wide counter).
fn bad_global(ctx: &mut Ctx<'_>, p: &Value) -> Result<()> {
    let id = p_obj(p, "id")?;
    let n = GLOBAL_COUNTER.fetch_add(1, Ordering::SeqCst);
    ctx.set(id, "counter", Value::Int(n as i64))
}

/// The ordinary command set.
pub fn standard() -> Registry {
    let mut r = Registry::new();
    for (id, schema, run) in [
        ("create_box", "create_box/1", create_box as crate::exec::CmdFn),
        ("move", "move/1", move_cmd),
        ("set_prop", "set_prop/1", set_prop),
        ("tag_add", "tag_add/1", tag_add),
        ("tag_remove", "tag_remove/1", tag_remove),
        ("pts_insert", "pts_insert/1", pts_insert),
        ("pts_delete", "pts_delete/1", pts_delete),
        ("pts_move", "pts_move/1", pts_move),
        ("delete", "delete/1", delete_cmd),
        ("keep", "keep/1", keep_cmd),
        ("scatter", "scatter/1", scatter),
        ("array", "array/1", array),
        ("union", "union/1", union_cmd),
    ] {
        r.register(CmdSpec { id, schema, run });
    }
    r
}

/// The ordinary set plus two nondeterministic commands, used as negative controls for replay.
pub fn with_faulty() -> Registry {
    let mut r = standard();
    r.register(CmdSpec { id: "bad_clock", schema: "bad_clock/1", run: bad_clock });
    r.register(CmdSpec { id: "bad_global", schema: "bad_global/1", run: bad_global });
    r
}
