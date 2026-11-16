// coach.swift - reads the live board from Chess.app via the Accessibility API and asks Fighter for the best move.
//
//   ./bin/coach                 suggest moves for White (you) with 2s of thinking
//   ./bin/coach --color black   you play Black
//   ./bin/coach --time 5        think 5 seconds per suggestion
//   ./bin/coach --dump          print Chess.app's accessibility tree (debugging)
import Cocoa
import ApplicationServices

setvbuf(stdout, nil, _IONBF, 0)

// ---------------------------------------------------------------- options
var myColor = "w"
var thinkMs = 2000
var dumpMode = false
var args = CommandLine.arguments.dropFirst().makeIterator()
while let a = args.next() {
    switch a {
    case "--color": myColor = (args.next() ?? "white").lowercased().hasPrefix("b") ? "b" : "w"
    case "--time": thinkMs = Int((Double(args.next() ?? "2") ?? 2) * 1000)
    case "--dump": dumpMode = true
    default:
        print("usage: coach [--color white|black] [--time SECONDS] [--dump]"); exit(0)
    }
}

// ---------------------------------------------------------------- accessibility
if !AXIsProcessTrusted() {
    let opts = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
    _ = AXIsProcessTrustedWithOptions(opts)
    print("""
    coach needs Accessibility permission to read Chess.app's board.
      System Settings > Privacy & Security > Accessibility
      -> enable the app you're running this from (Terminal / iTerm / ...), then run coach again.
    """)
    exit(1)
}

func attr(_ e: AXUIElement, _ a: String) -> AnyObject? {
    var v: AnyObject?
    return AXUIElementCopyAttributeValue(e, a as CFString, &v) == .success ? v : nil
}
func str(_ e: AXUIElement, _ a: String) -> String { (attr(e, a) as? String) ?? "" }
func children(_ e: AXUIElement) -> [AXUIElement] { (attr(e, kAXChildrenAttribute) as? [AXUIElement]) ?? [] }

func chessApp() -> AXUIElement? {
    guard let app = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.Chess").first else { return nil }
    return AXUIElementCreateApplication(app.processIdentifier)
}

func dump(_ e: AXUIElement, _ d: Int, _ budget: inout Int) {
    budget -= 1
    if budget < 0 || d > 14 { return }
    let line = "\(str(e, kAXRoleAttribute)) title=\"\(str(e, kAXTitleAttribute))\" desc=\"\(str(e, kAXDescriptionAttribute))\" value=\"\((attr(e, kAXValueAttribute)).map { "\($0)" } ?? "")\" help=\"\(str(e, kAXHelpAttribute))\""
    print(String(repeating: "  ", count: d) + line)
    for c in children(e) { dump(c, d + 1, &budget) }
}

let pieceWords: [String: Character] = ["pawn": "p", "knight": "n", "bishop": "b", "rook": "r", "queen": "q", "king": "k"]
let squareRe = try! NSRegularExpression(pattern: "\\b([a-hA-H])\\s?([1-8])\\b")
let pieceRe = try! NSRegularExpression(pattern: "(white|black)\\s+(pawn|knight|bishop|rook|queen|king)", options: .caseInsensitive)

struct Snapshot { var board: [Character?]; var title: String }   // board index: rank*8+file, a1 = 0

// Walk the window, collecting every element that names a square; parse the piece (if any) from the same text.
func readBoard() -> Snapshot? {
    guard let app = chessApp() else { return nil }
    var windows = (attr(app, kAXWindowsAttribute) as? [AXUIElement]) ?? []
    if let main = attr(app, kAXMainWindowAttribute) { windows.insert(main as! AXUIElement, at: 0) }
    for w in windows {
        var board = [Character?](repeating: nil, count: 64)
        var seen = Set<Int>()
        var texts: [String] = []
        var stack: [(AXUIElement, Int)] = [(w, 0)]
        var visited = 0
        while let (e, d) = stack.popLast(), visited < 5000 {
            visited += 1
            var parts: [String] = []
            for p in [str(e, kAXDescriptionAttribute), str(e, kAXTitleAttribute), (attr(e, kAXValueAttribute) as? String) ?? "", str(e, kAXHelpAttribute)]
                where !p.isEmpty && !parts.contains(p) { parts.append(p) }
            let text = parts.joined(separator: " | ")
            if !text.isEmpty { texts.append(text) }
            let ns = text as NSString
            let sqMatches = squareRe.matches(in: text, range: NSRange(location: 0, length: ns.length))
            if sqMatches.count == 1, let m = sqMatches.first {
                let f = Int(ns.substring(with: m.range(at: 1)).lowercased().unicodeScalars.first!.value) - 97
                let r = Int(ns.substring(with: m.range(at: 2)))! - 1
                let sq = r * 8 + f
                if !seen.contains(sq) {
                    seen.insert(sq)
                    if let pm = pieceRe.firstMatch(in: text, range: NSRange(location: 0, length: ns.length)) {
                        let color = ns.substring(with: pm.range(at: 1)).lowercased()
                        var ch = pieceWords[ns.substring(with: pm.range(at: 2)).lowercased()]!
                        if color == "white" { ch = Character(ch.uppercased()) }
                        board[sq] = ch
                    }
                }
            }
            if d < 14 { for c in children(e) { stack.append((c, d + 1)) } }
        }
        if seen.count >= 64 { return Snapshot(board: board, title: str(w, kAXTitleAttribute)) }
    }
    return nil
}

func fenBoard(_ b: [Character?]) -> String {
    var s = ""
    for r in stride(from: 7, through: 0, by: -1) {
        var empty = 0
        for f in 0..<8 {
            if let p = b[r * 8 + f] { if empty > 0 { s += String(empty); empty = 0 }; s.append(p) } else { empty += 1 }
        }
        if empty > 0 { s += String(empty) }
        if r > 0 { s += "/" }
    }
    return s
}

// Castling rights can't be read from the screen; infer them from kings/rooks still on their home squares
// (and never having left them, as far as we've observed).
var lostCastle = Set<Character>()
func castling(_ b: [Character?]) -> String {
    if b[4] != "K" { lostCastle.formUnion(["K", "Q"]) }
    if b[7] != "R" { lostCastle.insert("K") }
    if b[0] != "R" { lostCastle.insert("Q") }
    if b[60] != "k" { lostCastle.formUnion(["k", "q"]) }
    if b[63] != "r" { lostCastle.insert("k") }
    if b[56] != "r" { lostCastle.insert("q") }
    let s = String("KQkq".filter { !lostCastle.contains($0) })
    return s.isEmpty ? "-" : s
}

// ---------------------------------------------------------------- engine (Fighter over UCI)
let enginePath = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent().appendingPathComponent("fighter").path
let proc = Process()
proc.executableURL = URL(fileURLWithPath: enginePath)
let toEngine = Pipe(), fromEngine = Pipe()
proc.standardInput = toEngine
proc.standardOutput = fromEngine
var engineBuf = ""
func send(_ s: String) { toEngine.fileHandleForWriting.write((s + "\n").data(using: .utf8)!) }
func readLine() -> String? {
    while true {
        if let r = engineBuf.range(of: "\n") {
            let line = String(engineBuf[..<r.lowerBound]); engineBuf.removeSubrange(..<r.upperBound); return line
        }
        let d = fromEngine.fileHandleForReading.availableData
        if d.isEmpty { return nil }
        engineBuf += String(decoding: d, as: UTF8.self)
    }
}

// ---------------------------------------------------------------- display
func sqName(_ s: Int) -> String { "\(Character(UnicodeScalar(97 + s % 8)!))\(s / 8 + 1)" }
func sqIndex(_ n: Substring) -> Int { Int(n.unicodeScalars.first!.value) - 97 + (Int(String(n.last!))! - 1) * 8 }

let glyph: [Character: String] = ["K": "\u{265A}", "Q": "\u{265B}", "R": "\u{265C}", "B": "\u{265D}", "N": "\u{265E}", "P": "\u{265F}\u{FE0E}"]
func render(_ b: [Character?], hiFrom: Int?, hiTo: Int?, flip: Bool, lines: [String]) {
    var out = "\u{1B}[H\u{1B}[2J\n"
    var side = lines
    while side.count < 18 { side.append("") }
    var row = 0
    let ranks = flip ? Array(0..<8) : Array((0..<8).reversed())
    let files = flip ? Array((0..<8).reversed()) : Array(0..<8)
    for r in ranks {
        for sub in 0..<2 {
            var l = sub == 0 ? " \(r + 1) " : "   "
            for f in files {
                let s = r * 8 + f
                let light = (r + f) % 2 == 1
                let bg = (s == hiFrom || s == hiTo) ? (light ? 150 : 107) : (light ? 223 : 137)
                l += "\u{1B}[48;5;\(bg)m"
                if sub == 0, let p = b[s] {
                    let white = p.isUppercase
                    l += white ? "\u{1B}[1;38;5;231m" : "\u{1B}[38;5;16m"
                    l += "  " + glyph[Character(p.uppercased())]! + "   "
                } else { l += "      " }
                l += "\u{1B}[0m"
            }
            out += l + "    " + (row < side.count ? side[row] : "") + "\n"
            row += 1
        }
    }
    out += "   " + files.map { "  \(Character(UnicodeScalar(97 + $0)!))   " }.joined() + "\n"
    print(out, terminator: "")
}

// ---------------------------------------------------------------- main loop
if dumpMode {
    guard let app = chessApp() else { print("Chess.app is not running"); exit(1) }
    var budget = 3000
    var wins = (attr(app, kAXWindowsAttribute) as? [AXUIElement]) ?? []
    if let m = attr(app, kAXMainWindowAttribute) { wins.insert(m as! AXUIElement, at: 0) }
    print("windows: \(wins.count)")
    for w in wins { dump(w, 0, &budget) }
    exit(0)
}

do { try proc.run() } catch { print("cannot start engine at \(enginePath): \(error)"); exit(1) }
send("uci"); while let l = readLine(), l != "uciok" {}
send("setoption name Hash value 128")

var lastFEN = ""
var pendingKey = ""
var sideToMove: Character = "w"
var prevBoard: [Character?]? = nil
var noBoardWarned = false
print("coach: waiting for a Chess.app game window...")

while true {
    guard let snap = readBoard() else {
        if !noBoardWarned {
            print("coach: can't see a board in Chess.app yet (is a game window open?). Run with --dump to debug.")
            noBoardWarned = true
        }
        usleep(500_000); continue
    }
    noBoardWarned = false
    let placement = fenBoard(snap.board)
    let t = snap.title.lowercased()
    let titleSide: Character? = t.contains("white to move") ? "w" : t.contains("black to move") ? "b" : nil
    // React when the pieces OR the turn change; require two identical readings in a row, because Chess.app
    // updates the pieces and the title a moment apart while a move animates.
    let stateKey = placement + " " + (titleSide.map { String($0) } ?? "?")
    if stateKey == lastFEN { usleep(250_000); continue }
    if stateKey != pendingKey { pendingKey = stateKey; usleep(200_000); continue }

    // whose turn? Prefer the window title ("White to Move" / "Black to Move"), else infer from what changed.
    if let ts = titleSide { sideToMove = ts }
    else if placement == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR" { sideToMove = "w"; lostCastle = [] }
    else if let prev = prevBoard {
        // the side whose piece appeared on a new square just moved
        for s in 0..<64 where snap.board[s] != nil && snap.board[s] != prev[s] {
            sideToMove = snap.board[s]!.isUppercase ? "b" : "w"; break
        }
    }
    prevBoard = snap.board
    lastFEN = stateKey
    let fen = "\(placement) \(sideToMove) \(castling(snap.board)) - 0 1"
    let flip = myColor == "b"
    let header = "\u{1B}[1mFighter coach\u{1B}[0m — you play \(myColor == "w" ? "White" : "Black")"

    if String(sideToMove) != String(myColor) {
        render(snap.board, hiFrom: nil, hiTo: nil, flip: flip, lines: [header, "", "Waiting for the computer's move..."])
        usleep(250_000); continue
    }
    render(snap.board, hiFrom: nil, hiTo: nil, flip: flip, lines: [header, "", "\u{1B}[1;33mYour move — thinking...\u{1B}[0m"])
    send("position fen \(fen)")
    send("go movetime \(thinkMs)")
    var best = "", score = "", pv = "", depth = ""
    while let l = readLine() {
        if l.hasPrefix("info") {
            let tok = l.split(separator: " ")
            if let i = tok.firstIndex(of: "depth") { depth = String(tok[i + 1]) }
            if let i = tok.firstIndex(of: "score"), i + 2 < tok.count {
                if tok[i + 1] == "mate" { score = "mate in \(tok[i + 2])" }
                else if let cp = Int(tok[i + 2]) { score = String(format: "%+.2f", Double(cp) / 100) }
            }
            if let i = tok.firstIndex(of: "pv") { pv = tok[(i + 1)...].prefix(8).joined(separator: " ") }
        } else if l.hasPrefix("bestmove") {
            best = String(l.split(separator: " ")[1]); break
        }
    }
    if best.count < 4 || best == "0000" {
        render(snap.board, hiFrom: nil, hiTo: nil, flip: flip, lines: [header, "", "No legal moves — game over."])
        continue
    }
    let from = sqIndex(best.prefix(2)), to = sqIndex(best.dropFirst(2).prefix(2))
    let promo = best.count > 4 ? " (promote to \(["q": "queen", "r": "rook", "b": "bishop", "n": "knight"][String(best.last!)] ?? ""))" : ""
    let pieceName = ["P": "pawn", "N": "knight", "B": "bishop", "R": "rook", "Q": "queen", "K": "king"][String(snap.board[from].map { Character($0.uppercased()) } ?? "P")] ?? "piece"
    render(snap.board, hiFrom: from, hiTo: to, flip: flip, lines: [
        header, "",
        "\u{1B}[1;32mSuggested:  \(pieceName) \(sqName(from)) → \(sqName(to))\(promo)\u{1B}[0m",
        "",
        "Eval:   \(score)   (+ = good for you)",
        "Depth:  \(depth)",
        "Line:   \(pv)",
        "",
        "\u{1B}[2mMake your move in Chess.app; I'll update automatically.\u{1B}[0m",
    ])
}
