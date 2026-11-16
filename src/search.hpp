// search.hpp - evaluation + alpha-beta search (PVS, TT, null move, LMR, SEE, killers/history).
#pragma once
#include "board.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

namespace Eval {
const int matMg[6] = {82, 337, 365, 477, 1025, 0};
const int matEg[6] = {94, 281, 297, 512, 936, 0};
const int phaseInc[6] = {0, 1, 1, 2, 4, 0};
const int seeVal[6] = {100, 320, 330, 500, 900, 20000};

// Tables are written visually: first row = rank 8. White piece on sq uses index sq^56, black uses sq.
const int pawnMg[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
     50, 50, 50, 50, 50, 50, 50, 50,
     10, 10, 20, 30, 30, 20, 10, 10,
      5,  5, 10, 25, 25, 10,  5,  5,
      0,  0,  5, 20, 20,  5,  0,  0,
      5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,
      0,  0,  0,  0,  0,  0,  0,  0};
const int pawnEg[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
     80, 80, 80, 80, 80, 80, 80, 80,
     50, 50, 50, 50, 50, 50, 50, 50,
     30, 30, 30, 30, 30, 30, 30, 30,
     15, 15, 15, 15, 15, 15, 15, 15,
      5,  5,  5,  5,  5,  5,  5,  5,
      0,  0,  0,  0,  0,  0,  0,  0,
      0,  0,  0,  0,  0,  0,  0,  0};
const int knightT[64] = {
    -50,-40,-30,-30,-30,-30,-40,-50,
    -40,-20,  0,  0,  0,  0,-20,-40,
    -30,  0, 10, 15, 15, 10,  0,-30,
    -30,  5, 15, 20, 20, 15,  5,-30,
    -30,  0, 15, 20, 20, 15,  0,-30,
    -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40,
    -50,-40,-30,-30,-30,-30,-40,-50};
const int bishopT[64] = {
    -20,-10,-10,-10,-10,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5, 10, 10,  5,  0,-10,
    -10,  5,  5, 10, 10,  5,  5,-10,
    -10,  0, 10, 10, 10, 10,  0,-10,
    -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10,
    -20,-10,-10,-10,-10,-10,-10,-20};
const int rookT[64] = {
      0,  0,  0,  0,  0,  0,  0,  0,
      5, 10, 10, 10, 10, 10, 10,  5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,
      0,  0,  0,  5,  5,  0,  0,  0};
const int queenT[64] = {
    -20,-10,-10, -5, -5,-10,-10,-20,
    -10,  0,  0,  0,  0,  0,  0,-10,
    -10,  0,  5,  5,  5,  5,  0,-10,
     -5,  0,  5,  5,  5,  5,  0, -5,
      0,  0,  5,  5,  5,  5,  0, -5,
    -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10,
    -20,-10,-10, -5, -5,-10,-10,-20};
const int kingMg[64] = {
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -30,-40,-40,-50,-50,-40,-40,-30,
    -20,-30,-30,-40,-40,-30,-30,-20,
    -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,
     20, 30, 10,  0,  0, 10, 30, 20};
const int kingEg[64] = {
    -50,-40,-30,-20,-20,-30,-40,-50,
    -30,-20,-10,  0,  0,-10,-20,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 30, 40, 40, 30,-10,-30,
    -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30,
    -50,-30,-30,-30,-30,-30,-30,-50};
const int* pstMg[6] = {pawnMg, knightT, bishopT, rookT, queenT, kingMg};
const int* pstEg[6] = {pawnEg, knightT, bishopT, rookT, queenT, kingEg};
const int passMg[8] = {0, 5, 10, 15, 25, 40, 60, 0};
const int passEg[8] = {0, 10, 20, 35, 60, 100, 150, 0};
const int atkWeight[6] = {0, 2, 2, 3, 5, 0};

inline int centerDist(int s) {
    int f = fileOf(s), r = rankOf(s);
    return std::max(3 - std::min(f, 7 - f), 3 - std::min(r, 7 - r));
}

inline int evaluate(const Position& p) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    int npm[2] = {0, 0};
    U64 pawnAtt[2];
    pawnAtt[WHITE] = ((p.pcs[PAWN] << 7) & ~T::file[7]) | ((p.pcs[PAWN] << 9) & ~T::file[0]);
    pawnAtt[BLACK] = ((p.pcs[6 + PAWN] >> 9) & ~T::file[7]) | ((p.pcs[6 + PAWN] >> 7) & ~T::file[0]);
    int danger[2] = {0, 0}, attackers[2] = {0, 0};
    for (int c = 0; c < 2; c++) {
        int them = c ^ 1;
        U64 ownPawns = p.pcs[c * 6 + PAWN], theirPawns = p.pcs[them * 6 + PAWN];
        int ourK = p.kingSq(c), theirK = p.kingSq(them);
        U64 zone = T::king[theirK] | bit(theirK);
        for (int pt = PAWN; pt <= KING; pt++) {
            for (U64 b = p.pcs[c * 6 + pt]; b;) {
                int s = poplsb(b);
                int idx = c == WHITE ? s ^ 56 : s;
                mg[c] += matMg[pt] + pstMg[pt][idx];
                eg[c] += matEg[pt] + pstEg[pt][idx];
                phase += phaseInc[pt];
                if (pt != PAWN && pt != KING) npm[c] += seeVal[pt];
                if (pt == PAWN) {
                    int f = fileOf(s);
                    int rr = c == WHITE ? rankOf(s) : 7 - rankOf(s);
                    if (popcnt(ownPawns & T::file[f]) > 1) { mg[c] -= 5; eg[c] -= 10; }
                    if (!(ownPawns & T::adjFiles[f])) { mg[c] -= 10; eg[c] -= 15; }
                    if (!(T::passed[c][s] & theirPawns)) {
                        mg[c] += passMg[rr];
                        int bonus = passEg[rr];
                        int stop = s + (c == WHITE ? 8 : -8);
                        if (stop >= 0 && stop < 64) {
                            bonus += (T::dist[theirK][stop] * 5 - T::dist[ourK][stop] * 2) * rr / 2;
                            if (p.board[stop] != EMPTY) bonus -= bonus / 3;
                        }
                        eg[c] += bonus;
                    }
                } else if (pt != KING) {
                    U64 a = pt == KNIGHT ? T::knight[s] : pt == BISHOP ? T::bishopAtt(s, p.all)
                          : pt == ROOK ? T::rookAtt(s, p.all) : T::bishopAtt(s, p.all) | T::rookAtt(s, p.all);
                    if (a & zone) { attackers[c]++; danger[c] += atkWeight[pt] * popcnt(a & zone); }
                    int mob = popcnt(a & ~p.occ[c] & ~pawnAtt[them]);
                    switch (pt) {
                        case KNIGHT: mg[c] += 4 * (mob - 4); eg[c] += 4 * (mob - 4); break;
                        case BISHOP: mg[c] += 5 * (mob - 6); eg[c] += 5 * (mob - 6); break;
                        case ROOK: {
                            mg[c] += 2 * (mob - 7); eg[c] += 4 * (mob - 7);
                            U64 fm = T::file[fileOf(s)];
                            if (!(fm & ownPawns)) { if (!(fm & theirPawns)) { mg[c] += 25; eg[c] += 10; } else { mg[c] += 12; eg[c] += 5; } }
                            int rr = c == WHITE ? rankOf(s) : 7 - rankOf(s);
                            if (rr == 6) { mg[c] += 10; eg[c] += 20; }
                            break;
                        }
                        case QUEEN: mg[c] += 1 * (mob - 14); eg[c] += 2 * (mob - 14); break;
                    }
                }
            }
        }
        if (popcnt(p.pcs[c * 6 + BISHOP]) >= 2) { mg[c] += 30; eg[c] += 50; }
        // pawn shield for a castled-ish king
        int kf = fileOf(ourK), kr = c == WHITE ? rankOf(ourK) : 7 - rankOf(ourK);
        if (kr <= 1 && (kf <= 2 || kf >= 5)) {
            U64 shieldFiles = T::file[kf] | T::adjFiles[kf];
            U64 near = c == WHITE ? (T::rank[rankOf(ourK) + 1] | (rankOf(ourK) + 2 < 8 ? T::rank[rankOf(ourK) + 2] : 0))
                                  : (T::rank[rankOf(ourK) - 1] | (rankOf(ourK) - 2 >= 0 ? T::rank[rankOf(ourK) - 2] : 0));
            int shield = popcnt(ownPawns & shieldFiles & near);
            mg[c] += 12 * std::min(shield, 3) - 18;
        }
    }
    for (int c = 0; c < 2; c++) {
        // danger[c] = how much c attacks the enemy king
        if (attackers[c] >= 2 && p.pcs[c * 6 + QUEEN]) {
            int d = danger[c];
            mg[c] += std::min(d * d / 3, 500);
        }
    }
    if (phase > 24) phase = 24;
    int score = ((mg[WHITE] - mg[BLACK]) * phase + (eg[WHITE] - eg[BLACK]) * (24 - phase)) / 24;

    int strong = score > 0 ? WHITE : BLACK, weak = strong ^ 1;
    // mop-up: help convert lone-king endings
    if (!p.pcs[weak * 6 + PAWN] && npm[weak] == 0 && npm[strong] >= 500) {
        int wk = p.kingSq(weak), sk = p.kingSq(strong);
        int mop = 20 * centerDist(wk) + 8 * (7 - T::dist[wk][sk]);
        score += strong == WHITE ? mop : -mop;
    }
    // drawish: stronger side has no pawns and only a minor's worth of extra material
    if (!p.pcs[strong * 6 + PAWN] && npm[strong] - npm[weak] < 400) score /= 4;

    score += p.side == WHITE ? 10 : -10;  // tempo
    return p.side == WHITE ? score : -score;
}

inline int see(const Position& p, Move m) {
    int f = mFrom(m), t = mTo(m);
    if (mFlag(m) == F_EP) return 0;
    int gain[32], d = 0;
    gain[0] = p.board[t] == EMPTY ? 0 : seeVal[pType(p.board[t])];
    int lastVal = seeVal[pType(p.board[f])];
    if (isPromo(m)) { gain[0] += seeVal[QUEEN] - seeVal[PAWN]; lastVal = seeVal[QUEEN]; }
    U64 occ = p.all ^ bit(f);
    int side = p.side ^ 1;
    while (true) {
        U64 att = p.attackersTo(t, occ) & occ & p.occ[side];
        if (!att) break;
        int pt, s = -1;
        for (pt = PAWN; pt <= KING; pt++) { U64 x = att & p.pcs[side * 6 + pt]; if (x) { s = lsb(x); break; } }
        d++;
        gain[d] = lastVal - gain[d - 1];
        if (std::max(-gain[d - 1], gain[d]) < 0) break;
        if (pt == KING && (p.attackersTo(t, occ ^ bit(s)) & (occ ^ bit(s)) & p.occ[side ^ 1])) { d--; break; }
        occ ^= bit(s);
        lastVal = seeVal[pt];
        side ^= 1;
        if (d >= 30) break;
    }
    while (--d > 0) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}
}  // namespace Eval

const int INF = 32000, MATE = 31000, MATE_BOUND = 30000, MAXPLY = 128;

struct TTEntry { U64 key; Move move; int16_t score; int8_t depth; uint8_t flag; };
enum { TT_NONE, TT_EXACT, TT_LOWER, TT_UPPER };

struct SearchLimits {
    int movetimeMs = 1000;   // hard time per move (0 = unlimited)
    int softMs = 0;          // stop starting new iterations after this (0 = 60% of movetime)
    int maxDepth = 64;
    bool verbose = false;
    bool uciInfo = true;     // true: UCI "info" lines, false: xboard thinking output
};

struct SearchResult { Move best = 0; int score = 0; int depth = 0; uint64_t nodes = 0; std::string pv; };

class Searcher {
public:
    explicit Searcher(int ttMB = 64) { resizeTT(ttMB); }
    void resizeTT(int mb) {
        size_t n = 1;
        while (n * 2 * sizeof(TTEntry) <= (size_t)mb * 1024 * 1024) n *= 2;
        tt.assign(n, TTEntry{});
        ttMask = n - 1;
    }
    void clear() {
        std::fill(tt.begin(), tt.end(), TTEntry{});
        memset(history, 0, sizeof history);
        memset(killers, 0, sizeof killers);
    }

    SearchResult think(const Position& root, const SearchLimits& lim) {
        pos = root;
        stop = false; nodes = 0;
        start = std::chrono::steady_clock::now();
        hardMs = lim.movetimeMs;
        int soft = lim.softMs ? lim.softMs : (lim.movetimeMs ? lim.movetimeMs * 6 / 10 : 0);
        memset(killers, 0, sizeof killers);
        for (auto& a : history) for (auto& b : a) for (auto& c : b) c /= 8;

        SearchResult res;
        std::vector<Move> legal = pos.legalMoves();
        if (legal.empty()) return res;
        res.best = legal[0];
        if (legal.size() == 1 && lim.movetimeMs) { res.score = 0; return res; }

        int prevScore = 0;
        for (int depth = 1; depth <= lim.maxDepth && depth < MAXPLY - 4; depth++) {
            rootBest = 0;
            int score;
            if (depth >= 5) {
                int window = 25;
                int alpha = prevScore - window, beta = prevScore + window;
                while (true) {
                    score = search(depth, alpha, beta, 0, true);
                    if (stop) break;
                    if (score <= alpha) { alpha = std::max(-INF, alpha - window); window *= 2; }
                    else if (score >= beta) { beta = std::min(INF, beta + window); window *= 2; }
                    else break;
                    if (window > 1000) { alpha = -INF; beta = INF; }
                }
            } else score = search(depth, -INF, INF, 0, true);

            if (rootBest) res.best = rootBest;   // move fully searched and best so far, even if iteration aborted
            if (stop) break;
            prevScore = score;
            res.score = score; res.depth = depth; res.nodes = nodes;
            res.pv = pvString(res.best);
            int ms = elapsed();
            if (lim.verbose) {
                if (lim.uciInfo) {
                    std::string sc = std::abs(score) >= MATE_BOUND
                        ? "mate " + std::to_string(score > 0 ? (MATE - score + 1) / 2 : -(MATE + score) / 2)
                        : "cp " + std::to_string(score);
                    printf("info depth %d score %s time %d nodes %llu nps %llu pv %s\n", depth, sc.c_str(), ms,
                           (unsigned long long)nodes, (unsigned long long)(ms ? nodes * 1000 / ms : 0), res.pv.c_str());
                } else {
                    printf("%d %d %d %llu %s\n", depth, score, ms / 10, (unsigned long long)nodes, res.pv.c_str());
                }
                fflush(stdout);
            }
            if (std::abs(score) >= MATE_BOUND && depth > 8 && MATE - std::abs(score) < depth) break;
            if (soft && ms >= soft) break;
        }
        res.nodes = nodes;
        return res;
    }

private:
    std::vector<TTEntry> tt;
    size_t ttMask = 0;
    Position pos;
    bool stop = false;
    uint64_t nodes = 0;
    int hardMs = 0;
    std::chrono::steady_clock::time_point start;
    Move killers[MAXPLY][2];
    int history[2][64][64];
    Move rootBest = 0;

    int elapsed() const { return (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(); }
    void checkTime() { if (hardMs && elapsed() >= hardMs) stop = true; }

    static int lmr(int d, int m) {
        static int table[64][64];
        static bool init = false;
        if (!init) {
            for (int a = 1; a < 64; a++) for (int b = 1; b < 64; b++) table[a][b] = (int)(0.75 + std::log(a) * std::log(b) / 2.25);
            init = true;
        }
        return table[std::min(d, 63)][std::min(m, 63)];
    }

    TTEntry* probe(U64 k) { return &tt[k & ttMask]; }
    void store(U64 k, Move m, int score, int depth, int flag, int ply) {
        TTEntry* e = probe(k);
        if (score >= MATE_BOUND) score += ply; else if (score <= -MATE_BOUND) score -= ply;
        if (e->key == k && depth < e->depth - 2 && flag != TT_EXACT) return;
        if (!m && e->key == k) m = e->move;
        *e = TTEntry{k, m, (int16_t)score, (int8_t)std::max(0, std::min(depth, 127)), (uint8_t)flag};
    }

    std::string pvString(Move first) {
        std::string s;
        Position p = pos;
        Move m = first;
        for (int i = 0; i < 20 && m; i++) {
            bool ok = false;
            for (Move l : p.legalMoves()) if (l == m) ok = true;
            if (!ok || !p.make(m)) break;
            if (!s.empty()) s += ' ';
            s += Position::uci(m);
            if (p.isRepetition()) break;
            TTEntry* e = probe(p.key);
            m = e->key == p.key ? e->move : 0;
        }
        return s;
    }

    void scoreMoves(MoveList& ml, Move ttMove, int ply) {
        for (int i = 0; i < ml.n; i++) {
            Move m = ml.m[i];
            int& s = ml.score[i];
            if (m == ttMove) s = 2000000;
            else if (isCap(m) || isPromo(m)) {
                int victim = mFlag(m) == F_EP ? PAWN : (pos.board[mTo(m)] == EMPTY ? PAWN : pType(pos.board[mTo(m)]));
                int attacker = pType(pos.board[mFrom(m)]);
                int base = isCap(m) ? Eval::seeVal[victim] * 10 - attacker : 0;
                if (isPromo(m)) base += promoType(m) == QUEEN ? 9000 : -9000;
                s = (Eval::see(pos, m) >= 0 ? 1000000 : -100000) + base;
            } else if (m == killers[ply][0]) s = 900000;
            else if (m == killers[ply][1]) s = 800000;
            else s = history[pos.side][mFrom(m)][mTo(m)];
        }
    }
    static Move pick(MoveList& ml, int i) {
        int b = i;
        for (int j = i + 1; j < ml.n; j++) if (ml.score[j] > ml.score[b]) b = j;
        std::swap(ml.m[i], ml.m[b]); std::swap(ml.score[i], ml.score[b]);
        return ml.m[i];
    }

    int qsearch(int alpha, int beta, int ply) {
        if ((++nodes & 1023) == 0) checkTime();
        if (stop) return 0;
        if (ply >= MAXPLY - 1) return Eval::evaluate(pos);
        bool inCheck = pos.inCheck();
        int best = -INF, stand = 0;
        if (!inCheck) {
            stand = Eval::evaluate(pos);
            if (stand >= beta) return stand;
            if (stand > alpha) alpha = stand;
            best = stand;
        }
        MoveList ml;
        pos.genMoves(ml, !inCheck);
        scoreMoves(ml, 0, ply);
        int legal = 0;
        for (int i = 0; i < ml.n; i++) {
            Move m = pick(ml, i);
            if (!inCheck) {
                if (isPromo(m) && promoType(m) != QUEEN) continue;
                if (ml.score[i] < 0) continue;  // losing capture by SEE
                if (!isPromo(m)) {
                    int victim = mFlag(m) == F_EP ? PAWN : pType(pos.board[mTo(m)]);
                    if (stand + Eval::seeVal[victim] + 200 < alpha) continue;
                }
            }
            if (!pos.make(m)) continue;
            legal++;
            int score = -qsearch(-beta, -alpha, ply + 1);
            pos.unmake();
            if (stop) return 0;
            if (score > best) {
                best = score;
                if (score > alpha) { alpha = score; if (score >= beta) break; }
            }
        }
        if (inCheck && legal == 0) return -MATE + ply;
        return best;
    }

    int search(int depth, int alpha, int beta, int ply, bool doNull) {
        bool pvNode = beta - alpha > 1;
        bool root = ply == 0;
        if (!root) {
            if (pos.half >= 100 || pos.isRepetition() || pos.insufficient()) return 0;
            alpha = std::max(alpha, -MATE + ply);
            beta = std::min(beta, MATE - ply - 1);
            if (alpha >= beta) return alpha;
        }
        bool inCheck = pos.inCheck();
        if (inCheck) depth++;
        if (depth <= 0) return qsearch(alpha, beta, ply);
        if ((++nodes & 1023) == 0) checkTime();
        if (stop) return 0;
        if (ply >= MAXPLY - 2) return Eval::evaluate(pos);

        Move ttMove = 0;
        TTEntry* e = probe(pos.key);
        if (e->key == pos.key) {
            ttMove = e->move;
            if (!pvNode && !root && e->depth >= depth) {
                int s = e->score;
                if (s >= MATE_BOUND) s -= ply; else if (s <= -MATE_BOUND) s += ply;
                if (e->flag == TT_EXACT || (e->flag == TT_LOWER && s >= beta) || (e->flag == TT_UPPER && s <= alpha)) return s;
            }
        }

        int staticEval = inCheck ? -INF : Eval::evaluate(pos);
        if (!pvNode && !inCheck) {
            if (depth <= 6 && staticEval - 80 * depth >= beta && std::abs(beta) < MATE_BOUND) return staticEval;
            if (doNull && depth >= 3 && staticEval >= beta && pos.hasNonPawn(pos.side)) {
                int R = 3 + depth / 6;
                pos.makeNull();
                int s = -search(depth - 1 - R, -beta, -beta + 1, ply + 1, false);
                pos.unmakeNull();
                if (stop) return 0;
                if (s >= beta) return s >= MATE_BOUND ? beta : s;
            }
        }
        if (!ttMove && depth >= 4 && !root) depth--;  // internal iterative reduction

        MoveList ml;
        pos.genMoves(ml, false);
        scoreMoves(ml, ttMove, ply);
        int legal = 0, best = -INF, origAlpha = alpha;
        Move bestMove = 0;
        Move quiets[256]; int nq = 0;
        for (int i = 0; i < ml.n; i++) {
            Move m = pick(ml, i);
            bool quiet = !isCap(m) && !isPromo(m);
            if (!root && !inCheck && quiet && best > -MATE_BOUND) {
                if (depth <= 4 && nq >= 3 + depth * depth * 2) continue;              // late move pruning
                if (depth <= 3 && staticEval + 90 + 90 * depth <= alpha) continue;    // futility
            }
            if (!root && !inCheck && !quiet && best > -MATE_BOUND && depth <= 4 && ml.score[i] < 0) continue;  // bad captures at low depth
            if (!pos.make(m)) continue;
            legal++;
            bool givesCheck = pos.inCheck();
            int newDepth = depth - 1;
            int score;
            if (legal == 1) score = -search(newDepth, -beta, -alpha, ply + 1, true);
            else {
                int R = 0;
                if (depth >= 3 && quiet && !givesCheck && !inCheck && legal > 2) {
                    R = lmr(depth, legal);
                    if (pvNode) R--;
                    if (m == killers[ply][0] || m == killers[ply][1]) R--;
                    R -= history[pos.side ^ 1][mFrom(m)][mTo(m)] / 6000;
                    R = std::max(0, std::min(R, newDepth - 1));
                }
                score = -search(newDepth - R, -alpha - 1, -alpha, ply + 1, true);
                if (score > alpha && R > 0) score = -search(newDepth, -alpha - 1, -alpha, ply + 1, true);
                if (score > alpha && score < beta) score = -search(newDepth, -beta, -alpha, ply + 1, true);
            }
            pos.unmake();
            if (stop) return 0;
            if (score > best) {
                best = score; bestMove = m;
                if (score > alpha) {
                    alpha = score;
                    if (root) rootBest = m;
                    if (score >= beta) {
                        if (quiet) {
                            if (killers[ply][0] != m) { killers[ply][1] = killers[ply][0]; killers[ply][0] = m; }
                            int bonus = std::min(depth * depth * 4, 1600);
                            auto upd = [&](Move q, int b) {
                                int& h = history[pos.side][mFrom(q)][mTo(q)];
                                h += b - h * std::abs(b) / 16384;
                            };
                            upd(m, bonus);
                            for (int j = 0; j < nq; j++) upd(quiets[j], -bonus);
                        }
                        break;
                    }
                }
            }
            if (quiet && nq < 256) quiets[nq++] = m;
        }
        if (legal == 0) return inCheck ? -MATE + ply : 0;
        store(pos.key, bestMove, best, depth, best >= beta ? TT_LOWER : best > origAlpha ? TT_EXACT : TT_UPPER, ply);
        return best;
    }
};
