// fighter.cpp - standalone engine speaking UCI and xboard (auto-detected), plus "perft" / "bench" utilities.
#include "search.hpp"
#include <iostream>

static uint64_t perft(Position& p, int d) {
    if (d == 0) return 1;
    MoveList ml; p.genMoves(ml, false);
    uint64_t n = 0;
    for (int i = 0; i < ml.n; i++) if (p.make(ml.m[i])) { n += perft(p, d - 1); p.unmake(); }
    return n;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    Position pos;
    Searcher s(64);

    if (argc >= 2 && std::string(argv[1]) == "perft") {
        // perft <depth> [fen...]
        int d = argc >= 3 ? atoi(argv[2]) : 5;
        if (argc >= 4) { std::string fen; for (int i = 3; i < argc; i++) fen += std::string(argv[i]) + " "; pos.setFEN(fen); }
        auto t0 = std::chrono::steady_clock::now();
        uint64_t n = perft(pos, d);
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        printf("perft(%d) = %llu  (%.2fs)\n", d, (unsigned long long)n, sec);
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "bench") {
        SearchLimits lim; lim.movetimeMs = argc >= 3 ? atoi(argv[2]) : 3000; lim.verbose = true;
        auto r = s.think(pos, lim);
        printf("bestmove %s\n", Position::uci(r.best).c_str());
        return 0;
    }

    bool xboard = false, forceMode = false;
    int engineSide = BLACK;
    int stMs = 0, sdDepth = 64, myTimeCs = 0, incMs = 0, mps = 0;
    std::vector<Move> gameMoves;
    Position startPos;

    auto xbThinkAndMove = [&]() {
        SearchLimits lim;
        lim.uciInfo = false; lim.verbose = true; lim.maxDepth = sdDepth;
        if (stMs) lim.movetimeMs = stMs;
        else if (myTimeCs) {
            int left = myTimeCs * 10;
            int movesToGo = mps ? std::max(1, mps - (int)(gameMoves.size() / 2) % mps) : 30;
            lim.movetimeMs = std::max(20, std::min(left / 2, left / (movesToGo + 2) + incMs * 3 / 4));
            lim.softMs = lim.movetimeMs / 2;
            lim.movetimeMs = std::min(left / 3 + 1, lim.movetimeMs * 2);
        } else lim.movetimeMs = 2000;
        auto r = s.think(pos, lim);
        if (!r.best) return;
        printf("move %s\n", Position::uci(r.best).c_str());
        pos.make(r.best);
        gameMoves.push_back(r.best);
    };

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream ss(line);
        std::string cmd; ss >> cmd;
        if (cmd.empty()) continue;
        if (cmd == "quit") break;

        // ---------------- UCI ----------------
        if (cmd == "uci") {
            printf("id name Fighter 1.0\nid author mutualJoe\noption name Hash type spin default 64 min 1 max 4096\nuciok\n");
        } else if (cmd == "isready") printf("readyok\n");
        else if (cmd == "ucinewgame") { s.clear(); pos = Position(); }
        else if (cmd == "setoption") {
            std::string tok, name, value; bool inVal = false;
            while (ss >> tok) { if (tok == "name") continue; if (tok == "value") { inVal = true; continue; } (inVal ? value : name) += tok; }
            if (name == "Hash" && !value.empty()) s.resizeTT(atoi(value.c_str()));
        } else if (cmd == "position") {
            std::string tok; ss >> tok;
            if (tok == "startpos") { pos = Position(); ss >> tok; }
            else if (tok == "fen") {
                std::string fen;
                while (ss >> tok && tok != "moves") fen += tok + " ";
                pos.setFEN(fen);
            }
            if (tok == "moves") while (ss >> tok) { Move m = pos.parseUCI(tok); if (!m) break; pos.make(m); }
        } else if (cmd == "go" && !xboard) {
            std::string tok; int wt = 0, bt = 0, wi = 0, bi = 0, mt = 0, depth = 64, mtg = 0; bool inf = false;
            while (ss >> tok) {
                if (tok == "wtime") ss >> wt; else if (tok == "btime") ss >> bt; else if (tok == "winc") ss >> wi;
                else if (tok == "binc") ss >> bi; else if (tok == "movetime") ss >> mt; else if (tok == "depth") ss >> depth;
                else if (tok == "movestogo") ss >> mtg; else if (tok == "infinite") inf = true;
            }
            SearchLimits lim; lim.verbose = true; lim.maxDepth = depth;
            int left = pos.side == WHITE ? wt : bt, inc = pos.side == WHITE ? wi : bi;
            if (mt) lim.movetimeMs = mt;
            else if (left) {
                int t = left / (mtg ? mtg + 1 : 30) + inc * 3 / 4;
                lim.softMs = std::max(10, t / 2);
                lim.movetimeMs = std::max(10, std::min(left / 3, t * 2));
            } else if (inf || depth < 64) lim.movetimeMs = 0;
            auto r = s.think(pos, lim);
            printf("bestmove %s\n", Position::uci(r.best).c_str());
        }
        // ---------------- xboard / CECP ----------------
        else if (cmd == "xboard") xboard = true;
        else if (cmd == "protover") printf("feature myname=\"Fighter 1.0\" setboard=1 usermove=0 ping=1 sigint=0 sigterm=0 colors=0 done=1\n");
        else if (cmd == "new") { pos = Position(); startPos = pos; gameMoves.clear(); s.clear(); forceMode = false; engineSide = BLACK; sdDepth = 64; }
        else if (cmd == "setboard") { std::string fen; std::getline(ss, fen); pos.setFEN(fen); startPos = pos; gameMoves.clear(); }
        else if (cmd == "force") forceMode = true;
        else if (cmd == "go") { forceMode = false; engineSide = pos.side; xbThinkAndMove(); }
        else if (cmd == "st") { int x; ss >> x; stMs = x * 1000; }
        else if (cmd == "sd") ss >> sdDepth;
        else if (cmd == "level") { std::string base; ss >> mps >> base >> incMs; incMs *= 1000; stMs = 0; }
        else if (cmd == "time") ss >> myTimeCs;
        else if (cmd == "ping") { int n; ss >> n; printf("pong %d\n", n); }
        else if (cmd == "undo" || cmd == "remove") {
            int n = cmd == "undo" ? 1 : 2;
            for (int i = 0; i < n && !gameMoves.empty(); i++) { gameMoves.pop_back(); }
            pos = startPos; for (Move m : gameMoves) pos.make(m);
        } else if (cmd == "white" || cmd == "black" || cmd == "otim" || cmd == "easy" || cmd == "hard" || cmd == "post" ||
                   cmd == "nopost" || cmd == "random" || cmd == "computer" || cmd == "accepted" || cmd == "rejected" ||
                   cmd == "result" || cmd == "?" || cmd == "variant") {
            // ignored
        } else if (cmd == "d") {
            printf("%s\n", pos.fen().c_str());
        } else {
            std::string mv = cmd == "usermove" ? (ss >> cmd, cmd) : cmd;
            Move m = pos.parseUCI(mv);
            if (!m) { printf("Illegal move: %s\n", mv.c_str()); continue; }
            pos.make(m); gameMoves.push_back(m);
            if (xboard && !forceMode && pos.side == engineSide) xbThinkAndMove();
        }
    }
    return 0;
}
