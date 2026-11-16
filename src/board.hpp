// board.hpp - bitboard position, move generation, make/unmake, SAN/FEN helpers.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <cstdlib>
#include <algorithm>

typedef uint64_t U64;
typedef uint16_t Move;

enum { WHITE = 0, BLACK = 1 };
enum { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { EMPTY = 12 };
// move flags
enum { F_QUIET = 0, F_DOUBLE = 1, F_OO = 2, F_OOO = 3, F_CAP = 4, F_EP = 5,
       F_PN = 8, F_PB = 9, F_PR = 10, F_PQ = 11, F_PCN = 12, F_PCB = 13, F_PCR = 14, F_PCQ = 15 };

inline int mFrom(Move m) { return m & 63; }
inline int mTo(Move m) { return (m >> 6) & 63; }
inline int mFlag(Move m) { return m >> 12; }
inline Move mk(int f, int t, int fl) { return Move(f | (t << 6) | (fl << 12)); }
inline bool isCap(Move m) { int f = mFlag(m); return f == F_CAP || f == F_EP || f >= F_PCN; }
inline bool isPromo(Move m) { return mFlag(m) >= F_PN; }
inline int promoType(Move m) { return KNIGHT + (mFlag(m) & 3); }

inline int lsb(U64 b) { return __builtin_ctzll(b); }
inline int msb(U64 b) { return 63 - __builtin_clzll(b); }
inline int popcnt(U64 b) { return __builtin_popcountll(b); }
inline int poplsb(U64& b) { int s = lsb(b); b &= b - 1; return s; }
inline U64 bit(int s) { return 1ULL << s; }
inline int pType(int pc) { return pc % 6; }
inline int pColor(int pc) { return pc / 6; }
inline int fileOf(int s) { return s & 7; }
inline int rankOf(int s) { return s >> 3; }

namespace T {
inline U64 knight[64], king[64], pawn[2][64], ray[8][64];
inline U64 file[8], rank[8], adjFiles[8], passed[2][64], forward[2][64];
inline U64 zPiece[12][64], zSide, zCastle[16], zEp[8];
inline int castleMask[64];
inline int dist[64][64];
// dirs: 0 N,1 E,2 NE,3 NW (positive) 4 S,5 W,6 SE,7 SW (negative)
inline const int dr[8] = {1, 0, 1, 1, -1, 0, -1, -1};
inline const int df[8] = {0, 1, 1, -1, 0, -1, 1, -1};

inline void initOnce() {
    for (int i = 0; i < 8; i++) {
        file[i] = 0x0101010101010101ULL << i;
        rank[i] = 0xFFULL << (8 * i);
    }
    for (int i = 0; i < 8; i++)
        adjFiles[i] = (i > 0 ? file[i - 1] : 0) | (i < 7 ? file[i + 1] : 0);
    for (int s = 0; s < 64; s++) {
        int r = rankOf(s), f = fileOf(s);
        auto add = [&](U64& b, int rr, int ff) { if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) b |= bit(rr * 8 + ff); };
        knight[s] = king[s] = pawn[0][s] = pawn[1][s] = 0;
        int kn[8][2] = {{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}};
        for (auto& d : kn) add(knight[s], r + d[0], f + d[1]);
        for (int a = -1; a <= 1; a++) for (int b = -1; b <= 1; b++) if (a || b) add(king[s], r + a, f + b);
        add(pawn[0][s], r + 1, f - 1); add(pawn[0][s], r + 1, f + 1);
        add(pawn[1][s], r - 1, f - 1); add(pawn[1][s], r - 1, f + 1);
        for (int d = 0; d < 8; d++) {
            ray[d][s] = 0;
            int rr = r + dr[d], ff = f + df[d];
            while (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) { ray[d][s] |= bit(rr * 8 + ff); rr += dr[d]; ff += df[d]; }
        }
        forward[0][s] = ray[0][s];
        forward[1][s] = ray[4][s];
        U64 span0 = forward[0][s], span1 = forward[1][s];
        passed[0][s] = span0 | (f > 0 ? (span0 >> 1) & ~file[7] : 0) | (f < 7 ? (span0 << 1) & ~file[0] : 0);
        passed[1][s] = span1 | (f > 0 ? (span1 >> 1) & ~file[7] : 0) | (f < 7 ? (span1 << 1) & ~file[0] : 0);
        for (int t = 0; t < 64; t++) {
            int a = std::abs(rankOf(t) - r), b = std::abs(fileOf(t) - f);
            dist[s][t] = a > b ? a : b;
        }
        castleMask[s] = 15;
    }
    castleMask[0] = 15 & ~2; castleMask[4] = 15 & ~3; castleMask[7] = 15 & ~1;
    castleMask[56] = 15 & ~8; castleMask[60] = 15 & ~12; castleMask[63] = 15 & ~4;
    U64 seed = 0x9E3779B97F4A7C15ULL;
    auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; };
    for (auto& p : zPiece) for (auto& z : p) z = rnd();
    zSide = rnd();
    for (auto& z : zCastle) z = rnd();
    for (auto& z : zEp) z = rnd();
}
inline void init() { static std::once_flag f; std::call_once(f, initOnce); }

inline U64 rayAtt(int d, int s, U64 occ) {
    U64 a = ray[d][s], b = a & occ;
    if (b) a ^= ray[d][d < 4 ? lsb(b) : msb(b)];
    return a;
}
inline U64 bishopAtt(int s, U64 occ) { return rayAtt(2, s, occ) | rayAtt(3, s, occ) | rayAtt(6, s, occ) | rayAtt(7, s, occ); }
inline U64 rookAtt(int s, U64 occ) { return rayAtt(0, s, occ) | rayAtt(1, s, occ) | rayAtt(4, s, occ) | rayAtt(5, s, occ); }
}  // namespace T

struct MoveList {
    Move m[256];
    int score[256];
    int n = 0;
    void add(Move mv) { m[n++] = mv; }
};

struct Position {
    U64 pcs[12];
    U64 occ[2];
    U64 all;
    int8_t board[64];
    int side, ep, castle, half, fullmove;
    U64 key;
    struct Undo { U64 key; int ep, castle, half, captured; Move move; };
    std::vector<Undo> hist;
    std::vector<U64> keyHist;

    Position() { T::init(); setFEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

    void clear() {
        memset(pcs, 0, sizeof pcs); occ[0] = occ[1] = all = 0;
        for (auto& b : board) b = EMPTY;
        side = WHITE; ep = -1; castle = 0; half = 0; fullmove = 1; key = 0;
        hist.clear(); keyHist.clear();
    }
    void addPiece(int s, int pc) { pcs[pc] |= bit(s); occ[pColor(pc)] |= bit(s); all |= bit(s); board[s] = pc; key ^= T::zPiece[pc][s]; }
    void removePiece(int s) { int pc = board[s]; pcs[pc] ^= bit(s); occ[pColor(pc)] ^= bit(s); all ^= bit(s); board[s] = EMPTY; key ^= T::zPiece[pc][s]; }
    void movePiece(int f, int t) { int pc = board[f]; removePiece(f); addPiece(t, pc); }
    int kingSq(int c) const { return lsb(pcs[c * 6 + KING]); }

    U64 attackersTo(int s, U64 o) const {
        return (T::pawn[BLACK][s] & pcs[PAWN]) | (T::pawn[WHITE][s] & pcs[6 + PAWN]) |
               (T::knight[s] & (pcs[KNIGHT] | pcs[6 + KNIGHT])) | (T::king[s] & (pcs[KING] | pcs[6 + KING])) |
               (T::bishopAtt(s, o) & (pcs[BISHOP] | pcs[6 + BISHOP] | pcs[QUEEN] | pcs[6 + QUEEN])) |
               (T::rookAtt(s, o) & (pcs[ROOK] | pcs[6 + ROOK] | pcs[QUEEN] | pcs[6 + QUEEN]));
    }
    bool attacked(int s, int by) const {
        int o = by * 6;
        if (T::pawn[by ^ 1][s] & pcs[o + PAWN]) return true;
        if (T::knight[s] & pcs[o + KNIGHT]) return true;
        if (T::king[s] & pcs[o + KING]) return true;
        if (T::bishopAtt(s, all) & (pcs[o + BISHOP] | pcs[o + QUEEN])) return true;
        if (T::rookAtt(s, all) & (pcs[o + ROOK] | pcs[o + QUEEN])) return true;
        return false;
    }
    bool inCheck() const { return attacked(kingSq(side), side ^ 1); }
    bool hasNonPawn(int c) const { return pcs[c * 6 + KNIGHT] | pcs[c * 6 + BISHOP] | pcs[c * 6 + ROOK] | pcs[c * 6 + QUEEN]; }

    bool setFEN(const std::string& fen) {
        clear();
        std::istringstream ss(fen);
        std::string bd, stm, cs, eps;
        ss >> bd >> stm >> cs >> eps;
        int r = 7, f = 0;
        const std::string pcsStr = "PNBRQKpnbrqk";
        for (char c : bd) {
            if (c == '/') { r--; f = 0; }
            else if (c >= '1' && c <= '8') f += c - '0';
            else { size_t i = pcsStr.find(c); if (i == std::string::npos || r < 0 || f > 7) return false; addPiece(r * 8 + f, (int)i); f++; }
        }
        side = stm == "b" ? BLACK : WHITE;
        for (char c : cs) castle |= c == 'K' ? 1 : c == 'Q' ? 2 : c == 'k' ? 4 : c == 'q' ? 8 : 0;
        if (eps.size() == 2 && eps[0] >= 'a' && eps[0] <= 'h') ep = (eps[1] - '1') * 8 + (eps[0] - 'a');
        if (!(ss >> half)) half = 0;
        if (!(ss >> fullmove)) fullmove = 1;
        key ^= T::zCastle[castle];
        if (ep != -1) key ^= T::zEp[ep & 7];
        if (side == BLACK) key ^= T::zSide;
        return popcnt(pcs[KING]) == 1 && popcnt(pcs[6 + KING]) == 1;
    }
    std::string fen() const {
        std::string s; const char* p = "PNBRQKpnbrqk";
        for (int r = 7; r >= 0; r--) {
            int e = 0;
            for (int f = 0; f < 8; f++) {
                int pc = board[r * 8 + f];
                if (pc == EMPTY) e++; else { if (e) s += char('0' + e); e = 0; s += p[pc]; }
            }
            if (e) s += char('0' + e);
            if (r) s += '/';
        }
        s += side == WHITE ? " w " : " b ";
        std::string c;
        if (castle & 1) c += 'K'; if (castle & 2) c += 'Q'; if (castle & 4) c += 'k'; if (castle & 8) c += 'q';
        s += c.empty() ? "-" : c;
        s += ' ';
        s += ep == -1 ? "-" : std::string{char('a' + fileOf(ep)), char('1' + rankOf(ep))};
        return s + " " + std::to_string(half) + " " + std::to_string(fullmove);
    }

    void genMoves(MoveList& ml, bool noisyOnly) const {
        int us = side, them = us ^ 1;
        U64 own = occ[us], enemy = occ[them], empty = ~all;
        U64 pawns = pcs[us * 6 + PAWN];
        int up = us == WHITE ? 8 : -8;
        U64 promoRank = us == WHITE ? T::rank[7] : T::rank[0];
        U64 single = us == WHITE ? (pawns << 8) & empty : (pawns >> 8) & empty;
        U64 dbl = us == WHITE ? ((single & T::rank[2]) << 8) & empty : ((single & T::rank[5]) >> 8) & empty;
        for (U64 b = single & promoRank; b;) { int t = poplsb(b); for (int fl = F_PQ; fl >= F_PN; fl--) ml.add(mk(t - up, t, fl)); }
        if (!noisyOnly) {
            for (U64 b = single & ~promoRank; b;) { int t = poplsb(b); ml.add(mk(t - up, t, F_QUIET)); }
            for (U64 b = dbl; b;) { int t = poplsb(b); ml.add(mk(t - 2 * up, t, F_DOUBLE)); }
        }
        for (U64 b = pawns; b;) {
            int f = poplsb(b);
            for (U64 a = T::pawn[us][f] & enemy; a;) {
                int t = poplsb(a);
                if (bit(t) & promoRank) { for (int fl = F_PCQ; fl >= F_PCN; fl--) ml.add(mk(f, t, fl)); }
                else ml.add(mk(f, t, F_CAP));
            }
            if (ep != -1 && (T::pawn[us][f] & bit(ep))) ml.add(mk(f, ep, F_EP));
        }
        for (int pt = KNIGHT; pt <= KING; pt++) {
            for (U64 b = pcs[us * 6 + pt]; b;) {
                int f = poplsb(b);
                U64 a = pt == KNIGHT ? T::knight[f] : pt == BISHOP ? T::bishopAtt(f, all) : pt == ROOK ? T::rookAtt(f, all)
                      : pt == QUEEN ? T::bishopAtt(f, all) | T::rookAtt(f, all) : T::king[f];
                a &= ~own;
                for (U64 c = a & enemy; c;) ml.add(mk(f, poplsb(c), F_CAP));
                if (!noisyOnly) for (U64 q = a & empty; q;) ml.add(mk(f, poplsb(q), F_QUIET));
            }
        }
        if (!noisyOnly) {
            if (us == WHITE) {
                if ((castle & 1) && !(all & (bit(5) | bit(6))) && board[7] == ROOK && !attacked(4, them) && !attacked(5, them) && !attacked(6, them)) ml.add(mk(4, 6, F_OO));
                if ((castle & 2) && !(all & (bit(1) | bit(2) | bit(3))) && board[0] == ROOK && !attacked(4, them) && !attacked(3, them) && !attacked(2, them)) ml.add(mk(4, 2, F_OOO));
            } else {
                if ((castle & 4) && !(all & (bit(61) | bit(62))) && board[63] == 6 + ROOK && !attacked(60, them) && !attacked(61, them) && !attacked(62, them)) ml.add(mk(60, 62, F_OO));
                if ((castle & 8) && !(all & (bit(57) | bit(58) | bit(59))) && board[56] == 6 + ROOK && !attacked(60, them) && !attacked(59, them) && !attacked(58, them)) ml.add(mk(60, 58, F_OOO));
            }
        }
    }

    bool make(Move m) {
        Undo u{key, ep, castle, half, EMPTY, m};
        int f = mFrom(m), t = mTo(m), fl = mFlag(m);
        int pc = board[f], us = side, them = us ^ 1;
        keyHist.push_back(key);
        if (ep != -1) key ^= T::zEp[ep & 7];
        ep = -1;
        half++;
        if (fl == F_EP) { int cs = t + (us == WHITE ? -8 : 8); u.captured = board[cs]; removePiece(cs); }
        else if (board[t] != EMPTY) { u.captured = board[t]; removePiece(t); }
        if (u.captured != EMPTY || pType(pc) == PAWN) half = 0;
        movePiece(f, t);
        if (fl >= F_PN) { removePiece(t); addPiece(t, us * 6 + promoType(m)); }
        else if (fl == F_DOUBLE) {
            int e = (f + t) / 2;
            if (T::pawn[us][e] & pcs[them * 6 + PAWN]) { ep = e; key ^= T::zEp[e & 7]; }
        } else if (fl == F_OO) movePiece(t + 1, t - 1);
        else if (fl == F_OOO) movePiece(t - 2, t + 1);
        key ^= T::zCastle[castle];
        castle &= T::castleMask[f] & T::castleMask[t];
        key ^= T::zCastle[castle];
        side ^= 1; key ^= T::zSide;
        if (us == BLACK) fullmove++;
        hist.push_back(u);
        if (attacked(kingSq(us), them)) { unmake(); return false; }
        return true;
    }
    void unmake() {
        Undo u = hist.back(); hist.pop_back();
        keyHist.pop_back();
        side ^= 1;
        int us = side;
        if (us == BLACK) fullmove--;
        Move m = u.move;
        int f = mFrom(m), t = mTo(m), fl = mFlag(m);
        if (fl >= F_PN) { removePiece(t); addPiece(t, us * 6 + PAWN); }
        movePiece(t, f);
        if (u.captured != EMPTY) addPiece(fl == F_EP ? t + (us == WHITE ? -8 : 8) : t, u.captured);
        if (fl == F_OO) movePiece(t - 1, t + 1);
        else if (fl == F_OOO) movePiece(t + 1, t - 2);
        key = u.key; ep = u.ep; castle = u.castle; half = u.half;
    }
    void makeNull() {
        hist.push_back(Undo{key, ep, castle, half, EMPTY, 0});
        keyHist.push_back(key);
        if (ep != -1) key ^= T::zEp[ep & 7];
        ep = -1; half++; side ^= 1; key ^= T::zSide;
    }
    void unmakeNull() {
        Undo u = hist.back(); hist.pop_back(); keyHist.pop_back();
        side ^= 1; key = u.key; ep = u.ep; half = u.half;
    }

    bool isRepetition() const {
        int n = (int)keyHist.size();
        for (int i = n - 2; i >= 0 && i >= n - half; i -= 2) if (keyHist[i] == key) return true;
        return false;
    }
    int repetitionCount() const {
        int n = (int)keyHist.size(), c = 0;
        for (int i = n - 2; i >= 0 && i >= n - half; i -= 2) if (keyHist[i] == key) c++;
        return c;
    }
    bool insufficient() const {
        if (pcs[PAWN] | pcs[6 + PAWN] | pcs[ROOK] | pcs[6 + ROOK] | pcs[QUEEN] | pcs[6 + QUEEN]) return false;
        int minors = popcnt(pcs[KNIGHT] | pcs[6 + KNIGHT] | pcs[BISHOP] | pcs[6 + BISHOP]);
        return minors <= 1;
    }

    std::vector<Move> legalMoves() {
        MoveList ml; genMoves(ml, false);
        std::vector<Move> v;
        for (int i = 0; i < ml.n; i++) if (make(ml.m[i])) { unmake(); v.push_back(ml.m[i]); }
        return v;
    }

    static std::string sq(int s) { return std::string{char('a' + fileOf(s)), char('1' + rankOf(s))}; }
    static std::string uci(Move m) {
        if (!m) return "0000";
        std::string s = sq(mFrom(m)) + sq(mTo(m));
        if (isPromo(m)) s += "nbrq"[promoType(m) - KNIGHT];
        return s;
    }
    // parse coordinate move (e2e4, e7e8q, also tolerates e7e8=Q); returns 0 if illegal
    Move parseUCI(std::string s) {
        std::string c;
        for (char ch : s) if (ch != '=' && ch != '-' && ch != 'x') c += (char)tolower(ch);
        if (c.size() < 4) return 0;
        for (Move m : legalMoves()) {
            std::string u = uci(m);
            if (u == c || (c.size() == 4 && u.size() == 5 && u.substr(0, 4) == c && u[4] == 'q')) return m;
        }
        return 0;
    }
    std::string san(Move m) {
        int f = mFrom(m), t = mTo(m), fl = mFlag(m), pt = pType(board[f]);
        std::string s;
        if (fl == F_OO) s = "O-O";
        else if (fl == F_OOO) s = "O-O-O";
        else if (pt == PAWN) {
            if (isCap(m)) { s += char('a' + fileOf(f)); s += 'x'; }
            s += sq(t);
            if (isPromo(m)) { s += '='; s += "NBRQ"[promoType(m) - KNIGHT]; }
        } else {
            s += "PNBRQK"[pt];
            bool amb = false, sameFile = false, sameRank = false;
            for (Move o : legalMoves()) {
                if (o == m || mTo(o) != t || mFrom(o) == f || pType(board[mFrom(o)]) != pt) continue;
                amb = true;
                if (fileOf(mFrom(o)) == fileOf(f)) sameFile = true;
                if (rankOf(mFrom(o)) == rankOf(f)) sameRank = true;
            }
            if (amb) {
                if (!sameFile) s += char('a' + fileOf(f));
                else if (!sameRank) s += char('1' + rankOf(f));
                else s += sq(f);
            }
            if (isCap(m)) s += 'x';
            s += sq(t);
        }
        if (make(m)) {
            if (inCheck()) s += legalMoves().empty() ? "#" : "+";
            unmake();
        }
        return s;
    }
};
