# P2P File Sharing System

A BitTorrent-style peer-to-peer file sharing system in modern C++ (C++17). Files
are transferred **directly between peers in fixed-size chunks**, downloaded **in
parallel from multiple peers at once** using a **rarest-piece-first** scheduler,
and verified with **SHA-256** on every chunk. A lightweight **tracker**
coordinates users, groups and file ownership — it stores *who has what*, never
the file bytes themselves. Two trackers replicate to each other for **high
availability**, and clients transparently **fail over** if one goes down.

> This is a from-scratch redesign of a course project. The companion
> [`instructions.md`](instructions.md) explains every concept and design
> decision in depth — read it if you want to understand, present, or rebuild the
> system.

---

## Highlights

- **Parallel chunk downloading** — a bounded worker pool pulls many chunks
  concurrently from different peers (`pwrite` at offset, no shared-offset races).
- **Rarest-piece-first scheduling** — peers exchange *bitfields* of the chunks
  they hold; the scarcest chunk is fetched first, keeping the swarm healthy.
- **Partial seeding** — a client starts serving the chunks it already has *while
  it is still downloading*, so multiple downloaders accelerate each other.
- **Integrity + fault tolerance** — every chunk is SHA-256 verified; bad peers
  are retried with exponential backoff and permanently blacklisted after
  repeated failures, so one dead peer never stalls a download.
- **Resumable downloads** — a `<file>.p2p` bitmap records completed chunks;
  an interrupted download continues instead of restarting.
- **Durable tracker** — every state change is appended to a **write-ahead
  journal** and replayed on startup, so a restart loses nothing.
- **High availability** — two trackers replicate events and resync via a full
  snapshot (anti-entropy) on (re)connect; clients fail over automatically.
- **Security** — passwords are stored as salted **PBKDF2-HMAC-SHA256** hashes and
  verified in constant time (no plaintext).
- **Robust protocol** — a versioned, length-prefixed, binary-safe message
  envelope replaces the fragile space-delimited text protocol.
- **Engineered like a real project** — layered architecture, CMake build, unit
  tests, an end-to-end smoke test, and optional ASan/TSan builds.

---

## Architecture

```
        ┌─────────────┐   replication (events + snapshot)   ┌─────────────┐
        │  Tracker 1  │◀──────────────────────────────────▶│  Tracker 2  │
        │  (journal)  │                                     │  (journal)  │
        └─────────────┘                                     └─────────────┘
              ▲   ▲                                                ▲
   control    │   │  control (login, groups, announce, discover)   │
   plane      │   └───────────────────────────┐                    │
              │                                │                    │
        ┌─────┴──────┐                   ┌─────┴──────┐       ┌─────┴──────┐
        │  Client A  │                   │  Client B  │       │  Client C  │
        │ (seeder)   │                   │ (leecher)  │       │ (leecher)  │
        └────────────┘                   └────────────┘       └────────────┘
              ▲                                │  ▲                  │
              │        data plane: chunk transfers (peer ↔ peer)     │
              └────────────────────────────────┘  └─────────────────┘
```

- **Control plane** (client ↔ tracker): authentication, groups, file
  *announcement* and peer *discovery*. Small messages.
- **Data plane** (client ↔ client): the actual chunk transfers. The tracker is
  never in this path — bandwidth scales with the number of peers.

### Repository layout

```
peer-to-peer-file-sharing/
├── common/            # shared library (p2pcommon)
│   ├── include/p2p/   # Message, Connection, Sha256, Crypto, ThreadPool, Logger, Config, Protocol
│   └── src/
├── tracker/           # tracker executable
│   ├── include/       # TrackerState, CommandHandler, PeerReplication, TrackerServer
│   └── src/
├── client/            # client executable
│   ├── include/       # TrackerClient, PeerServer, FileRegistry, FileMeta, ChunkDownloader, RarestFirst
│   └── src/
├── tests/             # unit tests (sha256, message round-trip, rarest-first)
├── config/            # tracker.conf, client.conf
├── scripts/           # smoke_test.sh (end-to-end)
├── CMakeLists.txt
├── README.md
└── instructions.md    # in-depth guide
```

---

## Prerequisites

- A C++17 compiler (GCC 9+ / Clang 10+)
- CMake ≥ 3.16
- OpenSSL (libcrypto) — used for SHA-256 and PBKDF2
- POSIX (Linux/macOS). Tested on Linux with GCC 13 + OpenSSL 3.

On Debian/Ubuntu:

```sh
sudo apt-get install build-essential cmake libssl-dev
```

---

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This produces:

- `build/tracker` — the tracker server
- `build/client` — the peer client
- `build/unit_tests` — the test runner

Optional sanitizer builds (recommended when hacking on the concurrency):

```sh
cmake -S . -B build-asan -DSANITIZE=address    # or thread / undefined
cmake --build build-asan -j
```

---

## Configuration

Both programs read a small `key = value` config file (see [`config/`](config/)).

**`config/tracker.conf`**

| Key                            | Meaning                                        |
|--------------------------------|------------------------------------------------|
| `tracker1_ip` / `tracker1_port`| endpoint of tracker #1                         |
| `tracker2_ip` / `tracker2_port`| endpoint of tracker #2                         |
| `max_clients`                  | max concurrent client connections              |
| `journal_dir`                  | where `tracker-<index>.journal` is written     |
| `log_level`                    | `debug` \| `info` \| `warn` \| `error`         |

**`config/client.conf`**

| Key                 | Meaning                                             |
|---------------------|-----------------------------------------------------|
| `tracker1_*` / `tracker2_*` | trackers to try (in order) with failover     |
| `chunk_size`        | chunk size in bytes (must match across the swarm)   |
| `download_workers`  | number of parallel download threads                 |
| `log_level`         | log verbosity                                       |

---

## Running

### 1. Start the tracker(s)

Each tracker process is told *which* tracker it is via an index (`1` or `2`):

```sh
# terminal 1
./build/tracker config/tracker.conf 1

# terminal 2 (optional second tracker for HA)
./build/tracker config/tracker.conf 2
```

A single tracker works fine on its own; the second is only needed for high
availability. The higher-index tracker dials the lower one, so start order does
not matter — they connect (and resync) as soon as both are up.

### 2. Start client(s)

A client needs its **own** `ip:port` (the address other peers use to fetch
chunks from it) plus a config file:

```sh
# terminal 3 — a seeder
./build/client 127.0.0.1:9001 config/client.conf

# terminal 4 — a downloader
./build/client 127.0.0.1:9002 config/client.conf
```

### 3. Use the interactive shell

```
create_user <user> <pass>            register a new account
login <user> <pass>                  authenticate (required before anything else)
logout
create_group <gid>                   create a group (you become owner)
list_groups
join_group <gid>                     request to join a group
list_requests <gid>                  (owner) list pending join requests
accept_request <gid> <user>          (owner) approve a join request
leave_group <gid>
list_files <gid>                     list files shared in a group
upload_file <gid> <filepath>         announce & seed a local file to the group
download_file <gid> <filename> <dest_dir>   download a file into dest_dir
help | quit
```

### Example session

**Seeder (`9001`):**

```
create_user alice secret
login alice secret
create_group movies
upload_file movies /home/alice/bigfile.iso
```

**Downloader (`9002`):**

```
create_user bob secret
login bob secret
join_group movies
```

**Seeder approves bob:**

```
accept_request movies bob
```

**Downloader fetches the file:**

```
list_files movies
download_file movies bigfile.iso /home/bob/Downloads
```

You'll see a live progress bar with percentage, throughput, chunk count and the
number of live peers. When it finishes, `bigfile.iso` in `/home/bob/Downloads`
is byte-for-byte identical to the source (every chunk was SHA-256 verified).

> Notes
> - Always `login` first; all group/file commands require an authenticated session.
> - The `<ip:port>` you launch a client with **must** be the address peers can
>   reach it on — it is what the tracker hands out to downloaders.
> - The tracker stores only metadata (hashes, sizes, ownership), never file bytes.

---

## End-to-end demo

The included script launches two trackers, a seeder and a downloader, transfers
a randomly generated file, and checks the SHA-256 of the result:

```sh
./scripts/smoke_test.sh 10      # transfer a 10 MB file
```

Expected tail:

```
[PASS] downloaded file matches source byte-for-byte
```

---

## Testing

```sh
cd build && ctest --output-on-failure
# or run the binary directly:
./build/unit_tests
```

Unit tests cover SHA-256 (against known NIST vectors), the message
serialize/parse round-trip (including binary bodies and values with spaces), and
the rarest-first selection logic.

---

## Design notes & limitations

This is a teaching/portfolio project. It deliberately keeps some things simple
and documents the trade-offs (all expanded in [`instructions.md`](instructions.md)):

- **Tracker concurrency** is bounded thread-per-connection, not `epoll`. Correct
  and bounded; the event-loop path to true C10k is described in the guide.
- **Replication** provides eventual consistency via event forwarding + snapshot
  resync between exactly two trackers. It is *not* a consensus protocol; Raft is
  the documented next step.
- **Rarest-first** uses bitfields fetched once at download start (peers that join
  later in the same download aren't re-polled).
- **No transport encryption** yet — the credential hashing protects stored
  passwords, but adding TLS to both planes is a natural extension.
---
