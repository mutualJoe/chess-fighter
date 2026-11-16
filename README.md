# chess-fighter

A from-scratch chess engine (**Fighter**) built to take on the engine inside macOS **Chess.app**, plus two tools around it:

- **coach** – a live move assistant. Play in Chess.app and coach reads the board straight from the Chess.app window and shows Fighter's suggested move, evaluation and expected line.
- **arena** – a match runner. It plays Fighter against Chess.app's own engine with no GUI, with an optional live board in the terminal, and saves every game to PGN.

## How Chess.app's AI works

Chess.app's opponent is **Sjeng 11.2** (by Gian-Carlo Pascutto). It ships as a separate executable inside the app bundle:

```
/System/Applications/Chess.app/Contents/Resources/sjeng.ChessEngine
```

Chess.app starts it as a child process and talks to it over stdin/stdout in plain text, using the **xboard/CECP** protocol (`new`, `force`, `go`, `e2e4`, `move e7e5`, …). The opening book (`*.opn`, `nbook.db`) and config (`sjeng.rc`) sit next to it in `Resources/`. The arena launches that same binary the same way, so Fighter plays against exactly what you face in the app.

A gotcha: Sjeng treats the `st` (seconds per move) command as a soft target and can think about 10× longer in critical positions. To make the match fair, the arena sends xboard's "move now" (`?`) when Sjeng's time runs out, so both engines get equal time.

## Requirements

- A Mac with Chess.app (any recent macOS)
- Xcode Command Line Tools for `clang++` and `swiftc`: `xcode-select --install`

## Build

```bash
git clone git@github.com:mutualJoe/chess-fighter.git
cd chess-fighter
make            # builds bin/fighter, bin/arena, bin/coach
make perft      # optional: move generator self-test (expects 4865609)
```

## 1. coach: play Chess.app with Fighter's suggestions

```bash
./bin/coach                    # you play White, 2 s of thinking per suggestion
./bin/coach --color black      # you play Black (board is flipped)
./bin/coach --time 5           # stronger suggestions, 5 s each
```

Open a game in Chess.app, then put the Chess.app and Terminal windows side by side. When it's your turn, coach highlights the suggested move in green and shows the evaluation (`+` is good for you) and the line Fighter expects. Make the move in Chess.app and coach updates automatically.

**One-time permission:** coach reads Chess.app's board through the macOS Accessibility API, the same interface VoiceOver uses. Chess.app labels each square, e.g. `"white rook, a1"`. Go to **System Settings → Privacy & Security → Accessibility**, enable your terminal app (Terminal, iTerm, …), then restart it.

How it reads the game:
- **Pieces:** read from Chess.app's accessibility labels for the 64 squares.
- **Side to move:** taken from the window title ("White to Move" / "Black to Move").
- **Castling rights:** inferred from whether the kings and rooks have left their home squares.
- **En passant:** not visible on screen, so it is ignored.

If coach can't see the board, run `./bin/coach --dump` to print what Chess.app exposes.

## 2. arena: Fighter vs. Chess.app's engine

```bash
./bin/arena --watch --games 4 --st 1         # watch live in the terminal
./bin/arena --games 32 --concurrency 8       # fast match, prints score + Elo estimate
```

| option | meaning |
|---|---|
| `--games N` | number of games (played in pairs, each opening once with each colour) |
| `--st SEC` | seconds per move for both engines (default 1) |
| `--sd DEPTH` | limit Sjeng to a fixed depth instead (Chess.app's weaker levels) |
| `--my-ms MS` | override Fighter's time per move |
| `--concurrency N` | games in parallel (default 4; `--watch` forces 1) |
| `--ponder` | let Sjeng think on Fighter's time, as it does in Chess.app |
| `--no-openings` | start from the initial position instead of 16 balanced openings |
| `--watch` | live board in the terminal |
| `--scale N` | board size for `--watch` (1–4, default 2) |
| `--ascii` | letters instead of chess glyphs |
| `--pgn FILE` | where games are saved (default `games.pgn`); open it in Chess.app via File → Open |
| `--log-dir DIR` | save the raw xboard conversation for every game |

## 3. fighter: the engine itself

`bin/fighter` is a standalone engine that speaks both **UCI** and **xboard**, so it works in any chess GUI (Cute Chess, Arena, BanksiaGUI, …).

```bash
./bin/fighter bench 3000                 # search the start position for 3 s
./bin/fighter perft 5 [FEN]              # move generator test
```

Under the hood:
- **Board:** bitboards, with move generation verified by perft on the standard test positions.
- **Search:** iterative deepening with principal variation search (PVS) and aspiration windows, plus a transposition table and null-move pruning.
- **Pruning and reductions:** late move reductions, late move pruning, futility pruning and reverse futility pruning, plus internal iterative reduction.
- **Move ordering:** static exchange evaluation (SEE), MVV-LVA, killer moves and the history heuristic.
- **Quiescence search:** delta pruning; captures that lose material by SEE are skipped.
- **Evaluation:** tapered middlegame/endgame scores covering material and piece-square tables, mobility, and pawn structure (passed, doubled and isolated pawns). It also scores rooks on open files and on the 7th rank, the bishop pair, the king's pawn shield and king attacks. Mop-up and drawish-material scaling cover endgames.

Speed is about 3.5M nodes/s single-threaded on Apple Silicon, reaching about depth 15 in one second.

## Layout

```
src/board.hpp     bitboards, move generation, make/unmake, FEN/SAN
src/search.hpp    evaluation + search
src/fighter.cpp   UCI/xboard engine front-end
src/arena.cpp     match runner vs. sjeng.ChessEngine (+ --watch view)
coach/coach.swift Accessibility-based board reader + suggestion UI
```

## Notes

- This repo contains no Apple or Sjeng code. The arena only launches the `sjeng.ChessEngine` that is already on your Mac.
- coach is meant for fun and practice against the computer, not for online play.
