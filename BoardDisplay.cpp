#include "BoardDisplay.h"
#include <iostream>

BoardDisplay::BoardDisplay() {
    loadTextures();
}

void BoardDisplay::loadTextures() {
    if (!whitePawnTexture.loadFromFile("images/Chess_plt60.png") ||
        !whiteKnightTexture.loadFromFile("images/Chess_nlt60.png") ||
        !whiteBishopTexture.loadFromFile("images/Chess_blt60.png") ||
        !whiteRookTexture.loadFromFile("images/Chess_rlt60.png") ||
        !whiteQueenTexture.loadFromFile("images/Chess_qlt60.png") ||
        !whiteKingTexture.loadFromFile("images/Chess_klt60.png") ||
        !blackPawnTexture.loadFromFile("images/Chess_pdt60.png") ||
        !blackKnightTexture.loadFromFile("images/Chess_ndt60.png") ||
        !blackBishopTexture.loadFromFile("images/Chess_bdt60.png") ||
        !blackRookTexture.loadFromFile("images/Chess_rdt60.png") ||
        !blackQueenTexture.loadFromFile("images/Chess_qdt60.png") ||
        !blackKingTexture.loadFromFile("images/Chess_kdt60.png")) {
        std::cerr << "Error loading piece textures" << std::endl;
    }
}

void BoardDisplay::setupPieces(Board& board) {
    pieces.clear();
    for (int i = 0; i < 64; ++i) {
        int x = 7 - (i % 8); // Reverse the columns
        int y = i / 8;       // Keep the rows as is
        sf::Sprite sprite;
        char piece = board.getPieceAt(i); // Assuming you have a method to get piece at a square
        switch (piece) {
        case 'p': sprite.setTexture(whitePawnTexture); break;
        case 'n': sprite.setTexture(whiteKnightTexture); break;
        case 'b': sprite.setTexture(whiteBishopTexture); break;
        case 'r': sprite.setTexture(whiteRookTexture); break;
        case 'q': sprite.setTexture(whiteQueenTexture); break;
        case 'k': sprite.setTexture(whiteKingTexture); break;
        case 'P': sprite.setTexture(blackPawnTexture); break;
        case 'N': sprite.setTexture(blackKnightTexture); break;
        case 'B': sprite.setTexture(blackBishopTexture); break;
        case 'R': sprite.setTexture(blackRookTexture); break;
        case 'Q': sprite.setTexture(blackQueenTexture); break;
        case 'K': sprite.setTexture(blackKingTexture); break;
        default: continue;
        }
        sprite.setPosition(x * tileSize, (7 - y) * tileSize); // Adjust for bottom-to-top indexing
        pieces.push_back(sprite);
    }
}

void BoardDisplay::updatePieces(Board& board) {
    setupPieces(board);
}

void BoardDisplay::draw(sf::RenderWindow& window) {
    // Draw checkered background
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            sf::RectangleShape square(sf::Vector2f(tileSize, tileSize));
            square.setPosition(x * tileSize, y * tileSize);
            if ((x + y) % 2 == 0) {
                square.setFillColor(lightColor);
            }
            else {
                square.setFillColor(darkColor);
            }
            window.draw(square);
        }
    }

    // Draw pieces
    for (auto& piece : pieces) {
        window.draw(piece);
    }
}

bool BoardDisplay::handleMove(sf::RenderWindow& window, Board& board) {
    static sf::Vector2i firstClick(-1, -1);
    static sf::Vector2i secondClick(-1, -1);

    if (sf::Mouse::isButtonPressed(sf::Mouse::Left)) {
        sf::Vector2i click = sf::Mouse::getPosition(window);
        int x = click.x / tileSize;
        int y = click.y / tileSize;
        sf::Vector2i boardClick(x, y);

        if (firstClick == sf::Vector2i(-1, -1)) {
            if (x >= 0 && x < 8 && y >= 0 && y < 8) {
                firstClick = boardClick;
            }
        }
        else {
            if (boardClick != firstClick && x >= 0 && x < 8 && y >= 0 && y < 8) {
                secondClick = boardClick;

                // Convert the click coordinates to board indices
                int from = (7 - firstClick.y) * 8 + (7 - firstClick.x);
                int to = (7 - secondClick.y) * 8 + (7 - secondClick.x);

                // Load legal moves
                loadLegalMoves(board);

                // Check if the move is legal
                for (Move& legalMove : legalMoves) {
                    if (legalMove.from == from && legalMove.to == to) {
                        board.makeMove(legalMove);
                        board.printBoard();
                        updatePieces(board);
                        firstClick = sf::Vector2i(-1, -1);
                        secondClick = sf::Vector2i(-1, -1);
                        return true;
                    }
                }

                // Reset if the move is not legal
                firstClick = sf::Vector2i(-1, -1);
                secondClick = sf::Vector2i(-1, -1);
            }
        }
    }

    return false;
}

void BoardDisplay::loadLegalMoves(Board& board) {
    legalMoves = board.generateAllMoves();
}