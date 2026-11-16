// arena.cpp - plays Fighter (in-process) against Chess.app's own engine (sjeng.ChessEngine, xboard protocol).
#include "search.hpp"
#include <atomic>
#include <thread>
#include <mutex>
#include <fstream>
#include <ctime>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* DEFAULT_SJENG = "/System/Applications/Chess.app/Contents/Resources/sjeng.ChessEngine";
static const char* CHESS_RES = "/System/Applications/Chess.app/Contents/Resources";

static const std::vector<std::pair<std::string, std::string>> OPENINGS = {
    {"Ruy Lopez", "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6"},
    {"Sicilian Najdorf", "e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6"},
    {"Queen's Gambit Declined", "d2d4 d7d5 c2c4 e7e6 b1c3 g8f6"},
    {"King's Indian", "d2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6"},
    {"French", "e2e4 e7e6 d2d4 d7d5 b1c3 g8f6"},
    {"Caro-Kann Advance", "e2e4 c7c6 d2d4 d7d5 e4e5 c8f5"},
    {"English", "c2c4 e7e5 b1c3 g8f6 g2g3 d7d5"},
    {"Reti", "g1f3 d7d5 g2g3 g8f6 f1g2 c7c6"},
    {"Italian", "e2e4 e7e5 g1f3 b8c6 f1c4 f8c5"},
    {"Nimzo-Indian", "d2d4 g8f6 c2c4 e7e6 b1c3 f8b4"},
    {"Closed Sicilian", "e2e4 c7c5 b1c3 b8c6 g2g3 g7g6"},
    {"Slav", "d2d4 d7d5 c2c4 c7c6 g1f3 g8f6"},
    {"Pirc", "e2e4 d7d6 d2d4 g8f6 b1c3 g7g6"},
    {"Petroff", "e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4"},
    {"Dutch", "d2d4 f7f5 g2g3 g8f6 f1g2 g7g6"},
    {"Sicilian Taimanov", "e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6"},
};

struct Config {
    int games = 16, concurrency = 4, stSec = 1, sd = 0, myMs = 0, maxPlies = 400;
    bool ponder = false, openings = true, verbose = false, watch = false, ascii = false;
    int watchPauseMs = 4000, scale = 2;
    std::string sjeng = DEFAULT_SJENG, pgn = "games.pgn", workdir, logDir;
};

class Proc {
public:
    pid_t pid = -1;
    int to = -1, from = -1;
    std::string buf;
    bool start(const std::string& exe, const std::string& cwd) {
        int a[2], b[2];
        if (pipe(a) || pipe(b)) return false;
        pid = fork();
        if (pid == 0) {
            dup2(a[0], 0); dup2(b[1], 1); dup2(b[1], 2);
            close(a[0]); close(a[1]); close(b[0]); close(b[1]);
            if (chdir(cwd.c_str()) != 0) _exit(126);
            execl(exe.c_str(), exe.c_str(), (char*)nullptr);
            _exit(127);
        }
        close(a[0]); close(b[1]);
        to = a[1]; from = b[0];
        return pid > 0;
    }
    FILE* log = nullptr;
    void send(const std::string& s) {
        if (log) fprintf(log, ">> %s\n", s.c_str());
        std::string x = s + "\n";
        ssize_t r = write(to, x.data(), x.size());
        (void)r;
    }
    // returns false on timeout / EOF
    bool readLine(std::string& line, int timeoutMs) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            size_t nl = buf.find('\n');
            if (nl != std::string::npos) { line = buf.substr(0, nl); buf.erase(0, nl + 1); if (!line.empty() && line.back() == '\r') line.pop_back(); return true; }
            int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) return false;
            pollfd p{from, POLLIN, 0};
            if (poll(&p, 1, left) <= 0) continue;
            char tmp[4096];
            ssize_t n = read(from, tmp, sizeof tmp);
            if (n <= 0) return false;
            buf.append(tmp, n);
        }
    }
    void stop() {
        if (pid <= 0) return;
        send("quit");
        close(to);
        for (int i = 0; i < 20; i++) { if (waitpid(pid, nullptr, WNOHANG) == pid) { pid = -1; break; } usleep(50000); }
        if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, nullptr, 0); }
        close(from);
        pid = -1;
    }
};

struct GameResult { int fighterPoints2 = 0; std::string result, reason, pgn; double sjengAvgMs = 0, myAvgMs = 0; int plies = 0; };

static std::vector<std::string> split(const std::string& s) {
    std::istringstream ss(s); std::vector<std::string> v; std::string t;
    while (ss >> t) v.push_back(t);
    return v;
}


// ---------------------------------------------------------------- live terminal view (--watch)
struct WatchInfo {
    std::string white, black, opening, status, tally;
    int round = 0;
    Move last = 0;
    std::string fEval = "-", sEval = "-";   // already formatted, White's point of view
    double fMs = 0, sMs = 0;
    std::string fMove, sMove;
};
static std::string g_tally = "Fighter +0 =0 -0";

static std::string fmtEval(int cpWhitePov) {
    char b[32];
    if (std::abs(cpWhitePov) >= MATE_BOUND) {
        int n = (MATE - std::abs(cpWhitePov) + 1) / 2;
        snprintf(b, sizeof b, "%sM%d", cpWhitePov > 0 ? "+" : "-", n);
    } else snprintf(b, sizeof b, "%+.2f", cpWhitePov / 100.0);
    return b;
}

static void render(const Config& cfg, const Position& pos, const std::vector<std::string>& sans, const WatchInfo& w) {
    static const char* glyph[12] = {"♟︎", "♞", "♝", "♜", "♛", "♚",
                                    "♟︎", "♞", "♝", "♜", "♛", "♚"};
    std::vector<std::string> side;
    side.push_back("\033[1mGame " + std::to_string(w.round) + "\033[0m  " + w.opening);
    side.push_back("");
    side.push_back("\033[1m□ White\033[0m  " + w.white);
    side.push_back("\033[1m■ Black\033[0m  " + w.black);
    side.push_back("");
    char buf[160];
    snprintf(buf, sizeof buf, "Fighter  last %-7s eval %-8s %5.0f ms", w.fMove.c_str(), w.fEval.c_str(), w.fMs);
    side.push_back(buf);
    snprintf(buf, sizeof buf, "Sjeng    last %-7s eval %-8s %5.0f ms", w.sMove.c_str(), w.sEval.c_str(), w.sMs);
    side.push_back(buf);
    side.push_back("\033[2m(evals from White's point of view)\033[0m");
    side.push_back("");
    side.push_back("\033[1;33m" + w.status + "\033[0m");
    side.push_back("");
    int nFull = ((int)sans.size() + 1) / 2;
    for (int i = std::max(0, nFull - 8); i < nFull; i++) {
        snprintf(buf, sizeof buf, "%3d. %-8s %-8s", i + 1, sans[2 * i].c_str(), 2 * i + 1 < (int)sans.size() ? sans[2 * i + 1].c_str() : "");
        side.push_back(buf);
    }
    while (side.size() < 21) side.push_back("");
    side.push_back("Match so far: " + w.tally);

    std::string out = "\033[H\033[2J";
    int from = w.last ? mFrom(w.last) : -1, to = w.last ? mTo(w.last) : -1;
    int checkSq = pos.inCheck() ? pos.kingSq(pos.side) : -1;
    int row = 0;
    auto line = [&](const std::string& left) {
        out += left + "    " + (row < (int)side.size() ? side[row] : "") + "\n";
        row++;
    };
    line("");
    const int sc = std::max(1, std::min(cfg.scale, 4));
    const int sqW = 3 * sc;                       // characters per square
    for (int r = 7; r >= 0; r--) {
        for (int sub = 0; sub < sc; sub++) {
            bool mid = sub == (sc - 1) / 2;
            std::string l = mid ? " " + std::string(1, char('1' + r)) + " " : "   ";
            for (int f = 0; f < 8; f++) {
                int s = r * 8 + f, pc = pos.board[s];
                bool light = (r + f) % 2 == 1;
                int bg = s == checkSq ? 167 : (s == from || s == to) ? (light ? 186 : 143) : (light ? 223 : 137);
                l += "\033[48;5;" + std::to_string(bg) + "m";
                if (pc == EMPTY || !mid) l += std::string(sqW, ' ');
                else {
                    bool white = pColor(pc) == WHITE;
                    l += white ? "\033[1;38;5;231m" : "\033[38;5;16m";
                    l += std::string((sqW - 1) / 2, ' ');
                    l += cfg.ascii ? std::string(1, "PNBRQKpnbrqk"[pc]) : std::string(glyph[pc]);
                    l += std::string(sqW - 1 - (sqW - 1) / 2, ' ');
                }
                l += "\033[0m";
            }
            line(l);
        }
    }
    std::string files = "   ";
    for (int f = 0; f < 8; f++) { std::string c(sqW, ' '); c[(sqW - 1) / 2] = char('a' + f); files += c; }
    line(files);
    while (row < (int)side.size()) line(std::string(3 + 8 * sqW, ' '));
    fputs(out.c_str(), stdout);
    fflush(stdout);
}

static GameResult playGame(const Config& cfg, int round, const std::string& openName, const std::string& openMoves,
                           bool fighterWhite, Searcher& searcher, const std::string& dir) {
    GameResult gr;
    Position pos;
    std::vector<std::string> sans;
    std::vector<std::string> openList = split(openMoves);

    Proc sj;
    FILE* log = cfg.logDir.empty() ? nullptr : fopen((cfg.logDir + "/game" + std::to_string(round) + ".log").c_str(), "w");
    sj.log = log;
    if (!sj.start(cfg.sjeng, dir)) { gr.result = "*"; gr.reason = "could not start sjeng"; return gr; }
    sj.send("xboard"); sj.send("protover 2"); sj.send("new");
    sj.send(cfg.ponder ? "hard" : "easy");
    sj.send("post");
    if (cfg.sd) sj.send("sd " + std::to_string(cfg.sd));
    else sj.send("st " + std::to_string(cfg.stSec));
    sj.send("force");
    for (auto& mv : openList) {
        Move m = pos.parseUCI(mv);
        if (!m) break;
        sans.push_back(pos.san(m));
        pos.make(m);
        sj.send(mv);
    }
    searcher.clear();
    int myMs = cfg.myMs ? cfg.myMs : cfg.stSec * 1000;
    double sjTotal = 0, myTotal = 0; int sjMoves = 0, myMoves = 0;
    std::string pending;  // fighter move not yet sent to sjeng
    bool fighterWins = false, sjengWins = false;
    WatchInfo wi;
    wi.round = round; wi.opening = openName; wi.tally = g_tally;
    wi.white = fighterWhite ? "Fighter (mine)" : "Sjeng (Chess.app)";
    wi.black = fighterWhite ? "Sjeng (Chess.app)" : "Fighter (mine)";

    while (true) {
        auto legal = pos.legalMoves();
        if (legal.empty()) {
            if (pos.inCheck()) { (pos.side == WHITE) == fighterWhite ? sjengWins = true : fighterWins = true; gr.reason = "checkmate"; }
            else gr.reason = "stalemate";
            break;
        }
        if (pos.half >= 100) { gr.reason = "50-move rule"; break; }
        if (pos.repetitionCount() >= 2) { gr.reason = "threefold repetition"; break; }
        if (pos.insufficient()) { gr.reason = "insufficient material"; break; }
        if ((int)sans.size() >= cfg.maxPlies) { gr.reason = "move limit"; break; }

        bool fighterToMove = (pos.side == WHITE) == fighterWhite;
        Move m = 0;
        if (cfg.watch) {
            wi.status = std::string(fighterToMove ? "Fighter" : "Sjeng") + " (" + (pos.side == WHITE ? "White" : "Black") + ") is thinking...";
            if (pos.inCheck()) wi.status += "  CHECK!";
            render(cfg, pos, sans, wi);
        }
        if (fighterToMove) {
            SearchLimits lim; lim.movetimeMs = myMs; lim.verbose = false;
            auto t0 = std::chrono::steady_clock::now();
            SearchResult r = searcher.think(pos, lim);
            myTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); myMoves++;
            m = r.best;
            wi.fMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            wi.fEval = fmtEval(pos.side == WHITE ? r.score : -r.score) + " d" + std::to_string(r.depth);
            wi.fMove = pos.san(m);
            if (cfg.verbose) printf("  [g%d] fighter %s (d%d %+d)\n", round, Position::uci(m).c_str(), r.depth, r.score);
            pending = Position::uci(m);
        } else {
            sj.send("force");
            if (!pending.empty()) { sj.send(pending); pending.clear(); }
            sj.send("go");
            wi.sEval = "book/-";
            auto t0 = std::chrono::steady_clock::now();
            std::string line, mv;
            int budget = cfg.sd ? 60000 : cfg.stSec * 1000 + 50;   // Sjeng treats "st" as soft; enforce equal time with "?"
            auto deadline = t0 + std::chrono::milliseconds(budget);
            bool resigned = false, nudged = false;
            while (true) {
                int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
                if (left <= 0 || !sj.readLine(line, left)) {
                    if (sj.pid <= 0 || nudged) break;
                    sj.send("?");   // xboard "move now"
                    nudged = true;
                    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    if (log) fprintf(log, "!! time up, sent '?'\n");
                    continue;
                }
                if (log) fprintf(log, "<< %s\n", line.c_str());
                auto tok = split(line);
                if (tok.size() >= 2 && tok[0] == "move") { mv = tok[1]; break; }
                if (tok.size() >= 4 && tok[0] == "tellics" && tok[1] == "whisper" && tok[2][0] == 'd') {
                    double e = atof(tok[3].c_str());   // pawns, Sjeng's point of view
                    char b[48]; snprintf(b, sizeof b, "%+.2f %s", pos.side == WHITE ? e : -e, tok[2].c_str());
                    wi.sEval = b;
                }
                if (line.find("resign") != std::string::npos) { resigned = true; break; }
                if (line.rfind("Illegal move", 0) == 0) { gr.reason = "sjeng rejected a move: " + line; break; }
            }
            sjTotal += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); sjMoves++;
            if (resigned) { fighterWins = true; gr.reason = "sjeng resigned"; break; }
            if (mv.empty()) { if (gr.reason.empty()) gr.reason = "sjeng timed out / crashed"; break; }
            m = pos.parseUCI(mv);
            if (!m) { fighterWins = true; gr.reason = "sjeng played illegal move " + mv; break; }
            wi.sMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            wi.sMove = pos.san(m);
            if (cfg.verbose) printf("  [g%d] sjeng   %s\n", round, mv.c_str());
        }
        sans.push_back(pos.san(m));
        pos.make(m);
        wi.last = m;
    }
    sj.stop();
    if (log) fclose(log);

    bool aborted = !fighterWins && !sjengWins && (gr.reason.find("sjeng") != std::string::npos);
    bool whiteWon = fighterWins == fighterWhite && (fighterWins || sjengWins);
    if (aborted) { gr.result = "*"; gr.fighterPoints2 = -1; }
    else if (!fighterWins && !sjengWins) { gr.result = "1/2-1/2"; gr.fighterPoints2 = 1; }
    else { gr.result = whiteWon ? "1-0" : "0-1"; gr.fighterPoints2 = fighterWins ? 2 : 0; }
    gr.plies = (int)sans.size();
    if (cfg.watch) {
        const char* who = gr.fighterPoints2 == 2 ? "FIGHTER WINS" : gr.fighterPoints2 == 0 ? "SJENG WINS" : gr.fighterPoints2 == 1 ? "DRAW" : "ABORTED";
        wi.status = std::string(who) + "  " + gr.result + "  (" + gr.reason + ")";
        render(cfg, pos, sans, wi);
        usleep(cfg.watchPauseMs * 1000);
    }
    gr.sjengAvgMs = sjMoves ? sjTotal / sjMoves : 0;
    gr.myAvgMs = myMoves ? myTotal / myMoves : 0;

    char date[16]; time_t now = time(nullptr); strftime(date, sizeof date, "%Y.%m.%d", localtime(&now));
    std::string tc = cfg.sd ? "sd " + std::to_string(cfg.sd) : std::to_string(cfg.stSec) + "s/move";
    std::ostringstream p;
    p << "[Event \"Fighter vs Chess.app (" << tc << ")\"]\n[Site \"local\"]\n[Date \"" << date << "\"]\n[Round \"" << round << "\"]\n"
      << "[White \"" << (fighterWhite ? "Fighter" : "Sjeng (Chess.app)") << "\"]\n"
      << "[Black \"" << (fighterWhite ? "Sjeng (Chess.app)" : "Fighter") << "\"]\n"
      << "[Result \"" << gr.result << "\"]\n[Opening \"" << openName << "\"]\n[Termination \"" << gr.reason << "\"]\n\n";
    std::string body;
    for (size_t i = 0; i < sans.size(); i++) {
        if (i % 2 == 0) body += std::to_string(i / 2 + 1) + ". ";
        body += sans[i] + " ";
    }
    body += gr.result;
    int col = 0; std::string wrapped;
    for (auto& t : split(body)) { if (col + (int)t.size() > 79) { wrapped += "\n"; col = 0; } else if (col) { wrapped += " "; col++; } wrapped += t; col += (int)t.size(); }
    p << wrapped << "\n\n";
    gr.pgn = p.str();
    return gr;
}

static void usage() {
    printf("arena - Fighter vs Chess.app's engine (Sjeng)\n"
           "  --games N        number of games (default 16, played in color-swapped pairs)\n"
           "  --st SEC         seconds per move for both engines (default 1)\n"
           "  --sd DEPTH       limit Sjeng to a fixed depth instead (Chess.app's weaker levels)\n"
           "  --my-ms MS       override Fighter's time per move\n"
           "  --concurrency N  parallel games (default 4)\n"
           "  --ponder         let Sjeng think on Fighter's time (as in Chess.app)\n"
           "  --no-openings    start every game from the initial position\n"
           "  --pgn FILE       output PGN (default games.pgn) - open it in Chess.app!\n"
           "  --sjeng PATH     engine binary (default: Chess.app's)\n"
           "  --verbose        print every move\n"
           "  --log-dir DIR    write the raw xboard conversation for each game to DIR\n"
           "  --watch          live board in the terminal (plays one game at a time)\n"
           "  --ascii          letters instead of chess glyphs in --watch\n"
           "  --scale N        board size in --watch: 1 small, 2 default, 3-4 huge\n");
}

int main(int argc, char** argv) {
    T::init();
    Config cfg;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--games") cfg.games = atoi(next().c_str());
        else if (a == "--st") cfg.stSec = atoi(next().c_str());
        else if (a == "--sd") cfg.sd = atoi(next().c_str());
        else if (a == "--my-ms") cfg.myMs = atoi(next().c_str());
        else if (a == "--concurrency") cfg.concurrency = atoi(next().c_str());
        else if (a == "--ponder") cfg.ponder = true;
        else if (a == "--no-openings") cfg.openings = false;
        else if (a == "--pgn") cfg.pgn = next();
        else if (a == "--sjeng") cfg.sjeng = next();
        else if (a == "--verbose") cfg.verbose = true;
        else if (a == "--log-dir") cfg.logDir = next();
        else if (a == "--watch") cfg.watch = true;
        else if (a == "--ascii") cfg.ascii = true;
        else if (a == "--scale") cfg.scale = atoi(next().c_str());
        else { usage(); return a == "--help" || a == "-h" ? 0 : 1; }
    }
    if (access(cfg.sjeng.c_str(), X_OK) != 0) { fprintf(stderr, "engine not found: %s\n", cfg.sjeng.c_str()); return 1; }
    cfg.games = std::max(1, cfg.games);
    cfg.concurrency = std::max(1, std::min(cfg.concurrency, cfg.games));
    if (cfg.watch) cfg.concurrency = 1;

    char tmpl[] = "/tmp/fighter-arena-XXXXXX";
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    cfg.workdir = tmpl;

    printf("Fighter vs Sjeng (Chess.app) | %d games | %s | ponder %s | concurrency %d\n", cfg.games,
           cfg.sd ? ("sjeng depth " + std::to_string(cfg.sd) + ", fighter " + std::to_string(cfg.myMs ? cfg.myMs : cfg.stSec * 1000) + "ms").c_str()
                  : (std::to_string(cfg.stSec) + "s/move").c_str(),
           cfg.ponder ? "on" : "off", cfg.concurrency);

    std::ofstream pgn(cfg.pgn);
    std::mutex mu;
    std::atomic<int> next{0};
    int w = 0, d = 0, l = 0, aborted = 0;
    double sjMs = 0, myMs = 0; int timed = 0;

    auto worker = [&](int id) {
        std::string dir = cfg.workdir + "/w" + std::to_string(id);
        mkdir(dir.c_str(), 0755);
        // Sjeng reads its opening book / config from its cwd, just as when Chess.app launches it.
        std::string cmd = "cp " + std::string(CHESS_RES) + "/sjeng.rc " + std::string(CHESS_RES) + "/*.opn " + std::string(CHESS_RES) + "/*.db '" + dir + "' 2>/dev/null";
        if (system(cmd.c_str()) != 0) {}
        Searcher searcher(64);
        while (true) {
            int g = next++;
            if (g >= cfg.games) break;
            int pair = g / 2;
            bool fighterWhite = g % 2 == 0;
            std::string name = "Initial position", moves;
            if (cfg.openings) { auto& o = OPENINGS[pair % OPENINGS.size()]; name = o.first; moves = o.second; }
            GameResult r = playGame(cfg, g + 1, name, moves, fighterWhite, searcher, dir);
            std::lock_guard<std::mutex> lk(mu);
            const char* out = r.fighterPoints2 == 2 ? "WIN " : r.fighterPoints2 == 1 ? "draw" : r.fighterPoints2 == 0 ? "LOSS" : "ABORT";
            if (r.fighterPoints2 == 2) w++; else if (r.fighterPoints2 == 1) d++; else if (r.fighterPoints2 == 0) l++; else aborted++;
            g_tally = "Fighter +" + std::to_string(w) + " =" + std::to_string(d) + " -" + std::to_string(l);
            if (r.fighterPoints2 >= 0) { sjMs += r.sjengAvgMs; myMs += r.myAvgMs; timed++; }
            printf("game %3d  %-5s Fighter as %s  %-7s %-24s %3d plies  (%s)   [+%d =%d -%d]\n", g + 1, out, fighterWhite ? "White" : "Black",
                   r.result.c_str(), name.c_str(), r.plies, r.reason.c_str(), w, d, l);
            fflush(stdout);
            pgn << r.pgn; pgn.flush();
        }
    };
    std::vector<std::thread> th;
    for (int i = 0; i < cfg.concurrency; i++) th.emplace_back(worker, i);
    for (auto& t : th) t.join();

    int n = w + d + l;
    printf("\nFinal: Fighter %d wins, %d draws, %d losses", w, d, l);
    if (aborted) printf(" (%d aborted)", aborted);
    if (n) {
        double s = (w + 0.5 * d) / n;
        printf("  ->  score %.1f%%", 100 * s);
        if (s > 0 && s < 1) printf(", Elo diff %+.0f", -400 * std::log10(1 / s - 1));
        else printf(", Elo diff %s", s >= 1 ? "> +400 (perfect score)" : "< -400");
    }
    if (timed) printf("\nAvg think time per move: Fighter %.0f ms, Sjeng %.0f ms", myMs / timed, sjMs / timed);
    printf("\nPGN written to %s\n", cfg.pgn.c_str());
    std::string rm = "rm -rf '" + cfg.workdir + "'";
    if (system(rm.c_str()) != 0) {}
    return 0;
}
