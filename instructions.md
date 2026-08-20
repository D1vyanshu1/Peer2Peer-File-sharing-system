# The Complete Guide to This P2P File Sharing System

This document teaches the project from the ground up. By the end you should be
able to (a) understand every component and why it exists, (b) explain the system
confidently in an interview, and (c) rebuild it from scratch. It is long on
purpose — treat it as a book with chapters, not a cheat sheet.

**Table of contents**

1. [What problem are we solving?](#1-what-problem-are-we-solving)
2. [Core concepts (BitTorrent vocabulary)](#2-core-concepts-bittorrent-vocabulary)
3. [System overview & the two planes](#3-system-overview--the-two-planes)
4. [Module map — every file and its job](#4-module-map--every-file-and-its-job)
5. [The wire protocol](#5-the-wire-protocol)
6. [The tracker in depth](#6-the-tracker-in-depth)
7. [Durability: the write-ahead journal](#7-durability-the-write-ahead-journal)
8. [Replication & high availability](#8-replication--high-availability)
9. [Security: password hashing](#9-security-password-hashing)
10. [The client and the download engine](#10-the-client-and-the-download-engine)
11. [The concurrency model, precisely](#11-the-concurrency-model-precisely)
12. [End-to-end walkthrough of a download](#12-end-to-end-walkthrough-of-a-download)
13. [How to run and experiment](#13-how-to-run-and-experiment)
14. [Rebuild-from-scratch checklist](#14-rebuild-from-scratch-checklist)
15. [Interview Q&A](#15-interview-qa)
16. [Limitations & future work](#16-limitations--future-work)
17. [Glossary](#17-glossary)

---

## 1. What problem are we solving?

Imagine a group of people who want to share large files. The naive approach is a
**central file server**: everyone uploads to it and everyone downloads from it.
That server is a bottleneck (its upload bandwidth is shared across all
downloaders) and a single point of failure.

**Peer-to-peer (P2P)** flips this around. The people who *have* the file (or
even parts of it) serve it to the people who *want* it. The more peers there
are, the more total bandwidth exists. This is how BitTorrent distributes Linux
ISOs to millions of people without a giant server farm.

But pure P2P has a coordination problem: how does a downloader *find* peers that
have the file? That's the job of a **tracker** — a small, central directory that
knows which peers hold which files. Crucially, **the tracker never touches file
data**; it only stores metadata (file → list of peers). The heavy lifting
happens directly between peers.

This project implements exactly that: a tracker for coordination, and clients
that are simultaneously downloaders *and* servers.

---

## 2. Core concepts (BitTorrent vocabulary)

You should be able to define each of these on a whiteboard:

- **Chunk (piece)** — a file is split into fixed-size blocks (here **512 KiB**).
  Chunks are the unit of transfer and verification. Splitting is what enables
  parallelism: different chunks can come from different peers simultaneously.
- **Swarm** — all the peers involved with a particular file.
- **Seeder** — a peer that has the *complete* file and only uploads.
- **Leecher** — a peer that is still downloading (and, in a good design, also
  uploading the chunks it already has).
- **Bitfield** — a compact map of which chunks a peer currently holds. When you
  connect to a peer you ask for its bitfield so you know what you can request.
- **Rarest-piece-first** — a scheduling strategy: among the chunks you still
  need, download the one that the *fewest* peers have, first. This spreads scarce
  chunks through the swarm quickly and avoids a situation where the last copy of
  a chunk disappears when a peer leaves.
- **Chunk hashing** — each chunk has a known cryptographic hash (SHA-256). After
  downloading a chunk you recompute its hash and compare. If it doesn't match,
  the chunk is corrupt or the peer is malicious — you discard and refetch.
- **Announce / discover** — a peer *announces* to the tracker that it has a file;
  a downloader asks the tracker to *discover* who has it.

---

## 3. System overview & the two planes

The system has two clearly separated communication paths ("planes"):

**Control plane — client ↔ tracker.** Small request/response messages:
`login`, `create_group`, `upload_file` (announce), `download_file` (discover),
etc. This is where identity and coordination live.

**Data plane — client ↔ client.** The actual bytes. A downloader connects
*directly* to peer clients and requests chunks. The tracker is not involved, so
throughput is not limited by the tracker.

Every client therefore runs two roles at once:
- a **PeerServer** thread that listens for chunk requests from others (upload
  side), and
- on demand, a **ChunkDownloader** that connects out to peers and pulls chunks
  (download side).

The tracker runs:
- a **TrackerServer** accept loop that services client control-plane connections,
- a **PeerReplicator** that keeps a second tracker in sync,
- an in-memory **TrackerState** backed by a **journal** on disk.

---

## 4. Module map — every file and its job

### `common/` — shared library (`p2pcommon`)

| File | Responsibility |
|------|----------------|
| `p2p/Logger.{h,cpp}` | Thread-safe, leveled logging (`LOG_INFO`, …). One mutex so lines don't interleave. |
| `p2p/Config.{h,cpp}` | Parse `key = value` config files. Replaces hard-coded constants. |
| `p2p/Sha256.{h,cpp}` | SHA-256 hex digests via OpenSSL's EVP API. Used for chunk & file integrity. |
| `p2p/Crypto.{h,cpp}` | Password hashing: PBKDF2-HMAC-SHA256 with random salt; constant-time verify. |
| `p2p/Message.{h,cpp}` | The structured, versioned, binary-safe message envelope (type + headers + body). |
| `p2p/Connection.{h,cpp}` | RAII TCP socket with **length-prefixed framing** and partial-IO-safe send/recv. |
| `p2p/ThreadPool.{h,cpp}` | Fixed worker pool with a blocking task queue. Used by the downloader. |
| `p2p/Protocol.h` | All message-type and header-key string constants (single source of truth). |

### `tracker/`

| File | Responsibility |
|------|----------------|
| `TrackerState.{h,cpp}` | The authoritative model (users/groups/files), transactional ops, event application, journal. |
| `CommandHandler.{h,cpp}` | Translate a client `Message` → `TrackerState` operation → response. Pure logic, no sockets. |
| `PeerReplication.{h,cpp}` | Tracker-to-tracker replication: link management, event forwarding, snapshot resync. |
| `TrackerServer.{h,cpp}` | Accept loop; dispatches peer-tracker handshakes vs client sessions. |
| `main.cpp` | Wire everything together from config + index argument. |

### `client/`

| File | Responsibility |
|------|----------------|
| `FileMeta.{h,cpp}` | Split a local file into chunks and compute per-chunk + full SHA-256. |
| `FileRegistry.{h,cpp}` | Thread-safe catalogue of files this client can serve (+ its per-chunk bitfield). |
| `TrackerClient.{h,cpp}` | Control-plane client with automatic tracker **failover** and re-login. |
| `PeerServer.{h,cpp}` | Serve `GET_BITFIELD` / `GET_CHUNK` to other peers (keep-alive, framed). |
| `RarestFirst.h` | The pure rarest-first selection function (header-only, unit-tested). |
| `ChunkDownloader.{h,cpp}` | **The engine**: parallel, rarest-first, scored, verified, resumable download. |
| `main.cpp` | Interactive shell tying it all together. |

The dependency direction is strict: `client` and `tracker` depend on `common`;
`common` depends on nothing but the standard library + OpenSSL. This is what lets
the business logic (`CommandHandler`, `RarestFirst`) be unit-tested without
opening a socket.

---

## 5. The wire protocol

### The problem with the original

The original project sent commands as space/semicolon-delimited text and parsed
them with `istringstream >> a >> b >> c`. Two fatal weaknesses:

1. **Ambiguity.** A filename containing a space (`my movie.mp4`) breaks parsing.
2. **No message boundaries.** TCP is a *byte stream*, not a message stream. A
   single `recv()` may return half a message or two messages glued together. The
   original assumed one `recv()` == one message, which is simply a bug.

### Framing: how we find message boundaries

Every message is sent as a **length-prefixed frame**:

```
+-----------------------------+-----------------------------------+
| 4-byte length (network order)|  payload (exactly `length` bytes) |
+-----------------------------+-----------------------------------+
```

The receiver first reads exactly 4 bytes to learn the length `N`, then reads
exactly `N` bytes. Because we always read the *exact* number of bytes (looping
over `recv` until satisfied — see `Connection::recvAll`), boundaries are never
ambiguous. `Connection::kMaxFrame` caps `N` so a bad peer can't request a
gigabyte allocation.

### The payload: a structured envelope

Inside the frame, the payload is an HTTP-like envelope (`p2p::Message`):

```
UPLOAD_FILE\n                 ← message TYPE (first line)
v: 1\n                        ← protocol version (added automatically)
gid: movies\n                 ← headers: "key: value", one per line
filename: my movie.mp4\n      ← values MAY contain spaces
size: 5242880\n
\n                            ← blank line terminates the header block
<raw body bytes...>           ← optional binary-safe payload (e.g. a 512KB chunk)
```

Key properties:
- **Header values can contain spaces** (everything after `": "` up to the
  newline), fixing weakness #1.
- The **body is binary-safe**: its length is `frame_length − header_length`, so
  it can contain any bytes, including `\0` and even `\n\n`. This is why we can
  ship a raw file chunk as the body of a `CHUNK` message.
- A **version header** (`v`) rides on every message, so the protocol can evolve.

`Message::parse` splits header from body at the *first* blank line only, so a
body that itself contains `\n\n` is preserved intact (there's a unit test for
exactly this).

**Why not JSON/Protobuf?** They'd work and are mentioned as alternatives. This
hand-rolled envelope was chosen to keep the project dependency-light and to make
the framing logic explicit and teachable. Swapping the serializer would touch
only `Message`.

---

## 6. The tracker in depth

### The data model (`TrackerState`)

```
User   { name, passwordHash, ip, port, online }
Group  { id, owner, members{…}, pending{…}, files{ filename -> FileInfo } }
FileInfo { filename, size, fullHash, chunkHashes[], owners{usernames} }
```

- The tracker stores **metadata only**. `FileInfo` knows the chunk hashes and
  which *users* own the file, but never the bytes.
- "Owners" are usernames. When a downloader asks for a file, the tracker resolves
  each owner to its `ip:port` **and filters to those currently online** — you're
  never handed a peer you can't reach.

### Events: the single way state changes

Instead of mutating structures ad hoc, every change is expressed as an **event**
(a `Message` with a type like `EV_GROUP_CREATE`). This one idea buys three
features at once:

1. **Persistence** — durable events are appended to the journal (§7).
2. **Replication** — locally-produced events are forwarded to the peer tracker (§8).
3. **Idempotent merge** — `applyEvent` is written so replaying or re-receiving an
   event is harmless (creating an existing group is a no-op, adding an existing
   owner is a set-insert, etc.). This is what makes both journal replay and
   snapshot resync safe.

The flow for a local mutation (inside `TrackerState`, under one mutex):

```
validate  →  build event  →  commitLocal(event):
                                applyEvent(event)          // mutate memory
                                if durable: journalAppend  // persist
                                sink(event)                // hand to replicator
```

Events arriving *from* the peer tracker go through `applyRemoteEvent`, which
applies + journals but **does not** call the sink — that's what prevents an
infinite replication loop.

Ephemeral events (`LOGIN`, `LOGOUT`) are replicated (so both trackers know who is
online) but **not** journaled (online status shouldn't survive a restart).

### Command handling & sessions

`CommandHandler` is deliberately socket-free: it takes a decoded `Message` plus a
`ClientSession` (which remembers the authenticated user for that connection) and
returns a response `Message`. Because it has no networking, it is trivially
unit-testable against an in-memory `TrackerState`.

Authentication is enforced centrally: only `CREATE_USER` and `LOGIN` are allowed
before you have a session; everything else returns `please login first`.

### Concurrency in the tracker

`TrackerServer` uses **bounded thread-per-connection**: each accepted client gets
a thread that runs a receive→handle→respond loop for the connection's lifetime,
and `activeClients_` caps how many may exist at once (the original spawned
*unbounded* detached threads — a memory-exhaustion risk).

All `TrackerState` methods lock a single mutex. This "one big lock" is a conscious
trade-off: the critical sections are tiny (map inserts), the coordination
workload is light compared to the data plane, and coarse locking is trivially
correct. Fine-grained locking (per-group locks, a read/write lock) is the
optimization if profiling ever demands it.

#### Why not `epoll` here?

`epoll` is the classic answer to "handle 10k connections on a handful of
threads." We didn't use it because tracker connections are **long-lived and
mostly idle**, the per-connection state (a `ClientSession`) is tiny, and a
blocking thread-per-connection model with framed IO is far easier to get
*correct*. `epoll` with length-prefixed framing forces you to maintain a partial
read/write buffer and a parse state machine per fd — real complexity that isn't
justified at this scale. The event-loop migration is a well-understood next step
and is listed in §16.

---

## 7. Durability: the write-ahead journal

Original trackers kept everything in RAM: a restart wiped all users, groups and
file metadata. We fix this with a **write-ahead journal** — the same core idea
databases use.

- Each durable event is appended to `tracker-<index>.journal` as
  `[4-byte length][serialized event]` (the same framing as the wire — one
  consistent idea reused).
- On startup, `openJournal` replays the file top to bottom, calling `applyEvent`
  for each record, reconstructing the exact in-memory state.
- Records are flushed after each append, so a crash loses at most an in-flight
  write. A truncated tail record (torn write on crash) is detected by the length
  check and safely skipped.

Because events are idempotent and self-contained, replay is deterministic:
same journal → same state. Login state is intentionally absent from the journal,
so after a restart everyone is offline until they reconnect — which is correct,
since their TCP connections died with the old process.

> **Talking point:** "I made the tracker crash-recoverable with a write-ahead
> event journal and idempotent replay — the same pattern as a database redo log."

---

## 8. Replication & high availability

### Goal

If one tracker dies, the swarm keeps working: clients fail over to the other
tracker, which already knows the users/groups/files.

### The link

Exactly two trackers. To avoid a connect race (both dialing simultaneously and
ending up with two links), the tracker with the **higher index dials** the lower
one; the lower one only **accepts**. The connector sends a `TRACKER_HELLO`
handshake so the acceptor can distinguish a peer tracker from an ordinary client.
The single TCP link is used **bidirectionally**, and **all** outbound traffic
goes through one sender thread draining a queue, so writes never interleave.

### What flows across it

1. **Live events.** Every locally-produced event is enqueued and sent to the peer,
   which applies it via `applyRemoteEvent` (no re-forwarding → no loops).
2. **Snapshot (anti-entropy).** When the link (re)establishes, the connecting
   side dumps its *entire* durable state as a stream of events. The receiver
   applies them idempotently. This is how a tracker that was down **catches up**
   the moment it reconnects.

### Client-side failover

`TrackerClient` holds the list of all trackers. If a request fails because the
link broke, it transparently connects to the next tracker, **re-logs in** with
the stored credentials, and retries — so the user's session survives a tracker
crash.

### The limits of this design (be honest in interviews)

This gives **eventual consistency**, not consensus:
- There is no leader and no total order of events. If both trackers accept
  conflicting mutations during a partition, the merge is union / last-writer-wins,
  not a principled resolution.
- Events dropped while the peer was down are recovered by the *snapshot*, not by
  a replayed log, so very recent ephemeral state can be briefly stale.

The principled fix is a **consensus protocol (Raft)**: elect a leader, replicate
an ordered log, commit on a quorum. That's the headline item in §16, and being
able to articulate *why* you'd move to Raft — and what it buys (linearizable,
partition-safe agreement) — is itself a strong interview signal.

---

## 9. Security: password hashing

Storing passwords in plaintext (as the original did) is indefensible: anyone who
reads the tracker's memory, journal, or disk gets every password.

`Crypto` stores only a **derived hash**:

```
pbkdf2$100000$<random-salt-hex>$<derived-key-hex>
```

- **PBKDF2-HMAC-SHA256** stretches the password through 100 000 iterations,
  making brute force expensive.
- A **random 16-byte salt** per user means identical passwords hash differently
  and precomputed "rainbow tables" are useless.
- Verification is **constant-time** (`diff |= a[i]^b[i]`) so an attacker can't
  learn the hash byte-by-byte from timing differences.

Only the encoded string is ever stored or journaled — the plaintext exists only
for the instant it's being hashed/verified. (What's *not* covered: transport
encryption. See TLS in §16.)

---

## 10. The client and the download engine

This is where the most interesting engineering lives.

### 10.1 Chunking a file (`FileMeta`)

`computeFileMeta` reads a file with `pread` in `chunk_size` blocks and computes:
- `chunkHashes[i]` = SHA-256 of chunk `i`,
- `numChunks`, `size`,
- `fullHash` = SHA-256 of the concatenated chunk hashes (a Merkle-style content
  identifier — a stable fingerprint of the exact bytes, computed without a second
  pass over the data).

These hashes are announced to the tracker and later handed to downloaders, who
use them to verify each chunk independently.

### 10.2 Serving chunks (`PeerServer` + `FileRegistry`)

`FileRegistry` is the thread-safe catalogue of files this client can serve. For
each file it stores the local path, chunk metadata, and a **`have` bitfield**
(which chunks are actually present on disk).

`PeerServer` answers two peer requests over the framed protocol, keeping the
connection alive for many requests:
- `GET_BITFIELD filename` → the `have` string (`"1011…"`), so a downloader knows
  what this peer can provide.
- `GET_CHUNK filename index` → the chunk bytes (verified-available, read with
  `pread`), as the message *body*.

A subtle but important detail: a client registers a file in the registry **the
moment it starts downloading**, with the bitfield reflecting chunks it already
has (initially maybe zero). As chunks land, it flips bits to `1`. This makes a
mid-download client a **partial seeder** — other downloaders can pull the chunks
it has already obtained. That's what makes rarest-first meaningful in a real
swarm rather than a formality.

### 10.3 The downloader (`ChunkDownloader`) — step by step

**Setup**
1. Preallocate the output file to its final size (`ftruncate`).
2. **Resume:** read `<save>.p2p` — a bitmap of previously completed chunks — and
   seed the `have_` set from it. Register the (partial) file in the registry.
3. **Discover peers:** connect to each owner endpoint the tracker gave us and
   fetch its bitfield. Peers that don't answer are dropped. From the bitfields we
   know, per chunk, which peers can serve it.
4. The set of chunks we still need becomes `pending_`.

**The worker pool**

We create a `ThreadPool` of `download_workers` threads and submit a persistent
`workerLoop` to each. Every worker repeats: *pick work → fetch → record result*
until nothing is left. This is the fix for the original's one-chunk-at-a-time
loop — up to N transfers proceed concurrently.

**Picking work (`selectWork`, rarest-first + best peer)**

Under the lock, for each still-needed chunk we count how many *live* peers hold
it, and consider only chunks that have at least one peer eligible *right now*
(holds the chunk, not blacklisted, not in backoff). Among those candidates we
pick the **rarest** (fewest providers — the pure `rarestOf` function, unit
tested). For that chunk we then choose the **best peer** by score. The chunk moves
`pending_ → inflight_`.

If nothing is runnable this instant, we first check `canStillComplete_locked()`:
if some needed chunk has *zero* live providers, the download is provably
impossible and we fail fast. Otherwise a peer must merely be in backoff, so we
`wait_for` on the condition variable and try again — no busy-spin.

**Fetching a chunk (`fetchChunk`)**
1. Reuse (or open) a keep-alive connection to the peer (per-worker connection
   cache).
2. Send `GET_CHUNK`, receive the body.
3. Check the length, then **verify SHA-256** against the expected chunk hash. A
   mismatch → discard (the byte we'd write is never trusted).
4. `pwrite` the bytes at `index * chunk_size` — a *positioned* write that is safe
   from multiple threads and needs no shared file offset (another original bug
   fixed).
5. Update the peer's throughput **score** (an EWMA of bytes/second), so faster
   peers are preferred next time.

**Handling failure (backoff + blacklist)**

On any failure (unreachable, wrong response, hash mismatch, short read) the chunk
goes back to `pending_` and the peer is penalized:
- increment its failure count,
- set an **exponential backoff** deadline (`nextRetry`), capped at 5 s,
- after `maxFailuresPerPeer` failures, mark it **dead** (permanently skipped).

So a single flaky or malicious peer never stalls the download; work simply routes
around it. Contrast the original's blind `sleep(10)` that retried the *same*
random peer.

**Finishing**
- A background thread prints a live progress bar (percent, MB/s, chunks, live
  peers).
- When every `have_` bit is set, the download succeeded: we `fsync`, delete the
  `.p2p` resume file, and the client tells the tracker `DOWNLOAD_COMPLETE` so it's
  registered as a full owner (seeder) for future downloaders.

---

## 11. The concurrency model, precisely

Being able to state your threads, shared state, and invariants is what separates
"I used threads" from "I understand concurrency."

**Threads**
- *Tracker:* 1 accept thread; 1 thread per client connection (bounded); 1
  replication sender thread; 1 replication reader thread per link; connector
  thread (dialer side).
- *Client:* main/REPL thread; 1 PeerServer accept thread + 1 thread per inbound
  peer connection; N downloader workers + 1 progress thread during a download.

**Shared state & its guard**
- `TrackerState`: one `std::mutex`. Every public method locks it. Network sends
  to the peer happen *outside* this lock (the sink only enqueues) to avoid
  holding the state lock during IO.
- `FileRegistry`: one `std::mutex`; disk reads for `GET_CHUNK` happen *outside*
  the lock (copy the path under lock, read after) so disk IO doesn't serialize
  the registry.
- `ChunkDownloader`: one `std::mutex` + one `std::condition_variable` protecting
  `pending_`, `inflight_`, `have_`, `peers_`, `failed_`. Atomics
  (`bytesDone_`, `chunksDone_`, `finished_`) carry progress counters that don't
  need the lock.

**Key invariants**
- A chunk is in exactly one of: `pending_`, `inflight_`, or done (`have_[i]==1`).
- `inflight_` never exceeds the number of workers.
- Termination: the pool goes idle only when `pending_` and `inflight_` are both
  empty (success) or `failed_` is set (a chunk became unsatisfiable). The CV is
  notified on every state change, so a worker that waited always re-evaluates.

**Why no deadlock:** there is never more than one lock held at a time in any
thread (locks are not nested across subsystems), so a lock-ordering cycle is
impossible. The replication reader can be unblocked from another thread by
`shutdown(fd)` rather than by taking the same lock it might be waiting under.

---

## 12. End-to-end walkthrough of a download

Trace `download_file movies bigfile.iso ~/Downloads` on client *bob*:

1. **Discover.** `TrackerClient` sends `DOWNLOAD_FILE{gid=movies, filename=bigfile.iso}`.
   `CommandHandler` calls `getFileMeta`, which checks bob is a member, then
   returns a `META` message: size, full hash, all chunk hashes, and the `ip:port`
   of every *online* owner except bob.
2. **Announce as partial seeder.** bob sends `START_DOWNLOAD`; the tracker adds
   bob as an owner so other downloaders can start pulling from him immediately.
3. **Set up.** `ChunkDownloader` preallocates `~/Downloads/bigfile.iso`, checks
   for a `.p2p` resume file, and connects to each owner to fetch bitfields.
4. **Parallel fetch.** 8 workers loop: pick the rarest needed chunk that some
   fast, non-blacklisted peer has; `GET_CHUNK`; verify SHA-256; `pwrite` at the
   right offset; mark it `have`; flip the registry bit (bob can now serve that
   chunk); persist the resume bit.
5. **Progress.** The progress thread prints `[####----] 42% 18.3 MB/s chunks 40/95 peers 3`.
6. **Complete.** All bits set → `fsync`, delete `.p2p`, send `DOWNLOAD_COMPLETE`.
   bob is now a full seeder. The file on disk is bit-identical to the source
   because every chunk was individually verified.

---

## 13. How to run and experiment

Build and basic run: see the [README](README.md). Things worth trying to
*understand the system by observing it*:

- **Watch rarest-first & parallelism:** set `log_level = debug` and
  `download_workers = 8`, seed a file from one client, then download from another
  and watch chunks arrive out of order from whichever peers have them.
- **Prove resume works:** start a large download, `Ctrl-C` the downloader
  mid-transfer, then re-run the same `download_file` — it reports
  `resume: K/N chunks already present` and only fetches the rest.
- **Prove integrity:** stop a seeder and hand-edit a byte of its copy, restart it,
  and download — the tampered chunk fails SHA-256 and is refetched (or the peer is
  blacklisted). The final file is still correct if another good peer exists.
- **Prove HA/failover:** run both trackers, connect a client, then kill the
  tracker the client is using — the next command transparently fails over and
  re-logs into the other tracker.
- **Prove durability:** create users/groups/files, kill the tracker, restart it —
  everything is still there (replayed from the journal). Inspect
  `tracker-1.journal` to see the event log.
- **Prove correctness under stress:** rebuild with `-DSANITIZE=thread` and run the
  smoke test to check the concurrency is data-race clean.

---

## 14. Rebuild-from-scratch checklist

If you had to reconstruct this, here's the order that keeps you always compiling:

1. **Framing first.** `Message` (envelope) + `Connection` (length-prefixed frames,
   `sendAll`/`recvAll`). Everything rides on this. Write the round-trip test now.
2. **Primitives.** `Sha256`, `Crypto`, `Logger`, `Config`, `ThreadPool`. Small and
   independent; test SHA-256 against known vectors.
3. **Protocol constants.** Fix the command/header strings in one header.
4. **Tracker state + journal.** Model users/groups/files as *events*; make
   `applyEvent` idempotent; add append + replay. Test with a temp journal.
5. **Command handler.** Pure message→state→response. Unit-test against in-memory
   state (no sockets).
6. **Tracker server.** Accept loop, session, bounded threads. Now a single tracker
   works end-to-end for control commands.
7. **Client peer server + registry.** Serve bitfields and chunks.
8. **File chunking.** `FileMeta` with per-chunk hashes.
9. **The downloader.** Start *sequential* (one chunk, one peer) to get correctness,
   then add: worker pool → rarest-first (`rarestOf`, unit-tested) → verification →
   backoff/blacklist → resume → progress. Add features one at a time, re-testing.
10. **Replication.** Single link, dialer/acceptor, event forwarding, then snapshot
    resync. Add client failover.
11. **Harden.** CMake, sanitizers, the end-to-end smoke test, docs.

The meta-lesson: **build the protocol and a correct sequential path first, then
layer parallelism and features on top**, testing each layer. Trying to write the
parallel downloader before the framing is solid is how you get the mysterious
"half a chunk" bugs.

---

## 15. Interview Q&A

**Q: Walk me through what happens when a file is downloaded.**
See §12 — discover via tracker, fetch bitfields, parallel rarest-first fetch with
per-chunk SHA-256 verification and `pwrite`, mark complete, become a seeder.

**Q: How do you download in parallel without corrupting the file?**
Each worker writes its chunk with `pwrite(fd, buf, len, index*chunkSize)` — a
positioned write that doesn't touch a shared file offset, so concurrent writes to
different offsets are safe. No per-chunk locking needed.

**Q: Why rarest-first?**
It keeps scarce chunks from vanishing when a peer leaves and balances load across
the swarm. Downloading the most common chunks first would leave everyone missing
the same rare chunk at the end. We compute rarity from peer bitfields.

**Q: How do you handle a slow or malicious peer?**
Slow: peer scoring (throughput EWMA) deprioritizes it. Malicious/faulty: every
chunk is SHA-256 verified before writing; a bad chunk is discarded, the peer is
penalized with exponential backoff, and blacklisted after repeated failures. Work
routes to other peers automatically.

**Q: TCP is a stream — how do you know where a message ends?**
Length-prefixed framing: 4-byte big-endian length, then exactly that many bytes,
read in a loop (`recvAll`) because a single `recv` can return fewer bytes than
requested. The payload is a typed, versioned envelope with a binary-safe body.

**Q: What happens if the tracker restarts / crashes?**
Nothing is lost: every durable state change is appended to a write-ahead journal
and replayed on startup (idempotent events → deterministic state). Online status
is intentionally not persisted.

**Q: How is the second tracker kept in sync, and what are the limits?**
Events are forwarded over a single link; on (re)connect the connecting side sends
a full snapshot (anti-entropy) that the peer merges idempotently. Clients fail
over between trackers. It's eventual consistency, *not* consensus — the
principled upgrade is Raft (ordered replicated log + quorum commit).

**Q: How do you store passwords?**
Never in plaintext — salted PBKDF2-HMAC-SHA256 (100k iterations), compared in
constant time. Per-user random salt defeats rainbow tables.

**Q: Why thread-per-connection and not epoll?**
Bounded thread-per-connection is correct, simple, and fine for the connection
counts here; connections are long-lived and cheap in state. epoll is the right
move for C10k and I can describe the migration (non-blocking sockets + per-fd
parse state machine), but I chose correctness-first for this scope.

**Q: How would you scale the tracker to millions of users?**
epoll/io_uring event loop, shard state behind finer-grained locks or partition by
group, move persistence to a real embedded DB (SQLite/RocksDB) or a replicated
store, and replace ad-hoc replication with Raft.

---

## 16. Limitations & future work

Ordered roughly by value-for-effort. Each is a genuine, discussable extension —
not hand-waving.

1. **Raft-replicated tracker.** Replace best-effort event replication with a
   proper consensus log (leader election, quorum commit). Turns "eventual
   consistency between two trackers" into linearizable, partition-safe HA.
2. **TLS on both planes.** Wrap `Connection` in OpenSSL SSL_* to encrypt tracker
   and peer traffic — defends against eavesdropping and MITM.
3. **`epoll`/`io_uring` tracker.** Event-loop concurrency for C10k+, with per-fd
   framing state machines.
4. **Dynamic rarest-first.** Re-poll bitfields periodically and subscribe to
   "have" announcements so peers that join mid-download are considered, and rarity
   tracks the live swarm.
5. **Embedded-DB persistence.** Swap the journal for SQLite/RocksDB to get
   indexing, compaction, and snapshots for free (SQLite wasn't available in the
   build environment, which is why the dependency-free journal exists).
6. **Endgame mode.** Near the end of a download, request the last few chunks from
   *all* peers at once to avoid a slow final peer holding everything up.
7. **NAT traversal.** STUN/hole-punching so peers behind routers connect directly.
8. **Decentralization (DHT / Kademlia).** Remove the central tracker entirely for
   true serverless peer discovery.
9. **Bandwidth throttling (token bucket)** and richer **metrics/observability**.

---

## 17. Glossary

- **Anti-entropy** — periodically reconciling replicas by exchanging full/partial
  state so they converge (here: the snapshot on tracker reconnect).
- **Bitfield** — per-peer map of which chunks it holds.
- **Chunk / piece** — fixed-size unit of transfer and verification (512 KiB).
- **EWMA** — exponentially weighted moving average; used to smooth peer throughput
  scores.
- **Framing** — delimiting messages within a TCP byte stream (here: 4-byte length
  prefix).
- **Idempotent** — applying an operation twice has the same effect as once;
  essential for safe replay and merge.
- **Leecher / Seeder** — a peer still downloading / a peer with the complete file.
- **PBKDF2** — Password-Based Key Derivation Function 2; stretches a password to
  slow brute-force attacks.
- **pwrite/pread** — positioned read/write at an explicit offset; thread-safe
  because they don't use the shared file offset.
- **Rarest-piece-first** — fetch the chunk held by the fewest peers first.
- **Swarm** — all peers associated with a file.
- **Write-ahead journal** — append every change to a log before/at the time it's
  applied, so state can be rebuilt after a crash.
