#include "engine2.h"
#include "BoardDisplay.h"
#include "chess.h"
#include <thread>
#include "zobrist.h"

bool isDrawByMaterial(Board board) {
    int numWhitePawns = std::_Popcount(board.whitePawns);
    int numWhiteBishops = std::_Popcount(board.whiteBishops);
    int numWhiteKnights = std::_Popcount(board.whiteKnights);
    int numWhiteRooks = std::_Popcount(board.whiteRooks);
    int numWhiteQueens = std::_Popcount(board.whiteQueens);

    int numBlackPawns = std::_Popcount(board.blackPawns);
    int numBlackBishops = std::_Popcount(board.blackBishops);
    int numBlackKnights = std::_Popcount(board.blackKnights);
    int numBlackRooks = std::_Popcount(board.blackRooks);
    int numBlackQueens = std::_Popcount(board.blackQueens);

    // Encourage draws if both sides have no pawns or major pieces left and only up to one minor piece each.
    if (!numWhitePawns && !numBlackPawns &&
        !numWhiteQueens && !numBlackQueens &&
        !numWhiteRooks && !numBlackRooks) {

        // If both sides have at most one minor piece each, this is a draw
        if (isEndgameDraw(numWhiteBishops, numWhiteKnights, numBlackKnights, numBlackBishops) || (numWhiteBishops + numWhiteKnights + numBlackKnights + numBlackBishops <= 1)) {
            return true;
        }
    }
    return false;
}

void startPonderingThread(std::thread& ponderingThread, Board& dupBoard) {
    if (ponderingThread.joinable()) {
        ponderingThread.join();
    }
    isPonderingRunning = true;
    endTime2 = (std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(999999));
    ponderingThread = std::thread(ponderEngine2, std::ref(dupBoard));
}

void updatePonderingThread(Board& board, Board& dupBoard) {
    isPonderingRunning = false;
    endTime2 = std::chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    board.transposition_table = dupBoard.transposition_table;
    dupBoard = board;
    isPonderingRunning = true;
    endTime2 = (std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(999999));
}

void stopPonderingThread(std::thread& ponderingThread, Board& board, Board& dupboard) {
    endTime2 = std::chrono::high_resolution_clock::now();
    isPonderingRunning = false;
    if (ponderingThread.joinable()) {
        ponderingThread.join();
    }
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
    endMessage.setPosition({window.getSize().x / 2.0f - endMessage.getGlobalBounds().size.x / 2.0f, window.getSize().y / 3.0f});

    sf::Text playAgainButton(font, "Want to play again?", 30);
    playAgainButton.setFillColor(sf::Color::Green);
    playAgainButton.setStyle(sf::Text::Bold);
    playAgainButton.setPosition({window.getSize().x / 2.0f - playAgainButton.getGlobalBounds().size.x / 2.0f, window.getSize().y / 2.0f});

    sf::RectangleShape playAgainButtonBox(sf::Vector2f(playAgainButton.getGlobalBounds().size.x + 20, playAgainButton.getGlobalBounds().size.y + 10));
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
    bool ponderingOn = false;
    int timeLimit = 3000; //milliseconds
    char playerColor = 'w';
    bool startGame = false;

    initializeZobristTable();
    Board board;
    board.createBoard();
    //board.createBoardFromFEN("8/1p5p/p1pP3k/7p/P3r1P1/7P/2K5/2R5 w - - 0 1");
    board.printBoard();
    BoardDisplay display;
    display.setupPieces(board);

    sf::RenderWindow window(sf::VideoMode({static_cast<unsigned int>(display.tileSize * 8 + 330), static_cast<unsigned int>(display.tileSize * 8)}), "Chess Board");

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

    Board dupBoard = board;
    std::thread ponderingThread;
    bool isPlayerTurn = (playerColor == 'w');

    while (window.isOpen()) {
        while (auto event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>())
                window.close();

            if (event->is<sf::Event::MouseButtonPressed>()) {
                sf::Vector2f mousePos = window.mapPixelToCoords(sf::Mouse::getPosition(window));

                if (startButtonBox.getGlobalBounds().contains(mousePos)) {
                    startGame = true;
                    dupBoard = board;
                    if (ponderingOn) startPonderingThread(ponderingThread, dupBoard);
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
                        }
                        else if (timeLimit > 100) {
                            timeLimit -= 100;
                        }
                    }
                    else {
                        timeLimit += 1000;
                    }
                    timeLimitText.setString(std::to_string(timeLimit));
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
                    if (board.generateAllMoves().empty()) {
                        if (board.amIInCheck(board.whiteToMove)) {
                            endGameMessage = "You lose";
                        }
                        else {
                            endGameMessage = "Draw";
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    }
                    else if (board.isThreefoldRepetition(false)) {
                        endGameMessage = "Draw";
                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    }
                    if (display.handleMove(window, board)) {
                        std::cout << "Player move made" << std::endl;
                        if (ponderingOn) updatePonderingThread(board, dupBoard);
                        board.updatePositionHistory(true);
                        std::cout << "Player move made" << std::endl;
                        isPlayerTurn = false;
                    }
                }
                else {
                    // Check for game end
                    if (board.generateAllMoves().empty()) {
                        if (board.amIInCheck(board.whiteToMove)) {
                            endGameMessage = "You win";
                        }
                        else {
                            endGameMessage = "Draw";
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    }
                    else if (board.isThreefoldRepetition(false)) {
                        endGameMessage = "Draw";
                        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                        displayEndGameMessage(window, endGameMessage);
                        return;
                    }
                    Move engineMove = getEngineMove2(board, timeLimit);
                    board.makeMove(engineMove);
                    std::cout << engineMove.from << engineMove.to << std::endl;
                    board.lastMove = engineMove;
                    board.updatePositionHistory(true);
                    board.printBoard();
                    display.updatePieces(window, board);
                    std::cout << "Engine move made" << std::endl;
                    isPlayerTurn = true;
                    dupBoard = board;
                    // Restart pondering after engine move
                    if (ponderingOn) updatePonderingThread(board, dupBoard);
                }

                window.clear();
                display.draw(window);
                window.display();
            }

            if (ponderingOn) stopPonderingThread(ponderingThread, board, dupBoard);
        }
    }
}

int main() {
    playAgainstComputer();
    return 0;
}