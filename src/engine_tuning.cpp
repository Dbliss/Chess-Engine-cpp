// ========================= engine_match.cpp =========================
#include "engine.h"
#include "BoardDisplay.h"
#include "zobrist.h"

#include <SFML/Graphics.hpp>
#include <SFML/Audio/Listener.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <atomic>
#include <cctype>
#include <optional>
#include <algorithm>
#include <bit>

// ---------------------------- helpers
static std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

static bool isInt(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return false;
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') ++i;
    bool any = false;
    for (; i < t.size(); ++i) {
        if (!std::isdigit((unsigned char)t[i])) return false;
        any = true;
    }
    return any;
}

static std::vector<std::string> loadFens(const std::string& path, int& outPositions) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Failed to open " << path << "\n";
        outPositions = 0;
        return {};
    }

    std::vector<std::string> raw;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        raw.push_back(line);
    }

    outPositions = (int)raw.size();

    // Optional first line: integer N (how many to use)
    if (!raw.empty() && isInt(raw[0])) {
        outPositions = std::stoi(raw[0]);
        raw.erase(raw.begin());
        if (outPositions < 0) outPositions = 0;
        if (outPositions > (int)raw.size()) outPositions = (int)raw.size();
        raw.resize(outPositions);
    }

    outPositions = (int)raw.size();
    return raw;
}

bool isEndgameDraw(int numWhiteBishops, int numWhiteKnights, int numBlackKnights, int numBlackBishops);

static bool isDrawByMaterial(Board board) {
    int numWhitePawns   = std::popcount(board.whitePawns);
    int numWhiteBishops = std::popcount(board.whiteBishops);
    int numWhiteKnights = std::popcount(board.whiteKnights);
    int numWhiteRooks   = std::popcount(board.whiteRooks);
    int numWhiteQueens  = std::popcount(board.whiteQueens);

    int numBlackPawns   = std::popcount(board.blackPawns);
    int numBlackBishops = std::popcount(board.blackBishops);
    int numBlackKnights = std::popcount(board.blackKnights);
    int numBlackRooks   = std::popcount(board.blackRooks);
    int numBlackQueens  = std::popcount(board.blackQueens);

    // Encourage draws if both sides have no pawns or major pieces left and only up to one minor piece each.
    if (!numWhitePawns && !numBlackPawns &&
        !numWhiteQueens && !numBlackQueens &&
        !numWhiteRooks && !numBlackRooks) {

        // If both sides have at most one minor piece each, this is a draw
        if (isEndgameDraw(numWhiteBishops, numWhiteKnights, numBlackKnights, numBlackBishops) ||
            (numWhiteBishops + numWhiteKnights + numBlackKnights + numBlackBishops <= 1)) {
            return true;
        }
    }
    return false;
}

// ----------------------------
enum class GameResult { WhiteWin, BlackWin, Draw };

struct Stats {
    int aWins = 0;
    int bWins = 0;
    int draws = 0;
    int games = 0;
};

// ----------------------------
// SFML 3: sf::Text has no default ctor -> optional wrapper row widget
struct PanelRow {
    sf::RectangleShape box;
    std::optional<sf::Text> label;
    bool clickable = false;

    bool contains(const sf::Vector2f& p) const {
        return box.getGlobalBounds().contains(p);
    }

    sf::FloatRect bounds() const {
        return box.getGlobalBounds();
    }
};

struct MatchUI {
    float boardPx = 0.f;
    float panelW  = 280.f;
    float panelX  = 0.f;

    bool showBoard = true;
    bool muteSound = false;

    sf::RectangleShape panelBg;

    PanelRow rowDisplay;   // button
    PanelRow rowSound;     // button
    PanelRow rowScore;     // info
    PanelRow rowPosition;  // info
    PanelRow rowGame;      // info
    PanelRow rowThink;     // clickable
    PanelRow rowPaused;    // info
    PanelRow rowControls;  // info (multi-line)

    const sf::Font* font = nullptr;

    void init(float boardPixels, const sf::Font* fontPtr) {
        boardPx = boardPixels;
        panelX  = boardPx;
        font    = fontPtr;

        panelBg.setPosition({ panelX, 0.f });
        panelBg.setSize({ panelW, boardPx });
        panelBg.setFillColor(sf::Color(15, 15, 20));

        auto makeRow = [&](PanelRow& r, float y, int charSize, bool clickable) {
            r.clickable = clickable;

            r.box.setPosition({ panelX + 16.f, y });
            r.box.setSize({ panelW - 32.f, 44.f });
            r.box.setFillColor(sf::Color::Transparent);
            r.box.setOutlineThickness(2.f);
            r.box.setOutlineColor(sf::Color::Green);

            if (font) {
                r.label.emplace(*font, "", (unsigned)charSize);
                r.label->setFillColor(sf::Color::White);
                r.label->setPosition({ panelX + 28.f, y + 10.f });
            }
        };

        // Layout: consistent 44px rows + spacing (same vibe as your old UI)
        float y = 24.f;
        makeRow(rowDisplay,  y, 18, true);  y += 60.f;
        makeRow(rowSound,    y, 18, true);  y += 60.f;

        // Requested order:
        makeRow(rowScore,    y, 18, false); y += 60.f;
        makeRow(rowPosition, y, 18, false); y += 60.f;
        makeRow(rowGame,     y, 18, false); y += 60.f;

        makeRow(rowThink,    y, 18, true);  y += 60.f;
        makeRow(rowPaused,   y, 18, false); y += 60.f;

        // Controls row: same style box, but taller + smaller font
        rowControls.clickable = false;
        rowControls.box.setPosition({ panelX + 16.f, y });
        rowControls.box.setSize({ panelW - 32.f, 120.f });
        rowControls.box.setFillColor(sf::Color::Transparent);
        rowControls.box.setOutlineThickness(2.f);
        rowControls.box.setOutlineColor(sf::Color::Green);
        if (font) {
            rowControls.label.emplace(*font, "", 14);
            rowControls.label->setFillColor(sf::Color::White);
            rowControls.label->setPosition({ panelX + 28.f, y + 10.f });
        }

        refreshStaticLabels();
        applyAudio();
    }

    void refreshStaticLabels() {
        if (rowDisplay.label) rowDisplay.label->setString(std::string("Display: ") + (showBoard ? "ON" : "OFF"));
        if (rowSound.label)   rowSound.label->setString(std::string("Sound: ")   + (muteSound ? "MUTED" : "ON"));

        if (rowControls.label) {
            rowControls.label->setString(
                "Controls:\n"
                "  + / -   : think time\n"
                "  Space   : pause\n"
                "  Esc     : stop\n"
                "  D       : display\n"
                "  M       : mute\n"
                "  Click Think row: left=faster, right=slower"
            );
        }
    }

    void applyAudio() {
        sf::Listener::setGlobalVolume(muteSound ? 0.f : 100.f);
    }

    void toggleDisplay() {
        showBoard = !showBoard;
        refreshStaticLabels();
    }

    void toggleMute() {
        muteSound = !muteSound;
        refreshStaticLabels();
        applyAudio();
    }

    void handleThinkClick(const sf::Vector2f& p,
                          std::atomic<int>& thinkMs,
                          Engine& engineWhite,
                          Engine& engineBlack) {
        auto b = rowThink.bounds();
        float mid = b.position.x + b.size.x * 0.5f;

        int v = thinkMs.load();

        // left half = faster (less think), right half = slower (more think)
        if (p.x < mid) v = std::max(1, v - 10);
        else          v = std::min(20000, v + 10);

        thinkMs.store(v);
        engineWhite.setTimeLimitMs(v);
        engineBlack.setTimeLimitMs(v);
    }

    void handleClick(const sf::Vector2f& mousePos,
                     std::atomic<int>& thinkMs,
                     Engine& engineWhite,
                     Engine& engineBlack) {
        if (rowDisplay.contains(mousePos)) { toggleDisplay(); return; }
        if (rowSound.contains(mousePos))   { toggleMute();   return; }
        if (rowThink.contains(mousePos))   { handleThinkClick(mousePos, thinkMs, engineWhite, engineBlack); return; }
    }

    void draw(sf::RenderWindow& window,
              BoardDisplay* display,
              Board& board,
              int aWins, int draws, int bWins,
              int posIndex1Based,
              int posTotal,
              int gameIndex1Based,
              int gameTotal,
              const std::string& matchupLabel,
              int thinkMsValue,
              bool paused) {

        // dynamic text
        if (rowScore.label) {
            rowScore.label->setString(
                "Score: A " + std::to_string(aWins) +
                "  D " + std::to_string(draws) +
                "  B " + std::to_string(bWins)
            );
        }
        if (rowPosition.label) {
            rowPosition.label->setString("Position: " + std::to_string(posIndex1Based) + " / " + std::to_string(posTotal));
        }
        if (rowGame.label) {
            rowGame.label->setString("Game: " + std::to_string(gameIndex1Based) + " / " + std::to_string(gameTotal));
        }
        if (rowThink.label) {
            rowThink.label->setString("Think(ms): " + std::to_string(thinkMsValue));
        }
        if (rowPaused.label) {
            rowPaused.label->setString(std::string("Paused: ") + (paused ? "YES" : "NO"));
        }

        window.clear();

        // Board area
        if (showBoard && display) {
            display->draw(window);
        } else {
            sf::RectangleShape blank;
            blank.setPosition({ 0.f, 0.f });
            blank.setSize({ boardPx, boardPx });
            blank.setFillColor(sf::Color(0, 0, 0));
            window.draw(blank);
        }

        // Panel background
        window.draw(panelBg);

        auto drawRow = [&](PanelRow& r) {
            window.draw(r.box);
            if (r.label) window.draw(*r.label);
        };

        drawRow(rowDisplay);
        drawRow(rowSound);
        drawRow(rowScore);
        drawRow(rowPosition);
        drawRow(rowGame);
        drawRow(rowThink);
        drawRow(rowPaused);
        drawRow(rowControls);

        // Matchup label drawn under the game label (still inside the Game row box)
        if (font) {
            sf::Text matchup(*font, matchupLabel, 14);
            matchup.setFillColor(sf::Color(190, 190, 190));

            auto gb = rowGame.bounds();
            matchup.setPosition({ gb.position.x + 12.f, gb.position.y + 26.f });
            window.draw(matchup);
        }

        window.display();
    }
};

// ----------------------------
static void applyResult(Stats& s, bool aWasWhite, GameResult r) {
    s.games++;
    if (r == GameResult::Draw) { s.draws++; return; }

    bool whiteWon = (r == GameResult::WhiteWin);
    bool aWon = aWasWhite ? whiteWon : !whiteWon;

    if (aWon) s.aWins++;
    else s.bWins++;
}

// ----------------------------
static GameResult playOne(Board& board,
                         Engine& whiteEngine,
                         Engine& blackEngine,
                         sf::RenderWindow* window,
                         BoardDisplay* display,
                         MatchUI* ui,
                         std::atomic<int>& thinkMs,
                         std::atomic<bool>& paused,
                         std::atomic<bool>& stopRequested,
                         int aWins, int draws, int bWins,
                         int posIndex1Based,
                         int posTotal,
                         int gameIndex1Based,
                         int gameTotal,
                         const std::string& matchupLabel,
                         int maxPlies) {
    whiteEngine.newGame();
    blackEngine.newGame();

    // local repetition tracking (threefold)
    std::unordered_map<uint64_t, int> rep;
    rep.reserve(2048);
    rep[board.generateZobristHash()] = 1;

    if (display && window) {
        display->setupPieces(board);
        if (ui) ui->draw(*window, display, board,
                         aWins, draws, bWins,
                         posIndex1Based, posTotal,
                         gameIndex1Based, gameTotal,
                         matchupLabel, thinkMs.load(), paused.load());
    }

    auto applyThink = [&](int v) {
        v = std::max(1, std::min(20000, v));
        thinkMs.store(v);
        whiteEngine.setTimeLimitMs(v);
        blackEngine.setTimeLimitMs(v);
    };

    for (int ply = 0; ply < maxPlies && !stopRequested.load(); ++ply) {
        // --- UI events ---
        if (window) {
            while (auto ev = window->pollEvent()) {
                if (ev->is<sf::Event::Closed>()) {
                    window->close();
                    stopRequested.store(true);
                }

                if (ev->is<sf::Event::KeyPressed>()) {
                    auto key = ev->getIf<sf::Event::KeyPressed>()->code;

                    if (key == sf::Keyboard::Key::Escape) stopRequested.store(true);
                    if (key == sf::Keyboard::Key::Space)  paused.store(!paused.load());

                    if (key == sf::Keyboard::Key::D && ui) ui->toggleDisplay();
                    if (key == sf::Keyboard::Key::M && ui) ui->toggleMute();

                    if (key == sf::Keyboard::Key::Add || key == sf::Keyboard::Key::Equal) {
                        applyThink(thinkMs.load() - 10);
                    }
                    if (key == sf::Keyboard::Key::Hyphen || key == sf::Keyboard::Key::Subtract) {
                        applyThink(thinkMs.load() + 10);
                    }
                }

                if (ev->is<sf::Event::MouseButtonPressed>()) {
                    if (ui) {
                        auto mb = ev->getIf<sf::Event::MouseButtonPressed>();
                        sf::Vector2f mousePos = window->mapPixelToCoords(mb->position);
                        ui->handleClick(mousePos, thinkMs, whiteEngine, blackEngine);
                    }
                }
            }
        }

        // Pause loop
        while (paused.load() && !stopRequested.load()) {
            if (window) {
                while (auto ev = window->pollEvent()) {
                    if (ev->is<sf::Event::Closed>()) {
                        window->close();
                        stopRequested.store(true);
                    }
                    if (ev->is<sf::Event::KeyPressed>()) {
                        auto key = ev->getIf<sf::Event::KeyPressed>()->code;

                        if (key == sf::Keyboard::Key::Escape) stopRequested.store(true);
                        if (key == sf::Keyboard::Key::Space)  paused.store(false);

                        if (key == sf::Keyboard::Key::D && ui) ui->toggleDisplay();
                        if (key == sf::Keyboard::Key::M && ui) ui->toggleMute();

                        if (key == sf::Keyboard::Key::Add || key == sf::Keyboard::Key::Equal) {
                            applyThink(thinkMs.load() - 10);
                        }
                        if (key == sf::Keyboard::Key::Hyphen || key == sf::Keyboard::Key::Subtract) {
                            applyThink(thinkMs.load() + 10);
                        }
                    }
                    if (ev->is<sf::Event::MouseButtonPressed>()) {
                        if (ui) {
                            auto mb = ev->getIf<sf::Event::MouseButtonPressed>();
                            sf::Vector2f mousePos = window->mapPixelToCoords(mb->position);
                            ui->handleClick(mousePos, thinkMs, whiteEngine, blackEngine);
                        }
                    }
                }

                if (ui) ui->draw(*window, display, board,
                                 aWins, draws, bWins,
                                 posIndex1Based, posTotal,
                                 gameIndex1Based, gameTotal,
                                 matchupLabel, thinkMs.load(), true);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (stopRequested.load()) break;

        // draw by insufficient material
        if (isDrawByMaterial(board)) {
            return GameResult::Draw;
        }

        // kings only draw (extra-fast)
        if ((std::popcount(board.whitePieces) == 1) && (std::popcount(board.blackPieces) == 1)) {
            return GameResult::Draw;
        }

        std::vector<Move> legal = board.generateAllMoves();
        if (legal.empty()) {
            if (board.amIInCheck(board.whiteToMove)) {
                return board.whiteToMove ? GameResult::BlackWin : GameResult::WhiteWin;
            }
            return GameResult::Draw;
        }

        Engine& side = board.whiteToMove ? whiteEngine : blackEngine;
        Move m = side.getMove(board);

        Undo u;
        board.makeMove(m, u);

        // repetition
        uint64_t h = board.generateZobristHash();
        int c = (++rep[h]);
        if (c >= 3) {
            if (display && window && ui) {
                if (ui->showBoard) display->updatePieces(*window, board);
                ui->draw(*window, display, board,
                         aWins, draws, bWins,
                         posIndex1Based, posTotal,
                         gameIndex1Based, gameTotal,
                         matchupLabel, thinkMs.load(), paused.load());
            }
            return GameResult::Draw;
        }

        // update visuals (only if display is ON)
        if (display && window && ui) {
            if (ui->showBoard) {
                display->updatePieces(*window, board);
            }
            ui->draw(*window, display, board,
                     aWins, draws, bWins,
                     posIndex1Based, posTotal,
                     gameIndex1Based, gameTotal,
                     matchupLabel, thinkMs.load(), paused.load());
        }
    }

    return GameResult::Draw;
}

// ----------------------------
int main() {
    initializeZobristTable();

    int posCount = 0;
    std::vector<std::string> fens = loadFens("positions.txt", posCount);
    if (fens.empty()) {
        std::cerr << "No FENs found.\n";
        std::cerr << "Create positions.txt with one FEN per line.\n";
        std::cerr << "Optional: first non-comment line can be an integer N.\n";
        return 1;
    }

    // Force first x positions only
    int numGames = 100;
    if ((int)fens.size() > numGames) fens.resize(numGames);

    // ---------------- Configure engines ----------------
    // Assumption: everything ON for both to start.
    EngineConfig cfgA;
    cfgA.timeLimitMs = 120;
    cfgA.useOpeningBook = true;
    cfgA.useTT = true;
    cfgA.useNullMove = true;
    cfgA.useLMR = true;
    cfgA.useKillerMoves = true;
    cfgA.useHistoryHeuristic = true;
    cfgA.quiescenceIncludeChecks = false;
    cfgA.extendChecks = true;
    cfgA.ttSizeMB = 64;
    cfgA.maxGamePlies = 512;

    EngineConfig cfgB = cfgA;

    // Example difference knobs (edit however you want)
    cfgA.quiescenceIncludeChecks = true;

    Engine engineA(cfgA);
    Engine engineB(cfgB);

    // ---------------- Visual board + UI panel ----------------
    BoardDisplay display;
    float boardPx = display.tileSize * 8.f;
    float panelW  = 280.f;

    sf::RenderWindow window(
        sf::VideoMode({ (unsigned)(boardPx + panelW), (unsigned)(boardPx) }),
        "Engine Match (A vs B)"
    );

    sf::Font font;
    const sf::Font* fontPtr = nullptr;
    if (font.openFromFile("sansation.ttf")) {
        fontPtr = &font;
    } else {
        std::cerr << "Warning: sansation.ttf not found; panel boxes still render but text will be missing.\n";
    }

    MatchUI ui;
    ui.panelW = panelW;
    ui.init(boardPx, fontPtr);

    // THINK TIME (ms)
    std::atomic<int> thinkMs{ cfgA.timeLimitMs };
    std::atomic<bool> paused{ false };
    std::atomic<bool> stopRequested{ false };

    Stats stats;
    Board board;

    // totalGames is always 2 per position for this UI
    int totalGames = (int)fens.size() * 2;
    int gameCounter = 0;

    std::cout << "Loaded " << fens.size() << " positions -> " << totalGames << " games.\n";
    std::cout << "Controls: [+] faster, [-] slower, [Space] pause, [Esc] stop, [D] display, [M] mute.\n\n";

    for (size_t i = 0; i < fens.size() && !stopRequested.load(); ++i) {
        const std::string& fenStr = fens[i];
        int posIndex1Based = (int)i + 1;

        // Game 1: A(W) vs B(B)
        board.createBoardFromFEN(fenStr);
        {
            gameCounter++;
            std::string matchup = "Matchup: A(W) vs B(B)";

            engineA.setTimeLimitMs(thinkMs.load());
            engineB.setTimeLimitMs(thinkMs.load());

            GameResult r = playOne(board, engineA, engineB,
                                   window.isOpen() ? &window : nullptr,
                                   window.isOpen() ? &display : nullptr,
                                   window.isOpen() ? &ui : nullptr,
                                   thinkMs, paused, stopRequested,
                                   stats.aWins, stats.draws, stats.bWins,
                                   posIndex1Based, (int)fens.size(),
                                   gameCounter, 2,
                                   matchup,
                                   cfgA.maxGamePlies);

            applyResult(stats, true, r);
        }

        if (stopRequested.load()) break;

        // Game 2: B(W) vs A(B)
        board.createBoardFromFEN(fenStr);
        {
            gameCounter++;
            std::string matchup = "Matchup: B(W) vs A(B)";

            engineA.setTimeLimitMs(thinkMs.load());
            engineB.setTimeLimitMs(thinkMs.load());

            GameResult r = playOne(board, engineB, engineA,
                                   window.isOpen() ? &window : nullptr,
                                   window.isOpen() ? &display : nullptr,
                                   window.isOpen() ? &ui : nullptr,
                                   thinkMs, paused, stopRequested,
                                   stats.aWins, stats.draws, stats.bWins,
                                   posIndex1Based, (int)fens.size(),
                                   gameCounter, 2,
                                   matchup,
                                   cfgA.maxGamePlies);

            applyResult(stats, false, r);
        }

        std::cout << "After pos " << posIndex1Based << ": "
                  << "A wins=" << stats.aWins
                  << " | B wins=" << stats.bWins
                  << " | draws=" << stats.draws
                  << " | games=" << stats.games
                  << "\n";
    }

    std::cout << "\nFinal: "
              << "A wins=" << stats.aWins
              << " | B wins=" << stats.bWins
              << " | draws=" << stats.draws
              << " | games=" << stats.games
              << "\n";

    return 0;
}
