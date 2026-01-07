#include "engine.h"
#include "BoardDisplay.h"
#include "chess.h"
#include "zobrist.h"

#include <thread>
#include <bit>

// Optional helper (you already had it); still valid because engine.h keeps isEndgameDraw().
bool isDrawByMaterial(Board board) {
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

void playAgainstComputer();

void displayEndGameMessage(sf::RenderWindow& window, const std::string& message) {
    sf::Font font;
    if (!font.openFromFile("sansation.ttf")) {
        std::cerr << "Error loading font" << std::endl;
        return;
    }

    sf::Text endMessage(font, message, 50);
    endMessage.setFillColor(sf::Color::Red);
    endMessage.setStyle(sf::Text::Bold);
    endMessage.setPosition({window.getSize().x / 2.0f - endMessage.getGlobalBounds().size.x / 2.0f,
                            window.getSize().y / 3.0f});

    sf::Text playAgainButton(font, "Want to play again?", 30);
    playAgainButton.setFillColor(sf::Color::Green);
    playAgainButton.setStyle(sf::Text::Bold);
    playAgainButton.setPosition({window.getSize().x / 2.0f - playAgainButton.getGlobalBounds().size.x / 2.0f,
                                 window.getSize().y / 2.0f});

    sf::RectangleShape playAgainButtonBox(
        sf::Vector2f(playAgainButton.getGlobalBounds().size.x + 20,
                     playAgainButton.getGlobalBounds().size.y + 10));
    playAgainButtonBox.setPosition({playAgainButton.getPosition().x - 10, playAgainButton.getPosition().y - 5});
    playAgainButtonBox.setFillColor(sf::Color::Transparent);
    playAgainButtonBox.setOutlineThickness(2);
    playAgainButtonBox.setOutlineColor(sf::Color::Green);

    while (window.isOpen()) {
        while (auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>())
                window.close();

            if (event->is<sf::Event::MouseButtonPressed>()) {
                sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                if (playAgainButtonBox.getGlobalBounds().contains(mousePos)) {
                    playAgainstComputer();
                    return;
                }
            }
        }

        window.clear();
        window.draw(endMessage);
        window.draw(playAgainButtonBox);
        window.draw(playAgainButton);
        window.display();
    }
}

void playAgainstComputer() {
    // NOTE: engine.cpp (Engine) doesn’t use your old engine2 pondering globals/threads.
    // This keeps the UI toggle, but it is currently cosmetic (no background search).
    bool ponderingOn = false;

    int timeLimit = 3000; // milliseconds
    char playerColor = 'w';
    bool startGame = false;

    initializeZobristTable();

    Board board;
    board.createBoard();
    //board.createBoardFromFEN("8/3B4/2P1pk2/p3b1p1/7p/P3P3/1P4R1/2K5 w - - 0 1");
    board.printBoard();

    BoardDisplay display;
    display.setupPieces(board);

    sf::RenderWindow window(
        sf::VideoMode({static_cast<unsigned int>(display.tileSize * 8 + 330),
                       static_cast<unsigned int>(display.tileSize * 8)}),
        "Chess Board");

    // Create UI elements
    sf::Font font;
    if (!font.openFromFile("sansation.ttf")) {
        std::cerr << "Error loading font" << std::endl;
        return;
    }

    sf::Text startButton(font, "Start Game", 20);
    startButton.setPosition({static_cast<float>(display.tileSize * 8 + 10), 10});
    sf::RectangleShape startButtonBox(sf::Vector2f(275, 40)); // 10% wider
    startButtonBox.setPosition({static_cast<float>(display.tileSize * 8 + 5), 5});
    startButtonBox.setFillColor(sf::Color::Transparent);
    startButtonBox.setOutlineThickness(2);
    startButtonBox.setOutlineColor(sf::Color::Green);

    sf::Text playerColorLabel(font, "Player Color (White/Black):", 20);
    playerColorLabel.setPosition({static_cast<float>(display.tileSize * 8 + 10), 70});
    sf::RectangleShape playerColorBox(sf::Vector2f(275, 40)); // 10% wider
    playerColorBox.setPosition({static_cast<float>(display.tileSize * 8 + 5), 65});
    playerColorBox.setFillColor(sf::Color::Transparent);
    playerColorBox.setOutlineThickness(2);
    playerColorBox.setOutlineColor(sf::Color::Green);

    sf::Text timeLimitLabel(font, "Computer Thinking Time (ms):", 20);
    timeLimitLabel.setPosition({static_cast<float>(display.tileSize * 8 + 10), 150});
    sf::RectangleShape timeLimitBox(sf::Vector2f(275, 40)); // 10% wider
    timeLimitBox.setPosition({static_cast<float>(display.tileSize * 8 + 5), 145});
    timeLimitBox.setFillColor(sf::Color::Transparent);
    timeLimitBox.setOutlineThickness(2);
    timeLimitBox.setOutlineColor(sf::Color::Green);

    sf::Text ponderingLabel(font, "Allow Pondering (Yes/No):", 20);
    ponderingLabel.setPosition({static_cast<float>(display.tileSize * 8 + 10), 230});
    sf::RectangleShape ponderingBox(sf::Vector2f(275, 40)); // 10% wider
    ponderingBox.setPosition({static_cast<float>(display.tileSize * 8 + 5), 225});
    ponderingBox.setFillColor(sf::Color::Transparent);
    ponderingBox.setOutlineThickness(2);
    ponderingBox.setOutlineColor(sf::Color::Green);

    sf::Text playerColorText(font, playerColor == 'w' ? "White" : "Black", 20);
    playerColorText.setPosition({static_cast<float>(display.tileSize * 8 + 10), 110});

    sf::Text timeLimitText(font, std::to_string(timeLimit), 20);
    timeLimitText.setPosition({static_cast<float>(display.tileSize * 8 + 10), 190});

    sf::Text ponderingText(font, ponderingOn ? "Yes" : "No", 20);
    ponderingText.setPosition({static_cast<float>(display.tileSize * 8 + 10), 270});

    EngineConfig cfg;
    cfg.timeLimitMs = timeLimit;
    Engine engine(cfg);

    bool isPlayerTurn = (playerColor == 'w');

    while (window.isOpen()) {
        while (auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>())
                window.close();

            if (event->is<sf::Event::MouseButtonPressed>()) {
                sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));

                if (startButtonBox.getGlobalBounds().contains(mousePos)) {
                    startGame = true;

                    // reset engine heuristics/TT for a clean game
                    engine.newGame();

                    isPlayerTurn = (playerColor == 'w');
                }

                if (playerColorBox.getGlobalBounds().contains(mousePos)) {
                    playerColor = (playerColor == 'w') ? 'b' : 'w';
                    playerColorText.setString(playerColor == 'w' ? "White" : "Black");
                }

                if (timeLimitBox.getGlobalBounds().contains(mousePos)) {
                    if (mousePos.x < timeLimitLabel.getPosition().x + timeLimitLabel.getGlobalBounds().size.x / 2) {
                        if (timeLimit > 1000) {
                            timeLimit -= 1000;
                        } else if (timeLimit > 100) {
                            timeLimit -= 100;
                        }
                    } else {
                        timeLimit += 1000;
                    }
                    timeLimitText.setString(std::to_string(timeLimit));

                    // push new time limit into engine
                    engine.setTimeLimitMs(timeLimit);
                }

                if (ponderingBox.getGlobalBounds().contains(mousePos)) {
                    ponderingOn = !ponderingOn;
                    ponderingText.setString(ponderingOn ? "Yes" : "No");
                }
            }
        }

        window.clear();
        display.draw(window);

        // Draw UI elements
        window.draw(startButtonBox);
        window.draw(startButton);
        window.draw(playerColorBox);
        window.draw(playerColorLabel);
        window.draw(playerColorText);
        window.draw(timeLimitBox);
        window.draw(timeLimitLabel);
        window.draw(timeLimitText);
        window.draw(ponderingBox);
        window.draw(ponderingLabel);
        window.draw(ponderingText);

        window.display();

        if (startGame) {
            std::string endGameMessage;

            while (window.isOpen()) {
                while (auto innerEvent = window.pollEvent()) {
                    if (innerEvent->is<sf::Event::Closed>())
                        window.close();
                }

                if (isPlayerTurn) {
                    // Check for game end
                    MoveList moves;
                    board.generateAllMoves(moves);
                    if (moves.size == 0) {
                        if (board.amIInCheck(board.whiteToMove)) endGameMessage = "You lose";
                        else endGameMessage = "Draw";

                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    } else if (board.isThreefoldRepetition()) {
                        endGameMessage = "Draw";
                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    }

                    if (display.handleMove(window, board)) {
                        std::cout << "Player move made" << std::endl;
                        isPlayerTurn = false;
                    }
                } else {
                    // Check for game end
                    MoveList moves;
                    board.generateAllMoves(moves);
                    if (moves.size == 0) {
                        if (board.amIInCheck(board.whiteToMove)) endGameMessage = "You win";
                        else endGameMessage = "Draw";

                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    } else if (board.isThreefoldRepetition()) {
                        endGameMessage = "Draw";
                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    }

                    engine.setTimeLimitMs(timeLimit);
                    Move engineMove = engine.getMove(board);

                    Undo u;
                    board.makeMove(engineMove, u);
                    std::cout << engineMove.from << engineMove.to << std::endl;
                    board.lastMove = engineMove;
                    engine.printAfterMoveDebug(engine, board);

                    display.updatePieces(window, board);

                    isPlayerTurn = true;

                    (void)ponderingOn;
                }

                window.clear();
                display.draw(window);
                window.display();
            }
        }
    }
}

int main() {
    playAgainstComputer();
    return 0;
}
