//! wc2wing -- a third seat for Wing Commander II missions.
//!
//! The multiplayer hooks seat a second human in a fighter of the player's
//! wing; a mission with two such fighters at its launch nav point seats
//! two.  This tool gives a mission (of WC2.EXE, SO1.EXE, SO2.EXE or a
//! campaign built on them, such as SO3) a second wing fighter: a copy of
//! the first wingman's record (or of the player's, in a mission the story
//! flies alone), flown by the game's AI when no human takes it.
//!
//!   wc2wing list MODULE.00n [-m s1m0,s2m3 | -m all]
//!   wc2wing add  MODULE.00n -o OUT [-m ...] [--name NAME] [--pilot N] [--slot 3] [--gamedat DIR] [--force]
//!
//! The format (docs/so3.md, scripts/wc2data.py): a block container (a dword
//! of the file's size, dword entries with the offset in the low 24 bits and a
//! flag in the high byte, 0x20 = LZW compressed), seven tables indexed by
//! series * 4 + mission: header 24, nav points 10 x 101, objectives 8 x 64,
//! mission ships 16 x 60, mission name 40, series name 40, 222 unused.
//!
//! Slots at launch: the player's ship is slot 0, the other ships of the
//! launch nav point's list take slots 1, 2, ... in the list's order (a field
//! is not a ship and takes none; see `launch`).  The hooks give the second
//! client slot 3, and seat it in whichever slot holds a free wing fighter
//! when that one does not; the new ship is placed so that it takes slot 3
//! where it can.
use std::collections::HashSet;
use std::env;
use std::fs;
use std::process;

const STRIDES: [usize; 7] = [24, 1010, 512, 960, 40, 40, 222];
const NAV: usize = 101;
const SHIP: usize = 60;
const FIELD_CLASSES: [i16; 2] = [5, 6]; // asteroid and mine fields: not ships

// ---------------------------------------------------------------------------
// The block container

fn u32_at(d: &[u8], at: usize) -> u32 {
    u32::from_le_bytes([d[at], d[at + 1], d[at + 2], d[at + 3]])
}
fn i16_at(d: &[u8], at: usize) -> i16 {
    i16::from_le_bytes([d[at], d[at + 1]])
}
fn i32_at(d: &[u8], at: usize) -> i32 {
    i32::from_le_bytes([d[at], d[at + 1], d[at + 2], d[at + 3]])
}
fn put_i16(d: &mut [u8], at: usize, v: i16) {
    d[at..at + 2].copy_from_slice(&v.to_le_bytes());
}
fn put_i32(d: &mut [u8], at: usize, v: i32) {
    d[at..at + 4].copy_from_slice(&v.to_le_bytes());
}

/// The LZW of the compressed blocks: codes of 9 to 12 bits, LSB first, 256
/// resets the dictionary, 257 ends.
fn lzw_decode(src: &[u8], expect: usize) -> Result<Vec<u8>, String> {
    let mut out: Vec<u8> = Vec::with_capacity(expect);
    let mut dict: Vec<Vec<u8>> = Vec::new();
    let reset = |dict: &mut Vec<Vec<u8>>| {
        dict.clear();
        for i in 0..256u32 {
            dict.push(vec![i as u8]);
        }
        dict.push(Vec::new());
        dict.push(Vec::new());
    };
    reset(&mut dict);
    let mut nbits = 9usize;
    let mut prev: Option<Vec<u8>> = None;
    let mut bitpos = 0usize;
    let total = src.len() * 8;
    while bitpos + nbits <= total {
        let mut code = 0usize;
        for i in 0..nbits {
            let b = bitpos + i;
            code |= (((src[b >> 3] >> (b & 7)) & 1) as usize) << i;
        }
        bitpos += nbits;
        if code == 256 {
            reset(&mut dict);
            nbits = 9;
            prev = None;
            continue;
        }
        if code == 257 {
            break;
        }
        let entry: Vec<u8> = if code < dict.len() {
            dict[code].clone()
        } else if code == dict.len() && prev.is_some() {
            let p = prev.as_ref().unwrap();
            let mut e = p.clone();
            e.push(p[0]);
            e
        } else {
            return Err(format!("bad LZW code {} at bit {}", code, bitpos));
        };
        out.extend_from_slice(&entry);
        if let Some(p) = &prev {
            let mut n = p.clone();
            n.push(entry[0]);
            dict.push(n);
        }
        prev = Some(entry);
        if dict.len() == (1 << nbits) && nbits < 12 {
            nbits += 1;
        }
        if out.len() >= expect {
            break;
        }
    }
    Ok(out)
}

struct Block {
    flag: u8,
    data: Vec<u8>,
}

fn read_container(d: &[u8]) -> Result<Vec<Block>, String> {
    if d.len() < 8 {
        return Err("not a block container (too short)".into());
    }
    let first = (u32_at(d, 4) & 0xFFFFFF) as usize;
    if first < 8 || first > d.len() || !(first - 4).is_multiple_of(4) {
        return Err("not a block container (bad table)".into());
    }
    let n = (first - 4) / 4;
    let entries: Vec<u32> = (0..n).map(|i| u32_at(d, 4 + 4 * i)).collect();
    let mut out = Vec::new();
    for i in 0..n {
        let off = (entries[i] & 0xFFFFFF) as usize;
        let flag = (entries[i] >> 24) as u8;
        let next = if i + 1 < n { (entries[i + 1] & 0xFFFFFF) as usize } else { d.len() };
        if off > next || next > d.len() {
            return Err(format!("block {} runs outside the file", i));
        }
        let mut block = d[off..next].to_vec();
        if flag == 0x20 {
            let expect = u32_at(&block, 0) as usize;
            block = lzw_decode(&block[4..], expect)?;
        }
        out.push(Block { flag, data: block });
    }
    Ok(out)
}

/// The file for the blocks; compressed blocks are written plain (the game
/// reads either).
fn compose_container(blocks: &[Block]) -> Vec<u8> {
    let mut off = 4 + 4 * blocks.len();
    let mut table = Vec::new();
    for b in blocks {
        let flag = if b.flag == 0x20 { 0xE0u32 } else { b.flag as u32 };
        table.push((off as u32) | (flag << 24));
        off += b.data.len();
    }
    let mut out = Vec::with_capacity(off);
    out.extend_from_slice(&(off as u32).to_le_bytes());
    for e in table {
        out.extend_from_slice(&e.to_le_bytes());
    }
    for b in blocks {
        out.extend_from_slice(&b.data);
    }
    out
}

// ---------------------------------------------------------------------------
// MODULE.00n

struct Module {
    blocks: Vec<Block>,
}

fn cstr(d: &[u8]) -> String {
    let end = d.iter().position(|&c| c == 0).unwrap_or(d.len());
    d[..end].iter().map(|&c| c as char).collect()
}

impl Module {
    fn load(path: &str) -> Result<Module, String> {
        let d = fs::read(path).map_err(|e| format!("{}: {}", path, e))?;
        let blocks = read_container(&d).map_err(|e| format!("{}: {}", path, e))?;
        if blocks.len() != 7 {
            return Err(format!("{}: {} blocks, a MODULE file has 7", path, blocks.len()));
        }
        for (i, b) in blocks.iter().enumerate() {
            if b.data.len() < STRIDES[i] * 4 {
                return Err(format!("{}: block {} is {} bytes, too short for a MODULE table", path, i, b.data.len()));
            }
        }
        Ok(Module { blocks })
    }
    fn save(&self, path: &str) -> Result<(), String> {
        fs::write(path, compose_container(&self.blocks)).map_err(|e| format!("{}: {}", path, e))
    }
    fn missions(&self) -> usize {
        self.blocks[0].data.len() / 24
    }
    fn table(&self, t: usize, idx: usize) -> &[u8] {
        &self.blocks[t].data[idx * STRIDES[t]..(idx + 1) * STRIDES[t]]
    }
    fn table_mut(&mut self, t: usize, idx: usize) -> &mut [u8] {
        &mut self.blocks[t].data[idx * STRIDES[t]..(idx + 1) * STRIDES[t]]
    }
    fn header(&self, idx: usize) -> Vec<i16> {
        (0..12).map(|i| i16_at(self.table(0, idx), 2 * i)).collect()
    }
    fn mission_name(&self, idx: usize) -> String {
        cstr(self.table(4, idx))
    }
    fn nav(&self, idx: usize, n: usize) -> &[u8] {
        &self.table(1, idx)[n * NAV..(n + 1) * NAV]
    }
    fn nav_mut(&mut self, idx: usize, n: usize) -> &mut [u8] {
        &mut self.table_mut(1, idx)[n * NAV..(n + 1) * NAV]
    }
    fn ship(&self, idx: usize, s: usize) -> &[u8] {
        &self.table(3, idx)[s * SHIP..(s + 1) * SHIP]
    }
    fn ship_mut(&mut self, idx: usize, s: usize) -> &mut [u8] {
        &mut self.table_mut(3, idx)[s * SHIP..(s + 1) * SHIP]
    }
}

fn nav_ships(nav: &[u8]) -> Vec<i16> {
    (0..10).map(|i| i16_at(nav, 0x51 + 2 * i)).filter(|&s| s >= 0).collect()
}
fn set_nav_ships(nav: &mut [u8], ships: &[i16]) {
    for i in 0..10 {
        put_i16(nav, 0x51 + 2 * i, if i < ships.len() { ships[i] } else { -1 });
    }
}
fn ship_class(r: &[u8]) -> i16 {
    i16_at(r, 0x15)
}
fn is_field(r: &[u8]) -> bool {
    FIELD_CLASSES.contains(&ship_class(r))
}

/// What the SHIP.Vnn file says a type is: 0xC a fighter, 0xD a capital
/// ship, 0xE a base.  None without a game directory or the file.
fn ship_kind(gamedat: &Option<String>, shiptype: u8) -> Option<u16> {
    let dir = gamedat.as_ref()?;
    for name in [format!("SHIP.V{:02}", shiptype), format!("ship.v{:02}", shiptype)] {
        let p = format!("{}/{}", dir, name);
        if let Ok(d) = fs::read(&p) {
            if let Ok(blocks) = read_container(&d) {
                // a fighter's record is packet 3; a capital ship's block 38
                for b in [blocks.get(3), blocks.get(38)].into_iter().flatten() {
                    if b.data.len() >= 26 {
                        let kind = i16_at(&b.data, 20) as u16;
                        if (0xC..=0xE).contains(&kind) {
                            return Some(kind);
                        }
                    }
                }
            }
        }
    }
    None
}

struct Launch {
    nav: usize,
    list: Vec<i16>,
    slots: Vec<(i16, i32)>, // (mission ship, slot or -1 for a field)
}

/// The launch nav point (the one whose list has the player's ship, else nav
/// 0) and the slot each ship takes there.  Seen in the game (SO2.EXE, the
/// slot table dumped in flight): the player's ship is slot 0 wherever it is
/// listed; the wing ships the list leaves out are spawned before the list
/// (SO2's s2m1 lists neither the player nor Stingray: she took slot 1 ahead
/// of the Concordia); the list's other ships take the slots in order
/// (s1m1: Concordia, player, Stingray gives the Concordia slot 1 and
/// Stingray slot 2); a field takes none.
fn launch(m: &Module, idx: usize, player: i16, wing: &[i16]) -> Launch {
    let mut nav = 0;
    for n in 0..10 {
        if nav_ships(m.nav(idx, n)).contains(&player) {
            nav = n;
            break;
        }
    }
    let list = nav_ships(m.nav(idx, nav));
    let mut slots = Vec::new();
    let mut next = 1;
    if !list.contains(&player) {
        // (seen once, SO2's s2m1: the player and the wing unlisted, the wing
        // spawned ahead of the list; a wing ship unlisted while the player is
        // listed waits at its own nav point, as SO3's Toro does)
        slots.push((player, 0));
        for &w in wing {
            if w != player && !list.contains(&w) && !is_field(m.ship(idx, w as usize)) {
                slots.push((w, next));
                next += 1;
            }
        }
    }
    for &s in &list {
        if s == player {
            slots.push((s, 0));
        } else if is_field(m.ship(idx, s as usize)) {
            slots.push((s, -1));
        } else {
            slots.push((s, next));
            next += 1;
        }
    }
    Launch { nav, list, slots }
}

/// A fighter of the player's wing a human can fly: in the header's wing
/// list, of our side, in formation (behaviour 4), and a fighter by its SHIP
/// file where the game directory is at hand.
fn is_wing_fighter(m: &Module, idx: usize, s: i16, wing: &[i16], gamedat: &Option<String>) -> bool {
    if !wing.contains(&s) {
        return false;
    }
    let r = m.ship(idx, s as usize);
    if i16_at(r, 0x17) != 0 || i16_at(r, 0x1b) != 4 || is_field(r) {
        return false;
    }
    match ship_kind(gamedat, r[0x14]) {
        Some(kind) => kind == 0xC,
        None => true,
    }
}

fn describe_ship(m: &Module, idx: usize, s: i16) -> String {
    let r = m.ship(idx, s as usize);
    format!("{} \"{}\" type {} side {} behaviour {} formation {} orders {} pilot {:#04x}", s, cstr(&r[..19]), r[0x14], i16_at(r, 0x17),
            i16_at(r, 0x1b), r[0x30], i16_at(r, 0x35), r[0x3b])
}

/// Mission ship records nothing refers to: not the player's, not in the wing
/// list, in no nav point's list, nobody's leader or objective.
fn free_records(m: &Module, idx: usize, header: &[i16]) -> (Vec<usize>, Vec<usize>) {
    let mut referenced: HashSet<i16> = HashSet::new();
    for n in 0..10 {
        for s in nav_ships(m.nav(idx, n)) {
            referenced.insert(s);
        }
    }
    for &w in &header[1..] {
        if w >= 0 {
            referenced.insert(w);
        }
    }
    for s in 0..16 {
        let r = m.ship(idx, s);
        if referenced.contains(&(s as i16)) {
            for at in [0x38usize, 0x3a] {
                if r[at] != 0xFF {
                    referenced.insert(r[at] as i16);
                }
            }
        }
    }
    let mut zero = Vec::new();
    let mut unreferenced = Vec::new();
    for s in 0..16 {
        let r = m.ship(idx, s);
        if r.iter().all(|&b| b == 0) {
            zero.push(s);
        } else if !referenced.contains(&(s as i16)) {
            unreferenced.push(s);
        }
    }
    (zero, unreferenced)
}

fn parse_missions(arg: &str, m: &Module) -> Result<Vec<usize>, String> {
    if arg == "all" {
        return Ok((0..m.missions()).filter(|&idx| m.header(idx)[3] >= 0).collect());
    }
    let mut out = Vec::new();
    for part in arg.split(',') {
        let p = part.trim().to_lowercase();
        // s1m0, 1-0, 1/0
        let digits: Vec<usize> = p.split(|c: char| !c.is_ascii_digit()).filter(|x| !x.is_empty()).map(|x| x.parse().unwrap()).collect();
        if digits.len() != 2 || digits[1] > 3 {
            return Err(format!("mission \"{}\": write it as s1m0 (series 1, mission 0)", part));
        }
        let idx = digits[0] * 4 + digits[1];
        if idx >= m.missions() {
            return Err(format!("mission \"{}\": the file has {} series", part, m.missions() / 4));
        }
        out.push(idx);
    }
    Ok(out)
}

fn slot_name(idx: usize) -> String {
    format!("s{}m{}", idx / 4, idx % 4)
}

// ---------------------------------------------------------------------------
// The commands

fn list(m: &Module, missions: &[usize], gamedat: &Option<String>) {
    for &idx in missions {
        let h = m.header(idx);
        let player = h[3];
        let wing: Vec<i16> = h[4..].iter().cloned().filter(|&w| w >= 0).collect();
        println!("{} {}: launch from {}, land on {}", slot_name(idx), m.mission_name(idx), h[1], h[2]);
        if player < 0 {
            println!("    (no player ship: an empty slot)");
            continue;
        }
        println!("    player: {}", describe_ship(m, idx, player));
        for &w in &wing {
            println!("    wing:   {}{}", describe_ship(m, idx, w), if is_wing_fighter(m, idx, w, &wing, gamedat) { "  [a seat]" } else { "" });
        }
        let l = launch(m, idx, player, &wing);
        println!("    launch nav {} \"{}\": {} ships listed", l.nav, cstr(&m.nav(idx, l.nav)[..20]), l.list.len());
        for &(s, slot) in &l.slots {
            let r = m.ship(idx, s as usize);
            let what = if slot < 0 { "a field, no slot".to_string() } else { format!("slot {}", slot) };
            println!("      {:<16} {} \"{}\" type {}{}{}", what, s, cstr(&r[..19]), r[0x14],
                     if !l.list.contains(&s) { "  (of the wing, not listed: spawned first)" } else { "" },
                     if slot > 0 && is_wing_fighter(m, idx, s, &wing, gamedat) { "  <- a seat for a human" } else { "" });
        }
        let seats = l.slots.iter().filter(|&&(s, slot)| slot > 0 && is_wing_fighter(m, idx, s, &wing, gamedat)).count();
        let (zero, unref) = free_records(m, idx, &h);
        println!("    seats for humans beside the player: {}; free records: {} empty, {} unreferenced", seats, zero.len(), unref.len());
    }
}

struct AddOpts {
    name: Option<String>,
    pilot: Option<u8>,
    slot: i32,
    force: bool,
    make_room: bool,
    gamedat: Option<String>,
}

/// Room for one more ship: the enemy follower (side 1, in formation behind a
/// leader) with the highest index that nothing else refers to -- no other
/// record's leader or objective, no objective's subject -- is taken out of
/// the mission (its record zeroed, its index out of every nav point's list).
/// With `launch` the follower must be in that nav point's list (the list is
/// full).  The text says what went.
fn make_room(m: &mut Module, idx: usize, launch_nav: Option<usize>) -> Result<String, String> {
    let h = m.header(idx);
    let mut referenced: HashSet<i16> = HashSet::new();
    for &w in &h[1..] {
        if w >= 0 {
            referenced.insert(w);
        }
    }
    for s in 0..16 {
        let r = m.ship(idx, s);
        for at in [0x38usize, 0x3a] {
            if r[at] != 0xFF {
                referenced.insert(r[at] as i16);
            }
        }
    }
    for o in 0..8 {
        let obj = &m.table(2, idx)[o * 64..(o + 1) * 64];
        let (kind, param) = (i16_at(obj, 0), i16_at(obj, 2));
        if kind < 0 {
            break;
        }
        if kind != 0 && kind != 5 {
            referenced.insert(param);
        }
    }
    let listed: Vec<i16> = match launch_nav {
        Some(n) => nav_ships(m.nav(idx, n)),
        None => Vec::new(),
    };
    for s in (0..16).rev() {
        let r = m.ship(idx, s);
        let follower = i16_at(r, 0x17) == 1 && i16_at(r, 0x1b) == 4 && r[0x38] != 0xFF && !is_field(r) && r.iter().any(|&b| b != 0);
        if !follower || referenced.contains(&(s as i16)) || (launch_nav.is_some() && !listed.contains(&(s as i16))) {
            continue;
        }
        let name = cstr(&r[..19]);
        let leader = r[0x38];
        for n in 0..10 {
            let ships: Vec<i16> = nav_ships(m.nav(idx, n)).into_iter().filter(|&x| x != s as i16).collect();
            set_nav_ships(m.nav_mut(idx, n), &ships);
        }
        for b in m.ship_mut(idx, s).iter_mut() {
            *b = 0;
        }
        return Ok(format!("mission ship {} \"{}\" (a follower of {}) taken out to make room", s, name, leader));
    }
    Err(match launch_nav {
        Some(n) => format!("the launch nav point's list is full (ten ships) and no enemy follower in it can go: drop one from nav {} first", n),
        None => "all sixteen mission ship records are in use and no enemy follower can go: drop one first".to_string(),
    })
}

/// The mission's second wing fighter.  Ok(Some(text)) when something was
/// added, Ok(None) when the mission had its seats already, Err when it
/// cannot be done.
fn add(m: &mut Module, idx: usize, o: &AddOpts) -> Result<Option<String>, String> {
    let h = m.header(idx);
    let player = h[3];
    if player < 0 {
        return Err("no player ship (an empty slot)".into());
    }
    let wing: Vec<i16> = h[4..].iter().cloned().filter(|&w| w >= 0).collect();
    let l = launch(m, idx, player, &wing);
    let seats: Vec<(i16, i32)> = l.slots.iter().cloned().filter(|&(s, slot)| slot > 0 && is_wing_fighter(m, idx, s, &wing, &o.gamedat)).collect();
    if seats.len() >= 2 && !o.force {
        return Ok(None);
    }
    // the template: the first wing fighter at launch, else the player's ship
    let template = seats.first().map(|&(s, _)| s).unwrap_or(player);
    if wing.len() >= 8 {
        return Err("the wing list is full (eight ships)".into());
    }
    let mut notes = Vec::new();
    // a record for it
    let (mut zero, mut unref) = free_records(m, idx, &h);
    if zero.is_empty() && unref.is_empty() {
        if !o.make_room {
            return Err("all sixteen mission ship records are in use: drop one first, or --make-room".into());
        }
        notes.push(make_room(m, idx, None)?);
        let again = free_records(m, idx, &h);
        zero = again.0;
        unref = again.1;
    }
    let rec = match zero.first().or(unref.first()) {
        Some(&r) => r,
        None => return Err("no record came free".into()),
    };
    let mut l = l;
    if l.list.len() >= 10 {
        if !o.make_room {
            return Err(format!("the launch nav point's list is full (ten ships): drop one from nav {} first, or --make-room", l.nav));
        }
        notes.push(make_room(m, idx, Some(l.nav))?);
        l = launch(m, idx, player, &wing);
    }
    // the record: the template's, in the next formation place, a little off
    // to one side and behind, flown by an ordinary pilot (the hooks hold the
    // AI off a human's ship)
    let mut r = m.ship(idx, template as usize).to_vec();
    let tname = cstr(&r[..19]);
    let name = o.name.clone().unwrap_or_else(|| format!("{} 2", tname));
    let nb: Vec<u8> = name.chars().map(|c| if (c as u32) < 256 { c as u8 } else { b'?' }).take(19).collect();
    for b in r[..19].iter_mut() {
        *b = 0;
    }
    r[..nb.len()].copy_from_slice(&nb);
    put_i16(&mut r, 0x19, 1); // important: kept per slot like the wing's
    put_i16(&mut r, 0x1b, 4); // in formation
    let formation = (1u8..=15).find(|&f| !wing.iter().any(|&w| m.ship(idx, w as usize)[0x30] == f)).unwrap_or(15);
    r[0x30] = formation;
    put_i16(&mut r, 0x35, 4); // an ordinary pilot's orders (6 is the player, 5 the first wingman)
    let leader = if template == player { player as u8 } else { if r[0x38] == 0xFF { player as u8 } else { r[0x38] } };
    r[0x38] = leader;
    r[0x39] = 0;
    r[0x3a] = leader;
    if let Some(p) = o.pilot {
        r[0x3b] = p;
    }
    r[0x37] = 0;
    // 500 km to the left of the template's place and 1000 km behind it
    let (x, z) = (i32_at(&r, 0x1e), i32_at(&r, 0x26));
    put_i32(&mut r, 0x1e, x - 500 * 256);
    put_i32(&mut r, 0x26, z + 1000 * 256);
    m.ship_mut(idx, rec).copy_from_slice(&r);
    // the header's wing list
    {
        let hdr = m.table_mut(0, idx);
        let at = (4..12).find(|&i| i16_at(hdr, 2 * i) < 0).unwrap();
        put_i16(hdr, 2 * at, rec as i16);
    }
    // the launch list: before the listed ship that would take the wanted slot
    let mut list = l.list.clone();
    let mut at = list.len();
    for (i, s) in list.iter().enumerate() {
        if l.slots.iter().any(|&(x, slot)| x == *s && slot == o.slot) {
            at = i;
            break;
        }
    }
    list.insert(at, rec as i16);
    set_nav_ships(m.nav_mut(idx, l.nav), &list);
    let after = launch(m, idx, player, &wing);
    let got = after.slots.iter().find(|&&(s, _)| s == rec as i16).map(|&(_, slot)| slot).unwrap_or(-1);
    let mut text = format!("{} added as mission ship {} (a copy of {} \"{}\", formation {}, pilot {:#04x}), slot {} at launch",
                           name, rec, template, tname, formation, r[0x3b], got);
    if got != o.slot {
        text.push_str(&format!(" (not {}: the launch list is shorter; the hooks seat the client in it all the same)", o.slot));
    }
    if unref.contains(&rec) {
        text.push_str(&format!("; record {} was unreferenced and is reused", rec));
    }
    for n in notes {
        text.push_str("; ");
        text.push_str(&n);
    }
    Ok(Some(text))
}

fn usage() -> ! {
    eprintln!("wc2wing: a second fighter of the player's wing in Wing Commander II missions, a seat for a third player\n\
              \n  wc2wing list MODULE.00n [-m s1m0,s2m3 | -m all] [--gamedat DIR]\
              \n  wc2wing add  MODULE.00n -o OUT [-m ...] [--name NAME] [--pilot N] [--slot N] [--gamedat DIR] [--make-room] [--force]\
              \n\n  -m         the missions (default all); s1m0 is series 1, mission 0\
              \n  --name     the new ship's name (default: the template's with \" 2\")\
              \n  --pilot    its comm personality (COMMUNIC.Tnn, decimal or 0x..; default: the template's)\
              \n  --slot     the slot it should take at launch (default 3, the hooks' second client)\
              \n  --gamedat  the game's GAMEDAT directory, to tell fighters from capital ships by their SHIP files\
              \n  --make-room  where the sixteen records or the launch list are full, take out the enemy follower\
              \n             with the highest index that nothing refers to (the text says which)\
              \n  --force    add one even where two wing fighters fly already");
    process::exit(2)
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.len() < 2 {
        usage();
    }
    let cmd = args[0].as_str();
    let path = args[1].as_str();
    let mut missions = "all".to_string();
    let mut out: Option<String> = None;
    let mut o = AddOpts { name: None, pilot: None, slot: 3, force: false, make_room: false, gamedat: None };
    let mut i = 2;
    let need = |i: usize, args: &[String]| -> String {
        if i + 1 >= args.len() {
            usage();
        }
        args[i + 1].clone()
    };
    while i < args.len() {
        match args[i].as_str() {
            "-m" | "--missions" => { missions = need(i, &args); i += 1; }
            "-o" | "--out" => { out = Some(need(i, &args)); i += 1; }
            "--name" => { o.name = Some(need(i, &args)); i += 1; }
            "--pilot" => {
                let v = need(i, &args);
                o.pilot = Some(if let Some(h) = v.strip_prefix("0x") { u8::from_str_radix(h, 16) } else { v.parse() }.unwrap_or_else(|_| usage()));
                i += 1;
            }
            "--slot" => { o.slot = need(i, &args).parse().unwrap_or_else(|_| usage()); i += 1; }
            "--gamedat" => { o.gamedat = Some(need(i, &args)); i += 1; }
            "--force" => o.force = true,
            "--make-room" => o.make_room = true,
            _ => usage(),
        }
        i += 1;
    }
    let mut m = match Module::load(path) {
        Ok(m) => m,
        Err(e) => { eprintln!("wc2wing: {}", e); process::exit(1); }
    };
    let sel = match parse_missions(&missions, &m) {
        Ok(s) => s,
        Err(e) => { eprintln!("wc2wing: {}", e); process::exit(2); }
    };
    match cmd {
        "list" => list(&m, &sel, &o.gamedat),
        "add" => {
            let out = out.unwrap_or_else(|| { eprintln!("wc2wing: add needs -o OUT (the file to write)"); process::exit(2) });
            let mut failed = 0;
            for idx in sel {
                match add(&mut m, idx, &o) {
                    Ok(Some(text)) => println!("{} {}: {}", slot_name(idx), m.mission_name(idx), text),
                    Ok(None) => println!("{} {}: two wing fighters fly already, nothing added", slot_name(idx), m.mission_name(idx)),
                    Err(e) => { println!("{} {}: NOT DONE: {}", slot_name(idx), m.mission_name(idx), e); failed += 1; }
                }
            }
            if let Err(e) = m.save(&out) {
                eprintln!("wc2wing: {}", e);
                process::exit(1);
            }
            println!("written: {}", out);
            if failed > 0 {
                eprintln!("wc2wing: {} mission(s) not done", failed);
                process::exit(1);
            }
        }
        _ => usage(),
    }
}
