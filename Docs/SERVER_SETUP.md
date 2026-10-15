# Campus Combat Server Setup: ngrok now, AWS later

As of 2026-10-15.

## Overview

Today the game's backend runs on the development PC (192.168.1.5), and phones reach it through an ngrok HTTPS tunnel at `https://dante-morphologic-momently.ngrok-free.dev/v1`. Later the same API, database and cache move to AWS, and the game switches to the AWS URL with a one-line config change and a rebuild.

The tunnel only works while the PC is on and ngrok is running. If either stops, every phone request fails with 404 (ERR_NGROK_3200). If ngrok is up but the API is not, requests fail with 502.

## Current setup

All parts run on the development PC. The backend lives in `C:\UE\Projects\CampusCombat\campuscombat-backend`.

| Part | Runs as | Address | Notes |
| --- | --- | --- | --- |
| API (Fastify) | `node --env-file=../.env src/server.js` in `api` | port 8080 | Health check: `/v1/health` (plain `/health` returns 404 by design) |
| Postgres 17 | Docker container | 127.0.0.1:5433 | Not 5432: MediVoice containers use the default ports |
| Redis 7 | Docker container | 127.0.0.1:6380 | Not 6379, same reason |
| LocalStack 4.9 | Docker container | 127.0.0.1:4566 | Local stand-in for AWS services |
| Demo bots | `node --env-file=../.env scripts/demo-bots.js` in `api` | talks to 127.0.0.1:8080 | Optional |
| ngrok tunnel | `ngrok http 8080 --url dante-morphologic-momently.ngrok-free.dev` | public HTTPS | Must use the full domain |
| Match server | Unreal game server | UDP 7777 | Firewall rule "Campus Combat Match (UDP 7777)" |

The game reads the server address from `Config/DefaultGame.ini`, section `[/Script/CollegeCampusGame.CampusNetSettings]`:

```
ServerHost=192.168.1.5
ApiPort=8080
GamePort=7777
ApiBaseOverride="https://dante-morphologic-momently.ngrok-free.dev/v1"
ApiHost=dante-morphologic-momently.ngrok-free.dev/v1
```

When `ApiBaseOverride` is set, the game uses the ngrok URL. When it is empty, the game uses `ServerHost:ApiPort` on the local Wi-Fi. Keep the URL in quotes: the ini reader treats `//` as a comment.

## Start-up checklist

Run these in order after a restart, or after anything was stopped for an Unreal build.

1. Start Docker Desktop and wait until it says it is running.
2. Start the database and cache: in `campuscombat-backend`, run `docker compose up -d`.
3. Apply database changes: in `campuscombat-backend\api`, run `npm run migrate`.
4. Start the API: in `api`, run `node --env-file=../.env src/server.js`, or `npm start`. Wait until the database is up first, or the API exits.
5. Check it locally: open `http://192.168.1.5:8080/v1/health`. It should show `"status":"ok"`.
6. Start the tunnel: `ngrok http 8080 --url dante-morphologic-momently.ngrok-free.dev`. Leave the window open.
7. Check it publicly: `curl https://dante-morphologic-momently.ngrok-free.dev/v1/health`. It should show the same `ok`. (A browser or PowerShell may show ngrok's warning page, ERR_NGROK_6024; that is normal.)
8. Optional: start the demo bots.

Before an Android build, stop Docker (`wsl --shutdown`), the API, bots and ngrok to free memory. The PC has 7.7 GB of RAM and the cook needs most of it. Build with `C:\UE\Projects\CampusCombat\Build Campus Combat.bat`.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| App shows 404 on every request | ngrok tunnel is offline (ERR_NGROK_3200) | Start ngrok with the full domain, step 6 |
| ngrok started but app still 404 | Tunnel started on a shortened name such as `dante-morphologic` | Restart with `--url dante-morphologic-momently.ngrok-free.dev` |
| App shows 502 | ngrok is up but the API is not running (yet) | Step 4, wait a few seconds, reopen the game |
| Public health check works, app fails | Old APK with a different URL | Check `ApiBaseOverride`, rebuild |
| Local health check fails | API not running, or Docker down | Steps 1 to 4 |
| Health shows `db` or `redis` not ok | Containers stopped | `docker compose up -d` |
| Tunnel works but is slow | Free ngrok tier | Use LAN mode on the same Wi-Fi |

LAN mode: on the same Wi-Fi as the PC, empty `ApiBaseOverride` and rebuild. The game then calls `192.168.1.5:8080` directly, without ngrok. Change `ServerHost` if the PC's IP changes.

## Moving to AWS later

The game code does not change. Only the URL in `DefaultGame.ini` and the server behind it change.

```
NOW:    Phone (APK) -> ngrok tunnel -> Dev PC: API :8080, Postgres (Docker), Redis (Docker), match server UDP 7777
LATER:  Phone (APK) -> own HTTPS domain + load balancer -> AWS: API container, managed Postgres, managed Redis, GameLift match servers
```

Steps when the time comes:

1. Choose the AWS services: where the API container runs, managed Postgres and Redis, and GameLift for match servers.
2. Set up the database and cache, then copy the Postgres data across.
3. Deploy the API with the production `.env` values (database, Redis, JWT and Razorpay keys, AWS keys).
4. Put the API behind a real HTTPS domain and check `/v1/health`.
5. Set `ApiBaseOverride` (and `ApiHost`) to the new domain, rebuild the APK and test on the phone.
6. Stop ngrok once every tester has the new build.

Open questions:

- [ ] Which AWS service runs the API (ECS, App Runner or EC2)?
- [ ] Which AWS region (Mumbai is closest to players in India)?

A fuller migration plan is drafted in `Desktop\tree js game\college-game\docs\aws-migration-plan.md`.
