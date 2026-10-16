# Physarum Plugin

Physarum simulation plugin for after effects.

Scaffolded 2026-10-16 by the deploy app. Lives at `C:\dev\Physarum`.

## What this is

An app or tool with no web interface (a plugin, a desktop executable, a CLI).
It lives outside `htdocs`, so Apache never serves it, and it has no live copy:
deploying it means committing and pushing to GitHub.

## Layout

- `src/` - source
- `build/`, `dist/`, `out/`, `bin/`, `obj/` - build output, gitignored

Commit source, not build output. A built `.exe`, `.aex` or installer belongs in a
GitHub Release rather than in the repo history.

## Building

_How to build and run this, once there is something to build._

## Deploying

Through the deploy app at <http://127.0.0.1:4400>: "Connect to GitHub" once, then
"Preview & push". It pushes to the Anchor Point Studio account.

## Secrets

Nothing secret belongs in this folder. Keep keys in environment variables or a
local file listed in `.gitignore`.
