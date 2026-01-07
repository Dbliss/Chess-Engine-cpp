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
#include <iomanip>
#include <random>
#include <memory>

// ---------------------------- helpers
static void logLine(const std::string& s) {
    std::ofstream out("match_log.txt", std::ios::app);
    out << s << "\n";
    out.flush();
}

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

static bool isDrawByMaterial(const Board& board) {
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

        // Layout: consistent 44px rows + spacing
        float y = 24.f;
        makeRow(rowDisplay,  y, 18, true);  y += 60.f;
        makeRow(rowSound,    y, 18, true);  y += 60.f;

        makeRow(rowScore,    y, 18, false); y += 60.f;
        makeRow(rowPosition, y, 18, false); y += 60.f;
        makeRow(rowGame,     y, 18, false); y += 60.f;

        makeRow(rowThink,    y, 18, true);  y += 60.f;
        makeRow(rowPaused,   y, 18, false); y += 60.f;

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

        if (showBoard && display) {
            display->draw(window);
        } else {
            sf::RectangleShape blank;
            blank.setPosition({ 0.f, 0.f });
            blank.setSize({ boardPx, boardPx });
            blank.setFillColor(sf::Color(0, 0, 0));
            window.draw(blank);
        }

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

static double scoreFromStatsForB(const Stats& s) {
    if (s.games <= 0) return 0.0;
    return (double)s.bWins / (double)s.games + 0.5 * ((double)s.draws / (double)s.games);
}

static double scoreFromStatsForA(const Stats& s) {
    if (s.games <= 0) return 0.0;
    return (double)s.aWins / (double)s.games + 0.5 * ((double)s.draws / (double)s.games);
}

static std::vector<std::string> selectFensForGames(const std::vector<std::string>& allFens, int totalGamesWanted) {
    // We do 2 games per FEN (swap colors), so positions needed is ceil(totalGames/2)
    const int positionsNeeded = (totalGamesWanted + 1) / 2;
    if ((int)allFens.size() <= positionsNeeded) return allFens;
    return std::vector<std::string>(allFens.begin(), allFens.begin() + positionsNeeded);
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

    std::unordered_map<uint64_t, int> rep;
    rep.reserve(2048);
    rep[board.zobristHash] = 1;

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

        if (isDrawByMaterial(board)) {
            return GameResult::Draw;
        }

        if ((std::popcount(board.whitePieces) == 1) && (std::popcount(board.blackPieces) == 1)) {
            return GameResult::Draw;
        }

        MoveList moves;
        board.generateAllMoves(moves);
        if (moves.size == 0) {
            if (board.amIInCheck(board.whiteToMove)) {
                return board.whiteToMove ? GameResult::BlackWin : GameResult::WhiteWin;
            }
            return GameResult::Draw;
        }

        Engine& side = board.whiteToMove ? whiteEngine : blackEngine;
        const bool moverWasWhite = board.whiteToMove;

        Move m = side.getMove(board);
        const int depthReached = side.lastSearchDepth();
        const int evalScore = side.lastEval();

        Undo u;
        board.makeMove(m, u);

        //printAfterMoveDebug(board, ply + 1, moverWasWhite, depthReached, evalScore);

        uint64_t h = board.zobristHash;
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
// Generic A/B runner (this is the new reusable testing function)
struct MatchRunConfig {
    int totalGamesWanted = 200;   // total games (not positions). We’ll do color-swaps.
    bool useUI = false;           // headless by default (important for tuning speed/noise)
    bool verbose = true;
};

struct MatchResultAB {
    Stats stats;
    int totalGamesRequested = 0;
    int totalGamesPlayed = 0;
    double scoreA = 0.0; // points/game (win=1, draw=0.5)
    double scoreB = 0.0;
};

static MatchResultAB runABMatchSeries(const std::vector<std::string>& allFens,
                                     const EngineConfig& cfgAIn,
                                     const EngineConfig& cfgBIn,
                                     const MatchRunConfig& rcfg) {
    MatchResultAB out;
    out.totalGamesRequested = rcfg.totalGamesWanted;

    // pick FEN subset once for this run
    std::vector<std::string> fens = selectFensForGames(allFens, rcfg.totalGamesWanted);
    const int maxGamesPossible = (int)fens.size() * 2;
    const int totalGames = std::min(rcfg.totalGamesWanted, maxGamesPossible);

    if (fens.empty() || totalGames <= 0) {
        return out;
    }

    EngineConfig cfgA = cfgAIn;
    EngineConfig cfgB = cfgBIn;

    Engine engineA(cfgA);
    Engine engineB(cfgB);

    std::atomic<int> thinkMs{ cfgA.timeLimitMs };
    std::atomic<bool> paused{ false };
    std::atomic<bool> stopRequested{ false };

    std::unique_ptr<BoardDisplay> display;
    std::unique_ptr<sf::RenderWindow> window;
    MatchUI ui;

    std::unique_ptr<sf::Font> font;
    const sf::Font* fontPtr = nullptr;

    float boardPx = 0.f;
    float panelW  = 280.f;

    if (rcfg.useUI) {
        display = std::make_unique<BoardDisplay>();
        boardPx = display->tileSize * 8.f;

        window = std::make_unique<sf::RenderWindow>(
            sf::VideoMode({ (unsigned)(boardPx + panelW), (unsigned)(boardPx) }),
            "Engine A/B Match"
        );

        font = std::make_unique<sf::Font>();
        if (font->openFromFile("sansation.ttf")) {
            fontPtr = font.get();
        } else {
            std::cerr << "Warning: sansation.ttf not found; panel boxes still render but text will be missing.\n";
        }

        ui.panelW = panelW;
        ui.init(boardPx, fontPtr);
    }

    Stats stats;
    Board board;

    int gamesPlayed = 0;

    if (rcfg.verbose) {
        std::cout << "Running A/B: " << totalGames << " games using " << fens.size() << " positions.\n";
        std::cout << "Time(ms): A=" << cfgA.timeLimitMs << " B=" << cfgB.timeLimitMs << "\n";
    }

    for (size_t i = 0; i < fens.size() && !stopRequested.load() && gamesPlayed < totalGames; ++i) {
        const std::string& fenStr = fens[i];
        int posIndex1Based = (int)i + 1;

        // Game 1: A(W) vs B(B)
        if (gamesPlayed < totalGames) {
            board.createBoardFromFEN(fenStr);
            board.zobristHash = board.generateZobristHash();

            engineA.setTimeLimitMs(thinkMs.load());
            engineB.setTimeLimitMs(thinkMs.load());

            gamesPlayed++;
            const int gameInPos = 1;
            const int gamesThisPos = (totalGames - gamesPlayed + 1 >= 1) ? 2 : 1; // UI nicety; not critical

            std::string matchup = "A(W) vs B(B)  [" + std::to_string(gamesPlayed) + "/" + std::to_string(totalGames) + "]";

            GameResult r = playOne(board, engineA, engineB,
                                   (window && window->isOpen()) ? window.get() : nullptr,
                                   (window && window->isOpen()) ? display.get() : nullptr,
                                   (window && window->isOpen()) ? &ui : nullptr,
                                   thinkMs, paused, stopRequested,
                                   stats.aWins, stats.draws, stats.bWins,
                                   posIndex1Based, (int)fens.size(),
                                   gameInPos, 2,
                                   matchup,
                                   cfgA.maxGamePlies);

            applyResult(stats, true, r);
        }

        // Game 2: B(W) vs A(B)
        if (gamesPlayed < totalGames && !stopRequested.load()) {
            board.createBoardFromFEN(fenStr);
            board.zobristHash = board.generateZobristHash();

            engineA.setTimeLimitMs(thinkMs.load());
            engineB.setTimeLimitMs(thinkMs.load());

            gamesPlayed++;
            const int gameInPos = 2;

            std::string matchup = "B(W) vs A(B)  [" + std::to_string(gamesPlayed) + "/" + std::to_string(totalGames) + "]";

            GameResult r = playOne(board, engineB, engineA,
                                   (window && window->isOpen()) ? window.get() : nullptr,
                                   (window && window->isOpen()) ? display.get() : nullptr,
                                   (window && window->isOpen()) ? &ui : nullptr,
                                   thinkMs, paused, stopRequested,
                                   stats.aWins, stats.draws, stats.bWins,
                                   posIndex1Based, (int)fens.size(),
                                   gameInPos, 2,
                                   matchup,
                                   cfgA.maxGamePlies);

            applyResult(stats, false, r);
        }

        if (rcfg.verbose) {
            std::cout << "After pos " << posIndex1Based << ": "
                      << "A wins=" << stats.aWins
                      << " | B wins=" << stats.bWins
                      << " | draws=" << stats.draws
                      << " | games=" << stats.games
                      << "\n";
        }
    }

    out.stats = stats;
    out.totalGamesPlayed = stats.games;
    out.scoreA = scoreFromStatsForA(stats);
    out.scoreB = scoreFromStatsForB(stats);

    if (rcfg.verbose) {
        std::cout << "\nFinal: "
                  << "A wins=" << stats.aWins
                  << " | B wins=" << stats.bWins
                  << " | draws=" << stats.draws
                  << " | games=" << stats.games
                  << "\n";
        std::cout << "Score: A=" << std::fixed << std::setprecision(4) << out.scoreA
                  << " | B=" << out.scoreB << "\n";
    }

    return out;
}

// ----------------------------
// Piece-value tuning

struct PieceValues {
    int pawn   = 100;
    int knight = 325;
    int bishop = 325;
    int rook   = 500;
    int queen  = 975;
};

static void applyPieceValues(EngineConfig& cfg, const PieceValues& pv) {
    cfg.pawnValue   = pv.pawn;
    cfg.knightValue = pv.knight;
    cfg.bishopValue = pv.bishop;
    cfg.rookValue   = pv.rook;
    cfg.queenValue  = pv.queen;
}

static PieceValues getPieceValuesFromCfg(const EngineConfig& cfg) {
    PieceValues pv;
    pv.pawn   = cfg.pawnValue;
    pv.knight = cfg.knightValue;
    pv.bishop = cfg.bishopValue;
    pv.rook   = cfg.rookValue;
    pv.queen  = cfg.queenValue;
    return pv;
}

static std::string pvToString(const PieceValues& pv) {
    return "P=" + std::to_string(pv.pawn) +
           " N=" + std::to_string(pv.knight) +
           " B=" + std::to_string(pv.bishop) +
           " R=" + std::to_string(pv.rook) +
           " Q=" + std::to_string(pv.queen);
}

static int clampInt(int v, int lo, int hi) {
    return std::max(lo, std::min(hi, v));
}

static void clampPieceValues(PieceValues& pv) {
    // sane-ish bounds so random search doesn't do something dumb
    pv.pawn   = clampInt(pv.pawn,   60,  140);
    pv.knight = clampInt(pv.knight, 200, 500);
    pv.bishop = clampInt(pv.bishop, 200, 500);
    pv.rook   = clampInt(pv.rook,   300, 800);
    pv.queen  = clampInt(pv.queen,  600, 1400);
}

struct PieceTuningConfig {
    int gamesPerEval = 200;

    // Keep this bounded or you’ll end up doing thousands of games per tuning pass
    int randomTrials = 12;
    int hillClimbEvals = 18;

    // step schedule for the local search
    std::vector<int> steps = { 25, 15, 10, 5 };

    // accept only if B gains at least this many "points" over best (win=1, draw=0.5)
    // 200 games => 1 point = 0.005 score
    double minPointGainToAccept = 2.0; // i.e. +2 points over 200 games (≈ +1% absolute score)
};

static PieceValues tunePieceValuesVsBaseline(const std::vector<std::string>& allFens,
                                            const EngineConfig& baselineCfgA,
                                            const PieceTuningConfig& tc) {
    // Fix the position subset once so every candidate is compared on the same set.
    std::vector<std::string> fensFixed = selectFensForGames(allFens, tc.gamesPerEval);
    const int maxGamesPossible = (int)fensFixed.size() * 2;
    const int totalGames = std::min(tc.gamesPerEval, maxGamesPossible);

    if (fensFixed.empty() || totalGames <= 0) {
        std::cerr << "Not enough FENs to run tuning.\n";
        return getPieceValuesFromCfg(baselineCfgA);
    }

    MatchRunConfig rcfg;
    rcfg.totalGamesWanted = totalGames;
    rcfg.useUI = false;
    rcfg.verbose = false;

    auto evalCandidate = [&](const PieceValues& pv) -> MatchResultAB {
        EngineConfig cfgB = baselineCfgA;
        applyPieceValues(cfgB, pv);
        return runABMatchSeries(fensFixed, baselineCfgA, cfgB, rcfg);
    };

    auto pointsB = [](const MatchResultAB& r) -> double {
        return (double)r.stats.bWins + 0.5 * (double)r.stats.draws;
    };

    // start at baseline values (so we measure improvements vs "old")
    PieceValues best = getPieceValuesFromCfg(baselineCfgA);
    clampPieceValues(best);

    MatchResultAB bestRes = evalCandidate(best);
    double bestPoints = pointsB(bestRes);

    {
        std::string s = "[TUNE] Baseline (B==A values) "
            + pvToString(best)
            + " -> B: W=" + std::to_string(bestRes.stats.bWins)
            + " D=" + std::to_string(bestRes.stats.draws)
            + " L=" + std::to_string(bestRes.stats.aWins)
            + " | scoreB=" + std::to_string(bestRes.scoreB);
        std::cout << s << "\n";
        logLine(s);
    }

    // ----------------------------
    // Stage 1: random search (bounded)
    std::mt19937 rng(123456u);
    std::uniform_int_distribution<int> dP(-20, 20);
    std::uniform_int_distribution<int> dN(-60, 60);
    std::uniform_int_distribution<int> dB(-60, 60);
    std::uniform_int_distribution<int> dR(-80, 80);
    std::uniform_int_distribution<int> dQ(-160, 160);

    for (int t = 0; t < tc.randomTrials; ++t) {
        PieceValues cand = best;
        cand.pawn   += dP(rng);
        cand.knight += dN(rng);
        cand.bishop += dB(rng);
        cand.rook   += dR(rng);
        cand.queen  += dQ(rng);
        clampPieceValues(cand);

        MatchResultAB r = evalCandidate(cand);
        double p = pointsB(r);

        std::string line = "[TUNE][RAND " + std::to_string(t+1) + "/" + std::to_string(tc.randomTrials) + "] "
            + pvToString(cand)
            + " -> B: W=" + std::to_string(r.stats.bWins)
            + " D=" + std::to_string(r.stats.draws)
            + " L=" + std::to_string(r.stats.aWins)
            + " | scoreB=" + std::to_string(r.scoreB);
        std::cout << line << "\n";
        logLine(line);

        if (p > bestPoints + tc.minPointGainToAccept) {
            best = cand;
            bestRes = r;
            bestPoints = p;

            std::string acc = "  ACCEPT -> best now " + pvToString(best) + " (scoreB=" + std::to_string(bestRes.scoreB) + ")";
            std::cout << acc << "\n";
            logLine(acc);
        }
    }

    // ----------------------------
    // Stage 2: local hill-climb around best (bounded eval count)
    int evalsUsed = 0;
    for (int step : tc.steps) {
        bool improved = true;
        while (improved && evalsUsed < tc.hillClimbEvals) {
            improved = false;

            auto tryDelta = [&](auto applyDelta, const char* tag) {
                if (evalsUsed >= tc.hillClimbEvals) return;

                PieceValues cand = best;
                applyDelta(cand);
                clampPieceValues(cand);

                MatchResultAB r = evalCandidate(cand);
                evalsUsed++;
                double p = pointsB(r);

                std::string line = "[TUNE][HC step=" + std::to_string(step) + " " + std::string(tag) + "] "
                    + pvToString(cand)
                    + " -> B: W=" + std::to_string(r.stats.bWins)
                    + " D=" + std::to_string(r.stats.draws)
                    + " L=" + std::to_string(r.stats.aWins)
                    + " | scoreB=" + std::to_string(r.scoreB);
                std::cout << line << "\n";
                logLine(line);

                if (p > bestPoints + tc.minPointGainToAccept) {
                    best = cand;
                    bestRes = r;
                    bestPoints = p;
                    improved = true;

                    std::string acc = "  ACCEPT -> best now " + pvToString(best) + " (scoreB=" + std::to_string(bestRes.scoreB) + ")";
                    std::cout << acc << "\n";
                    logLine(acc);
                }
            };

            // Coordinate-ish steps (±step on each value)
            tryDelta([&](PieceValues& c){ c.pawn += step; },   "+P");
            tryDelta([&](PieceValues& c){ c.pawn -= step; },   "-P");
            tryDelta([&](PieceValues& c){ c.knight += step; }, "+N");
            tryDelta([&](PieceValues& c){ c.knight -= step; }, "-N");
            tryDelta([&](PieceValues& c){ c.bishop += step; }, "+B");
            tryDelta([&](PieceValues& c){ c.bishop -= step; }, "-B");
            tryDelta([&](PieceValues& c){ c.rook += step; },   "+R");
            tryDelta([&](PieceValues& c){ c.rook -= step; },   "-R");
            tryDelta([&](PieceValues& c){ c.queen += step; },  "+Q");
            tryDelta([&](PieceValues& c){ c.queen -= step; },  "-Q");
        }
    }

    std::string done = "[TUNE] DONE -> best " + pvToString(best)
        + " | B: W=" + std::to_string(bestRes.stats.bWins)
        + " D=" + std::to_string(bestRes.stats.draws)
        + " L=" + std::to_string(bestRes.stats.aWins)
        + " | scoreB=" + std::to_string(bestRes.scoreB);
    std::cout << done << "\n";
    logLine(done);

    return best;
}

// ----------------------------
// MAIN
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

    // ---- choose mode here (no CLI args) ----
    constexpr bool kTunePieceValues = false;   // <- set false to go back to normal A vs B match UI
    constexpr bool kShowUIInMatch   = true;   // only used when kTunePieceValues==false

    // ---------------- Configure engines ----------------
    // Engine A baseline (default)
    EngineConfig cfgA;
    cfgA.timeLimitMs = 1000;

    // Engine B starts as same as A (baseline), then we tune only piece values
    EngineConfig cfgB = cfgA;

    if constexpr (kTunePieceValues) {
        PieceTuningConfig tc;
        tc.gamesPerEval = 200; // exactly what you asked for (unless not enough FENs)
        tc.randomTrials = 12;
        tc.hillClimbEvals = 18;
        tc.steps = { 25, 15, 10, 5 };
        tc.minPointGainToAccept = 2.0;

        PieceValues best = tunePieceValuesVsBaseline(fens, cfgA, tc);

        std::cout << "\nBEST PIECE VALUES FOUND:\n";
        std::cout << "  pawnValue   = " << best.pawn   << "\n";
        std::cout << "  knightValue = " << best.knight << "\n";
        std::cout << "  bishopValue = " << best.bishop << "\n";
        std::cout << "  rookValue   = " << best.rook   << "\n";
        std::cout << "  queenValue  = " << best.queen  << "\n";

        return 0;
    }

    // ---------------- Normal A/B match (optional UI) ----------------
    // Example A/B: keep A default, change B however you want (still uses the new runner)
    cfgB.useOpeningBook = false; // or keep identical, etc.

    MatchRunConfig rcfg;
    rcfg.totalGamesWanted = 200; // total games
    rcfg.useUI = kShowUIInMatch;
    rcfg.verbose = true;

    MatchResultAB r = runABMatchSeries(fens, cfgA, cfgB, rcfg);

    std::cout << "\nFinal Score:\n";
    std::cout << "A: W=" << r.stats.aWins << " D=" << r.stats.draws << " L=" << r.stats.bWins << " | scoreA=" << r.scoreA << "\n";
    std::cout << "B: W=" << r.stats.bWins << " D=" << r.stats.draws << " L=" << r.stats.aWins << " | scoreB=" << r.scoreB << "\n";

    return 0;
}
