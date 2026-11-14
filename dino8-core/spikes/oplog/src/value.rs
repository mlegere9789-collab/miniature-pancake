//! The typed value tree and its two canonical encodings.
//!
//! * a compact binary form (used for hashing and for the write-ahead log), and
//! * a one-line JSON form with sorted keys (used for the text-diffable export).
//!
//! Both are canonical: a value has exactly one encoding in each, and the decoders reject every
//! other spelling (padded varints, unsorted or duplicate keys, reserved `$` keys, ...). That is
//! what makes "same bytes, same hash" and "export, import, export gives the same text" true.

use crate::ids::{hex_decode, hex_encode, Hash, ObjectId};
use std::cmp::Ordering;
use std::collections::BTreeMap;
use std::fmt;

/// Maximum nesting accepted by the decoders (guards the stack against hostile input).
pub const MAX_DEPTH: usize = 64;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CodecError {
    Eof,
    BadTag(u8),
    NonCanonical(&'static str),
    Utf8,
    TooDeep,
    Trailing,
    Syntax(String),
}

impl fmt::Display for CodecError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            CodecError::Eof => write!(f, "unexpected end of input"),
            CodecError::BadTag(t) => write!(f, "unknown tag {t}"),
            CodecError::NonCanonical(w) => write!(f, "non-canonical encoding: {w}"),
            CodecError::Utf8 => write!(f, "invalid UTF-8"),
            CodecError::TooDeep => write!(f, "nesting too deep"),
            CodecError::Trailing => write!(f, "trailing bytes"),
            CodecError::Syntax(s) => write!(f, "syntax error: {s}"),
        }
    }
}

impl std::error::Error for CodecError {}

/// A typed parameter / property value.
///
/// Floats compare and order by their bit pattern (`-0.0` differs from `0.0`, a NaN equals
/// itself), because the encoded form must round-trip exactly.
#[derive(Debug, Clone)]
pub enum Value {
    Null,
    Bool(bool),
    Int(i64),
    Float(f64),
    Str(String),
    Bytes(Vec<u8>),
    /// Reference to a content-addressed blob (for example heavy geometry).
    Hash(Hash),
    /// Reference to another scene object.
    Obj(ObjectId),
    List(Vec<Value>),
    Map(BTreeMap<String, Value>),
}

impl Value {
    fn rank(&self) -> u8 {
        match self {
            Value::Null => 0,
            Value::Bool(_) => 1,
            Value::Int(_) => 2,
            Value::Float(_) => 3,
            Value::Str(_) => 4,
            Value::Bytes(_) => 5,
            Value::Hash(_) => 6,
            Value::Obj(_) => 7,
            Value::List(_) => 8,
            Value::Map(_) => 9,
        }
    }

    pub fn str(s: &str) -> Value {
        Value::Str(s.to_string())
    }

    /// Build a map from key/value pairs. Keys beginning with `$` are reserved for the JSON
    /// tagging scheme and are rejected by the decoders; callers must not use them.
    pub fn map<'a, I: IntoIterator<Item = (&'a str, Value)>>(items: I) -> Value {
        let mut m = BTreeMap::new();
        for (k, v) in items {
            debug_assert!(!k.starts_with('$'), "reserved key {k}");
            m.insert(k.to_string(), v);
        }
        Value::Map(m)
    }

    pub fn f3(v: [f64; 3]) -> Value {
        Value::List(v.iter().map(|x| Value::Float(*x)).collect())
    }

    pub fn from_u64(v: u64) -> Value {
        Value::Int(v as i64)
    }

    pub fn get(&self, key: &str) -> Option<&Value> {
        match self {
            Value::Map(m) => m.get(key),
            _ => None,
        }
    }

    pub fn as_str(&self) -> Option<&str> {
        match self {
            Value::Str(s) => Some(s),
            _ => None,
        }
    }

    pub fn as_i64(&self) -> Option<i64> {
        match self {
            Value::Int(i) => Some(*i),
            _ => None,
        }
    }

    pub fn as_u64(&self) -> Option<u64> {
        self.as_i64().map(|i| i as u64)
    }

    /// Accepts both `Float` and `Int` (so scripts may write `1` for `1.0`).
    pub fn as_f64(&self) -> Option<f64> {
        match self {
            Value::Float(f) => Some(*f),
            Value::Int(i) => Some(*i as f64),
            _ => None,
        }
    }

    pub fn as_list(&self) -> Option<&[Value]> {
        match self {
            Value::List(l) => Some(l),
            _ => None,
        }
    }

    pub fn as_obj(&self) -> Option<ObjectId> {
        match self {
            Value::Obj(o) => Some(*o),
            _ => None,
        }
    }

    pub fn as_hash(&self) -> Option<Hash> {
        match self {
            Value::Hash(h) => Some(*h),
            _ => None,
        }
    }

    pub fn as_f3(&self) -> Option<[f64; 3]> {
        let l = self.as_list()?;
        if l.len() != 3 {
            return None;
        }
        Some([l[0].as_f64()?, l[1].as_f64()?, l[2].as_f64()?])
    }
}

impl PartialEq for Value {
    fn eq(&self, other: &Self) -> bool {
        self.cmp(other) == Ordering::Equal
    }
}

impl Eq for Value {}

impl PartialOrd for Value {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for Value {
    fn cmp(&self, other: &Self) -> Ordering {
        match (self, other) {
            (Value::Null, Value::Null) => Ordering::Equal,
            (Value::Bool(a), Value::Bool(b)) => a.cmp(b),
            (Value::Int(a), Value::Int(b)) => a.cmp(b),
            (Value::Float(a), Value::Float(b)) => a.total_cmp(b),
            (Value::Str(a), Value::Str(b)) => a.cmp(b),
            (Value::Bytes(a), Value::Bytes(b)) => a.cmp(b),
            (Value::Hash(a), Value::Hash(b)) => a.cmp(b),
            (Value::Obj(a), Value::Obj(b)) => a.cmp(b),
            (Value::List(a), Value::List(b)) => a.cmp(b),
            (Value::Map(a), Value::Map(b)) => a.cmp(b),
            _ => self.rank().cmp(&other.rank()),
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Binary codec
// ---------------------------------------------------------------------------------------------

const T_NULL: u8 = 0;
const T_FALSE: u8 = 1;
const T_TRUE: u8 = 2;
const T_INT: u8 = 3;
const T_FLOAT: u8 = 4;
const T_STR: u8 = 5;
const T_BYTES: u8 = 6;
const T_HASH: u8 = 7;
const T_OBJ: u8 = 8;
const T_LIST: u8 = 9;
const T_MAP: u8 = 10;

/// LEB128 unsigned varint.
pub fn put_varint(out: &mut Vec<u8>, mut v: u64) {
    while v >= 0x80 {
        out.push((v as u8) | 0x80);
        v >>= 7;
    }
    out.push(v as u8);
}

fn zigzag(v: i64) -> u64 {
    ((v << 1) ^ (v >> 63)) as u64
}

fn unzigzag(u: u64) -> i64 {
    ((u >> 1) as i64) ^ -((u & 1) as i64)
}

/// Cursor over a byte slice with canonical-form checks.
pub struct Reader<'a> {
    buf: &'a [u8],
    pos: usize,
}

impl<'a> Reader<'a> {
    pub fn new(buf: &'a [u8]) -> Self {
        Reader { buf, pos: 0 }
    }

    pub fn pos(&self) -> usize {
        self.pos
    }

    pub fn is_empty(&self) -> bool {
        self.pos >= self.buf.len()
    }

    pub fn u8(&mut self) -> Result<u8, CodecError> {
        let b = *self.buf.get(self.pos).ok_or(CodecError::Eof)?;
        self.pos += 1;
        Ok(b)
    }

    pub fn bytes(&mut self, n: usize) -> Result<&'a [u8], CodecError> {
        let end = self.pos.checked_add(n).ok_or(CodecError::Eof)?;
        let s = self.buf.get(self.pos..end).ok_or(CodecError::Eof)?;
        self.pos = end;
        Ok(s)
    }

    pub fn array<const N: usize>(&mut self) -> Result<[u8; N], CodecError> {
        let s = self.bytes(N)?;
        let mut a = [0u8; N];
        a.copy_from_slice(s);
        Ok(a)
    }

    pub fn varint(&mut self) -> Result<u64, CodecError> {
        let mut result: u64 = 0;
        let mut shift = 0u32;
        let start = self.pos;
        loop {
            let b = self.u8()?;
            if shift == 63 && b > 1 {
                return Err(CodecError::NonCanonical("varint overflow"));
            }
            result |= u64::from(b & 0x7f) << shift;
            if b & 0x80 == 0 {
                if b == 0 && self.pos - start > 1 {
                    return Err(CodecError::NonCanonical("padded varint"));
                }
                return Ok(result);
            }
            shift += 7;
            if shift > 63 {
                return Err(CodecError::NonCanonical("varint overflow"));
            }
        }
    }

    pub fn len_prefixed(&mut self) -> Result<&'a [u8], CodecError> {
        let n = self.varint()?;
        let n = usize::try_from(n).map_err(|_| CodecError::Eof)?;
        self.bytes(n)
    }

    pub fn string(&mut self) -> Result<String, CodecError> {
        let b = self.len_prefixed()?;
        std::str::from_utf8(b)
            .map(str::to_string)
            .map_err(|_| CodecError::Utf8)
    }

    fn value(&mut self, depth: usize) -> Result<Value, CodecError> {
        if depth > MAX_DEPTH {
            return Err(CodecError::TooDeep);
        }
        let tag = self.u8()?;
        Ok(match tag {
            T_NULL => Value::Null,
            T_FALSE => Value::Bool(false),
            T_TRUE => Value::Bool(true),
            T_INT => Value::Int(unzigzag(self.varint()?)),
            T_FLOAT => Value::Float(f64::from_bits(u64::from_le_bytes(self.array::<8>()?))),
            T_STR => Value::Str(self.string()?),
            T_BYTES => Value::Bytes(self.len_prefixed()?.to_vec()),
            T_HASH => Value::Hash(Hash(self.array::<32>()?)),
            T_OBJ => Value::Obj(ObjectId(self.array::<16>()?)),
            T_LIST => {
                let n = self.varint()?;
                // every element takes at least one byte, which bounds the allocation
                if n > (self.buf.len() - self.pos) as u64 {
                    return Err(CodecError::Eof);
                }
                let mut v = Vec::with_capacity(n as usize);
                for _ in 0..n {
                    v.push(self.value(depth + 1)?);
                }
                Value::List(v)
            }
            T_MAP => {
                let n = self.varint()?;
                if n > (self.buf.len() - self.pos) as u64 {
                    return Err(CodecError::Eof);
                }
                let mut m = BTreeMap::new();
                let mut prev: Option<String> = None;
                for _ in 0..n {
                    let k = self.string()?;
                    if k.starts_with('$') {
                        return Err(CodecError::NonCanonical("reserved key"));
                    }
                    if let Some(p) = &prev {
                        if *p >= k {
                            return Err(CodecError::NonCanonical("unsorted or duplicate key"));
                        }
                    }
                    let v = self.value(depth + 1)?;
                    prev = Some(k.clone());
                    m.insert(k, v);
                }
                Value::Map(m)
            }
            t => return Err(CodecError::BadTag(t)),
        })
    }

    pub fn read_value(&mut self) -> Result<Value, CodecError> {
        self.value(0)
    }
}

impl Value {
    /// Append the canonical binary encoding.
    pub fn encode(&self, out: &mut Vec<u8>) {
        match self {
            Value::Null => out.push(T_NULL),
            Value::Bool(false) => out.push(T_FALSE),
            Value::Bool(true) => out.push(T_TRUE),
            Value::Int(i) => {
                out.push(T_INT);
                put_varint(out, zigzag(*i));
            }
            Value::Float(f) => {
                out.push(T_FLOAT);
                out.extend_from_slice(&f.to_bits().to_le_bytes());
            }
            Value::Str(s) => {
                out.push(T_STR);
                put_varint(out, s.len() as u64);
                out.extend_from_slice(s.as_bytes());
            }
            Value::Bytes(b) => {
                out.push(T_BYTES);
                put_varint(out, b.len() as u64);
                out.extend_from_slice(b);
            }
            Value::Hash(h) => {
                out.push(T_HASH);
                out.extend_from_slice(&h.0);
            }
            Value::Obj(o) => {
                out.push(T_OBJ);
                out.extend_from_slice(&o.0);
            }
            Value::List(l) => {
                out.push(T_LIST);
                put_varint(out, l.len() as u64);
                for v in l {
                    v.encode(out);
                }
            }
            Value::Map(m) => {
                out.push(T_MAP);
                put_varint(out, m.len() as u64);
                for (k, v) in m {
                    put_varint(out, k.len() as u64);
                    out.extend_from_slice(k.as_bytes());
                    v.encode(out);
                }
            }
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut v = Vec::with_capacity(64);
        self.encode(&mut v);
        v
    }

    /// Decode exactly one value; trailing bytes are an error.
    pub fn from_bytes(buf: &[u8]) -> Result<Value, CodecError> {
        let mut r = Reader::new(buf);
        let v = r.read_value()?;
        if !r.is_empty() {
            return Err(CodecError::Trailing);
        }
        Ok(v)
    }
}

// ---------------------------------------------------------------------------------------------
// Canonical JSON codec
// ---------------------------------------------------------------------------------------------

fn write_json_string(s: &str, out: &mut String) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            c if (c as u32) < 0x20 => {
                out.push_str("\\u00");
                out.push(char::from(b"0123456789abcdef"[(c as usize) >> 4]));
                out.push(char::from(b"0123456789abcdef"[(c as usize) & 15]));
            }
            c => out.push(c),
        }
    }
    out.push('"');
}

fn write_json_float(f: f64, out: &mut String) {
    use std::fmt::Write;
    if !f.is_finite() {
        let _ = write!(out, "{{\"$f\":\"{:016x}\"}}", f.to_bits());
        return;
    }
    let a = f.abs();
    if a != 0.0 && !(1e-5..1e16).contains(&a) {
        let _ = write!(out, "{f:e}");
    } else {
        let start = out.len();
        let _ = write!(out, "{f}");
        if !out[start..].contains('.') {
            out.push_str(".0");
        }
    }
}

impl Value {
    pub fn write_json(&self, out: &mut String) {
        use std::fmt::Write;
        match self {
            Value::Null => out.push_str("null"),
            Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
            Value::Int(i) => {
                let _ = write!(out, "{i}");
            }
            Value::Float(f) => write_json_float(*f, out),
            Value::Str(s) => write_json_string(s, out),
            Value::Bytes(b) => {
                out.push_str("{\"$b\":\"");
                out.push_str(&hex_encode(b));
                out.push_str("\"}");
            }
            Value::Hash(h) => {
                out.push_str("{\"$h\":\"");
                out.push_str(&h.to_hex());
                out.push_str("\"}");
            }
            Value::Obj(o) => {
                out.push_str("{\"$o\":\"");
                out.push_str(&o.to_hex());
                out.push_str("\"}");
            }
            Value::List(l) => {
                out.push('[');
                for (i, v) in l.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    v.write_json(out);
                }
                out.push(']');
            }
            Value::Map(m) => {
                out.push('{');
                for (i, (k, v)) in m.iter().enumerate() {
                    if i > 0 {
                        out.push(',');
                    }
                    write_json_string(k, out);
                    out.push(':');
                    v.write_json(out);
                }
                out.push('}');
            }
        }
    }

    pub fn to_json(&self) -> String {
        let mut s = String::with_capacity(128);
        self.write_json(&mut s);
        s
    }

    pub fn from_json(s: &str) -> Result<Value, CodecError> {
        let mut p = JsonParser { s: s.as_bytes(), pos: 0 };
        p.ws();
        let v = p.value(0)?;
        p.ws();
        if p.pos != p.s.len() {
            return Err(CodecError::Trailing);
        }
        Ok(v)
    }
}

struct JsonParser<'a> {
    s: &'a [u8],
    pos: usize,
}

fn syn<T>(msg: &str) -> Result<T, CodecError> {
    Err(CodecError::Syntax(msg.to_string()))
}

impl JsonParser<'_> {
    fn ws(&mut self) {
        while self.pos < self.s.len() && matches!(self.s[self.pos], b' ' | b'\t' | b'\n' | b'\r') {
            self.pos += 1;
        }
    }

    fn peek(&self) -> Option<u8> {
        self.s.get(self.pos).copied()
    }

    fn expect(&mut self, c: u8) -> Result<(), CodecError> {
        if self.peek() == Some(c) {
            self.pos += 1;
            Ok(())
        } else {
            syn(&format!("expected '{}' at {}", c as char, self.pos))
        }
    }

    fn literal(&mut self, word: &str) -> Result<(), CodecError> {
        if self.s[self.pos..].starts_with(word.as_bytes()) {
            self.pos += word.len();
            Ok(())
        } else {
            syn(&format!("bad literal at {}", self.pos))
        }
    }

    fn value(&mut self, depth: usize) -> Result<Value, CodecError> {
        if depth > MAX_DEPTH {
            return Err(CodecError::TooDeep);
        }
        match self.peek() {
            None => Err(CodecError::Eof),
            Some(b'n') => self.literal("null").map(|_| Value::Null),
            Some(b't') => self.literal("true").map(|_| Value::Bool(true)),
            Some(b'f') => self.literal("false").map(|_| Value::Bool(false)),
            Some(b'"') => self.string().map(Value::Str),
            Some(b'[') => {
                self.pos += 1;
                self.ws();
                let mut v = Vec::new();
                if self.peek() == Some(b']') {
                    self.pos += 1;
                    return Ok(Value::List(v));
                }
                loop {
                    self.ws();
                    v.push(self.value(depth + 1)?);
                    self.ws();
                    match self.peek() {
                        Some(b',') => self.pos += 1,
                        Some(b']') => {
                            self.pos += 1;
                            return Ok(Value::List(v));
                        }
                        _ => return syn(&format!("expected ',' or ']' at {}", self.pos)),
                    }
                }
            }
            Some(b'{') => self.object(depth),
            Some(b'-' | b'0'..=b'9') => self.number(),
            Some(c) => syn(&format!("unexpected '{}' at {}", c as char, self.pos)),
        }
    }

    fn object(&mut self, depth: usize) -> Result<Value, CodecError> {
        self.pos += 1;
        self.ws();
        let mut m = BTreeMap::new();
        if self.peek() == Some(b'}') {
            self.pos += 1;
            return Ok(Value::Map(m));
        }
        loop {
            self.ws();
            let k = self.string()?;
            self.ws();
            self.expect(b':')?;
            self.ws();
            let v = self.value(depth + 1)?;
            if m.insert(k, v).is_some() {
                return syn("duplicate key");
            }
            self.ws();
            match self.peek() {
                Some(b',') => self.pos += 1,
                Some(b'}') => {
                    self.pos += 1;
                    break;
                }
                _ => return syn(&format!("expected ',' or '}}' at {}", self.pos)),
            }
        }
        // tagged single-key objects carry the non-JSON types
        if m.keys().any(|k| k.starts_with('$')) {
            if m.len() != 1 {
                return syn("reserved '$' key in a multi-key object");
            }
            let (k, v) = m.into_iter().next().ok_or(CodecError::Eof)?;
            let Value::Str(hex) = v else {
                return syn("tagged value must be a string");
            };
            return match k.as_str() {
                "$b" => hex_decode(&hex)
                    .map(Value::Bytes)
                    .ok_or_else(|| CodecError::Syntax("bad hex in $b".into())),
                "$h" => Hash::from_hex(&hex)
                    .map(Value::Hash)
                    .ok_or_else(|| CodecError::Syntax("bad hash in $h".into())),
                "$o" => ObjectId::from_hex(&hex)
                    .map(Value::Obj)
                    .ok_or_else(|| CodecError::Syntax("bad id in $o".into())),
                "$f" => {
                    let b = hex_decode(&hex)
                        .and_then(|b| <[u8; 8]>::try_from(b).ok())
                        .ok_or_else(|| CodecError::Syntax("bad bits in $f".into()))?;
                    let f = f64::from_bits(u64::from_be_bytes(b));
                    if f.is_finite() {
                        return syn("$f must hold a non-finite float");
                    }
                    Ok(Value::Float(f))
                }
                _ => syn("unknown tag"),
            };
        }
        Ok(Value::Map(m))
    }

    fn string(&mut self) -> Result<String, CodecError> {
        self.expect(b'"')?;
        let mut out = String::new();
        loop {
            let c = self.peek().ok_or(CodecError::Eof)?;
            match c {
                b'"' => {
                    self.pos += 1;
                    return Ok(out);
                }
                b'\\' => {
                    self.pos += 1;
                    let e = self.peek().ok_or(CodecError::Eof)?;
                    self.pos += 1;
                    match e {
                        b'"' => out.push('"'),
                        b'\\' => out.push('\\'),
                        b'/' => out.push('/'),
                        b'b' => out.push('\u{8}'),
                        b'f' => out.push('\u{c}'),
                        b'n' => out.push('\n'),
                        b'r' => out.push('\r'),
                        b't' => out.push('\t'),
                        b'u' => {
                            let hi = self.hex4()?;
                            let cp = if (0xD800..0xDC00).contains(&hi) {
                                if self.s[self.pos..].starts_with(b"\\u") {
                                    self.pos += 2;
                                    let lo = self.hex4()?;
                                    if !(0xDC00..0xE000).contains(&lo) {
                                        return syn("bad low surrogate");
                                    }
                                    0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00)
                                } else {
                                    return syn("lone high surrogate");
                                }
                            } else if (0xDC00..0xE000).contains(&hi) {
                                return syn("lone low surrogate");
                            } else {
                                hi
                            };
                            out.push(char::from_u32(cp).ok_or_else(|| {
                                CodecError::Syntax("bad code point".to_string())
                            })?);
                        }
                        _ => return syn("bad escape"),
                    }
                }
                c if c < 0x20 => return syn("raw control character in string"),
                _ => {
                    // copy one UTF-8 scalar (the input is a &str, so it is valid UTF-8)
                    let start = self.pos;
                    self.pos += 1;
                    while self.pos < self.s.len() && (self.s[self.pos] & 0xC0) == 0x80 {
                        self.pos += 1;
                    }
                    out.push_str(
                        std::str::from_utf8(&self.s[start..self.pos]).map_err(|_| CodecError::Utf8)?,
                    );
                }
            }
        }
    }

    fn hex4(&mut self) -> Result<u32, CodecError> {
        let h = self.s.get(self.pos..self.pos + 4).ok_or(CodecError::Eof)?;
        let mut v = 0u32;
        for &c in h {
            let d = match c {
                b'0'..=b'9' => c - b'0',
                b'a'..=b'f' => c - b'a' + 10,
                b'A'..=b'F' => c - b'A' + 10,
                _ => return syn("bad \\u escape"),
            };
            v = (v << 4) | u32::from(d);
        }
        self.pos += 4;
        Ok(v)
    }

    fn number(&mut self) -> Result<Value, CodecError> {
        let start = self.pos;
        if self.peek() == Some(b'-') {
            self.pos += 1;
        }
        match self.peek() {
            Some(b'0') => self.pos += 1,
            Some(b'1'..=b'9') => {
                while matches!(self.peek(), Some(b'0'..=b'9')) {
                    self.pos += 1;
                }
            }
            _ => return syn("bad number"),
        }
        let mut is_float = false;
        if self.peek() == Some(b'.') {
            is_float = true;
            self.pos += 1;
            let d = self.pos;
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.pos += 1;
            }
            if self.pos == d {
                return syn("digits expected after '.'");
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            is_float = true;
            self.pos += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.pos += 1;
            }
            let d = self.pos;
            while matches!(self.peek(), Some(b'0'..=b'9')) {
                self.pos += 1;
            }
            if self.pos == d {
                return syn("digits expected in exponent");
            }
        }
        let text = std::str::from_utf8(&self.s[start..self.pos]).map_err(|_| CodecError::Utf8)?;
        if is_float {
            let f: f64 = text.parse().map_err(|_| CodecError::Syntax("bad float".into()))?;
            if !f.is_finite() {
                return syn("float out of range");
            }
            Ok(Value::Float(f))
        } else {
            text.parse::<i64>()
                .map(Value::Int)
                .map_err(|_| CodecError::Syntax("integer out of range".into()))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Value {
        Value::map([
            ("a", Value::Int(-5)),
            ("b", Value::Float(1.5)),
            ("c", Value::List(vec![Value::Null, Value::Bool(true), Value::str("x\"y\n")])),
            ("d", Value::Bytes(vec![1, 2, 255])),
            ("e", Value::Hash(Hash([7; 32]))),
            ("f", Value::Obj(ObjectId([9; 16]))),
            ("g", Value::Float(f64::NAN)),
            ("h", Value::Float(-0.0)),
            ("i", Value::Float(1e300)),
            ("j", Value::Float(5e-324)),
            ("k", Value::Int(i64::MIN)),
        ])
    }

    #[test]
    fn binary_round_trip() {
        let v = sample();
        let b = v.to_bytes();
        assert_eq!(Value::from_bytes(&b).unwrap(), v);
    }

    #[test]
    fn json_round_trip_and_canonical() {
        let v = sample();
        let j = v.to_json();
        assert_eq!(Value::from_json(&j).unwrap(), v);
        assert_eq!(Value::from_json(&j).unwrap().to_json(), j);
        assert!(j.starts_with("{\"a\":-5,\"b\":1.5,\"c\":[null,true,\"x\\\"y\\u000a\"]"));
    }

    #[test]
    fn float_formats_are_distinct_from_ints() {
        assert_eq!(Value::Float(2.0).to_json(), "2.0");
        assert_eq!(Value::Int(2).to_json(), "2");
        assert_eq!(Value::Float(-0.0).to_json(), "-0.0");
        assert_eq!(Value::Float(1e16).to_json(), "1e16");
        assert_eq!(Value::Float(0.1 + 0.2).to_json(), "0.30000000000000004");
        assert!(matches!(Value::from_json("2.0").unwrap(), Value::Float(_)));
        assert!(matches!(Value::from_json("2").unwrap(), Value::Int(2)));
    }

    #[test]
    fn rejects_non_canonical_binary() {
        // padded varint 0x80 0x00 for zero inside a string length
        assert!(Value::from_bytes(&[T_STR, 0x80, 0x00]).is_err());
        // unsorted map keys
        let mut b = vec![T_MAP, 2];
        b.extend_from_slice(&[1, b'b', T_NULL, 1, b'a', T_NULL]);
        assert!(Value::from_bytes(&b).is_err());
        // duplicate keys
        let mut b = vec![T_MAP, 2];
        b.extend_from_slice(&[1, b'a', T_NULL, 1, b'a', T_NULL]);
        assert!(Value::from_bytes(&b).is_err());
        // reserved key
        let mut b = vec![T_MAP, 1];
        b.extend_from_slice(&[2, b'$', b'b', T_NULL]);
        assert!(Value::from_bytes(&b).is_err());
        // trailing bytes
        assert_eq!(Value::from_bytes(&[T_NULL, T_NULL]), Err(CodecError::Trailing));
        // bad bool tag
        assert!(Value::from_bytes(&[11]).is_err());
    }

    #[test]
    fn rejects_bad_json() {
        for bad in [
            "", "{", "[1,]", "{\"a\":1,}", "01", "1.", ".5", "+1", "\"\\x\"", "{\"a\":1,\"a\":2}",
            "{\"$b\":\"zz\"}", "{\"$z\":\"00\"}", "{\"$b\":\"00\",\"x\":1}", "nul", "1 2",
            "99999999999999999999",
        ] {
            assert!(Value::from_json(bad).is_err(), "should reject {bad:?}");
        }
    }

    #[test]
    fn depth_limit() {
        let deep = "[".repeat(100) + &"]".repeat(100);
        assert_eq!(Value::from_json(&deep), Err(CodecError::TooDeep));
        let mut b = [T_LIST, 1].repeat(100);
        b.push(T_NULL);
        assert!(Value::from_bytes(&b).is_err());
    }

    #[test]
    fn surrogate_pairs() {
        let v = Value::from_json("\"\\ud83d\\ude00\"").unwrap();
        assert_eq!(v, Value::str("\u{1F600}"));
        assert!(Value::from_json("\"\\ud83d\"").is_err());
        assert!(Value::from_json("\"\\ude00\"").is_err());
    }

    #[test]
    fn nan_payload_is_preserved() {
        let weird = f64::from_bits(0x7ff8_0000_0000_1234);
        let v = Value::Float(weird);
        let back = Value::from_bytes(&v.to_bytes()).unwrap();
        match back {
            Value::Float(f) => assert_eq!(f.to_bits(), weird.to_bits()),
            _ => panic!(),
        }
        let back = Value::from_json(&v.to_json()).unwrap();
        match back {
            Value::Float(f) => assert_eq!(f.to_bits(), weird.to_bits()),
            _ => panic!(),
        }
    }
}
