# SocketSpark

A terminal-based, multi-user TCP chat written in C and POSIX threads.

## Build

```bash
make
```

## Run

Start the server in one terminal:

```bash
./socket-server/server
```

Start each client in another terminal:

```bash
./socket-client/client 127.0.0.1 Alice
./socket-client/client 127.0.0.1 Bob
```

The server listens on port `8080` on every network interface. Pass the server's IPv4 address as the first client argument when clients are on different machines.

## Commands

- `/name NAME` — set a name (letters, numbers, `_`, and `-`; up to 31 characters)
- `/help` — list commands
- `/quit` — disconnect cleanly
