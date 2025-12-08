#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <random>
#include <thread>
#include <termios.h>

using namespace std;

// Game constants
const int BOARD_WIDTH = 15;
const int BOARD_HEIGHT = 20;
const int PREVIEW_SIZE = 4;

// Color codes for terminal
const char* COLOR_RESET = "\033[0m";
const char* COLOR_CYAN = "\033[36m";
const char* COLOR_YELLOW = "\033[33m";
const char* COLOR_PURPLE = "\033[35m";
const char* COLOR_GREEN = "\033[32m";
const char* COLOR_RED = "\033[31m";
const char* COLOR_BLUE = "\033[34m";
const char* COLOR_ORANGE = "\033[38;5;208m";
const char* COLOR_WHITE = "\033[37m";

// Tetromino shapes (7 pieces)
const int SHAPES[7][4][4][4] = {
    // I piece
    {
        {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0}},
        {{0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0}}
    },
    // O piece
    {
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}}
    },
    // T piece
    {
        {{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}
    },
    // S piece
    {
        {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0}},
        {{1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0}}
    },
    // Z piece
    {
        {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,0,1,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0}}
    },
    // J piece
    {
        {{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,1,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0}}
    },
    // L piece
    {
        {{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}},
        {{0,1,0,0}, {0,1,0,0}, {0,1,1,0}, {0,0,0,0}},
        {{0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0}},
        {{1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0}}
    }
};

// Color mapping for each piece
const char* PIECE_COLORS[7] = {
    COLOR_CYAN,   // I
    COLOR_YELLOW, // O
    COLOR_PURPLE, // T
    COLOR_GREEN,  // S
    COLOR_RED,    // Z
    COLOR_BLUE,   // J
    COLOR_ORANGE  // L
};

// Terminal settings for non-blocking input
struct termios orig_termios;

struct Position {
    int x{}, y{};
    Position() = default;
    Position(int _x, int _y) : x(_x), y(_y) {}
};

struct GameState {
    int board[BOARD_HEIGHT][BOARD_WIDTH];
    int currentPiece;
    int currentRotation;
    int currentX;
    int currentY;
    int nextPiece;
    int score;
    int level;
    int linesCleared;
    bool gameOver;
    bool paused;
    chrono::steady_clock::time_point lastMoveTime;
};

struct Piece {
    int type{0};
    int rotation{0};
    Position pos{5, 0};
};

struct InputBuffer {
    // Buffer to store input characters (3 bytes for arrow key sequences: ESC [ A/B/C/D)
    char buffer[3];
    
    // Tracks how many characters are currently in the buffer
    int count;
    
    // Timestamp of the last input for timeout handling
    std::chrono::steady_clock::time_point lastInputTime;

    // Constructor - initializes the buffer
    InputBuffer() : count(0) {
        buffer[0] = buffer[1] = buffer[2] = 0;  // Clear all buffer positions
        lastInputTime = std::chrono::steady_clock::now();  // Set initial timestamp
    }

    // Reset the buffer to its initial state
    void reset() {
        count = 0;  // Reset character count
        buffer[0] = buffer[1] = buffer[2] = 0;  // Clear all buffer positions
    }

    // Check if the current input sequence has timed out
    bool isTimeout() {
        auto now = std::chrono::steady_clock::now();  // Get current time
        // Calculate time since last input in milliseconds
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastInputTime).count();
        return elapsed > 50;  // Return true if more than 50ms have passed
    }
};

// Global input buffer
InputBuffer inputBuffer;

void clearScreen();
void initGame(GameState& game);
void gameLoop();
char getChar();
int kbhit();
int getFallSpeed(int level);
void drawStartScreen();
void drawGame(const GameState& game);
void enableRawMode();
void disableRawMode();
void handleInput(GameState& game, char key);
void rotatePiece(GameState& game, bool clockwise);
void movePieceHorizontal(GameState& game, int dx);
void hardDrop(GameState& game);
bool checkCollision(const GameState& game, int piece, int rotation, int x, int y);
void lockPiece(GameState& game);
int clearLines(GameState& game);
bool movePieceDown(GameState& game);

int main() {
    // Seed random number generator
    srand(static_cast<unsigned int>(time(nullptr)));

    // Function to enable raw mode for keyboard input
    enableRawMode();

    // Hide cursor
    std::cout << "\033[?25l";

     // Clear screen
    clearScreen();

    drawStartScreen();

    // Wait for key press
    while (!kbhit()) {
        this_thread::sleep_for(chrono::milliseconds(100));
    }

    // Consume the key
    getChar();

    // Start game loop
    gameLoop();

    // Show cursor
    cout << "\033[?25h";

    // Clear screen
    clearScreen();

    cout << "Thanks for playing Tetris!\n";

    return 0;
}
void clearScreen() {
    cout << "\033[2J\033[H";
}
void initGame(GameState& game) {
    // Clear board
    for (int i = 0; i < BOARD_HEIGHT; i++) {
        for (int j = 0; j < BOARD_WIDTH; j++) {
            game.board[i][j] = -1;
        }
    }

    game.currentPiece = rand() % 7;
    game.nextPiece = rand() % 7;
    game.currentRotation = 0;
    game.currentX = BOARD_WIDTH / 2 - 2;
    game.currentY = 0;
    game.score = 0;
    game.level = 1;
    game.linesCleared = 0;
    game.gameOver = false;
    game.paused = false;
    game.lastMoveTime = chrono::steady_clock::now();
}
void gameLoop() {
    GameState game;

    // Initialize game state
    initGame(game);

    auto lastUpdate = chrono::steady_clock::now();
    auto lastDraw = chrono::steady_clock::now();

    while (true) {
        auto currentTime = chrono::steady_clock::now();
        auto elapsed = chrono::duration_cast<chrono::milliseconds>(
            currentTime - lastUpdate).count();
        auto drawElapsed = chrono::duration_cast<chrono::milliseconds>(
            currentTime - lastDraw).count();

        // Handle input - read all available characters
        char key;
        while ((key = getChar()) != 0) {
            if (key == 'q' || key == 'Q') {
                if (game.gameOver || game.paused) {
                    return;
                }
            }
            handleInput(game, key);
        }

        // Auto drop based on level
        if (!game.paused && !game.gameOver) {
            int fallSpeed = getFallSpeed(game.level);
            if (elapsed >= fallSpeed) {
                movePieceDown(game);
                lastUpdate = currentTime;
            }
        }

        // Draw game at controlled rate (avoid flickering)
        if (drawElapsed >= 50) { // Update display at ~20 FPS
            drawGame(game);
            cout.flush();
            lastDraw = currentTime;
        }

        // Sleep to control frame rate and reduce CPU usage
        this_thread::sleep_for(chrono::milliseconds(10));
    }
}
int kbhit() {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    return select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
}
char getChar() {
    // Function to get character from keyboard
    char c = 0;
    if (kbhit()) {
        read(STDIN_FILENO, &c, 1);
    }
    return c;
}
int getFallSpeed(int level) {
    return max(100, 1000 - (level - 1) * 100);
}
void drawStartScreen() {
    // Calculate width for start screen (same as board display)
    int startScreenWidth = BOARD_WIDTH * 2 + 2;

    // Top border
    cout << "╔";
    for (int i = 0; i < startScreenWidth; i++) cout << "═";
    cout << "╗\n";

    // Empty row
    cout << "║";
    for (int i = 0; i < startScreenWidth; i++) cout << " ";
    cout << "║\n";

    // Title row - "TETRIS GAME"
    cout << "║";
    string title = "TETRIS GAME";
    int titlePadding = startScreenWidth - title.length();
    int titleLeft = titlePadding / 2;
    int titleRight = titlePadding - titleLeft;
    for (int i = 0; i < titleLeft; i++) cout << " ";
    cout << title;
    for (int i = 0; i < titleRight; i++) cout << " ";
    cout << "║\n";

    // Empty row
    cout << "║";
    for (int i = 0; i < startScreenWidth; i++) cout << " ";
    cout << "║\n";

    // "Press any key to start..." row
    cout << "║";
    string prompt = "Press any key to start...";
    int promptPadding = startScreenWidth - prompt.length();
    int promptLeft = promptPadding / 2;
    int promptRight = promptPadding - promptLeft;
    for (int i = 0; i < promptLeft; i++) cout << " ";
    cout << prompt;
    for (int i = 0; i < promptRight; i++) cout << " ";
    cout << "║\n";

    // Empty row
    cout << "║";
    for (int i = 0; i < startScreenWidth; i++) cout << " ";
    cout << "║\n";

    // Bottom border
    cout << "╚";
    for (int i = 0; i < startScreenWidth; i++) cout << "═";
    cout << "╝\n";

}
void drawGame(const GameState& game) {
    clearScreen();

    // Calculate widths for the board and next piece sections
    int boardWidth = BOARD_WIDTH * 2 + 2;  // 2 chars per cell + 2 for spaces (left and right)
    int nextPieceWidth = 14;  // Width of next piece section

    // Top border
    cout << "╔";
    for (int i = 0; i < boardWidth; i++) cout << "═"; cout << "╦";
    for (int i = 0; i < nextPieceWidth; i++) cout << "═"; cout << "╗\n";

    // Title row
    cout << "║";
    string titleText = " TETRIS GAME";
    int titleLen = titleText.length();
    int totalPadding = boardWidth - titleLen;
    int leftPad = totalPadding / 2;
    int rightPad = totalPadding - leftPad;

    for (int i = 0; i < leftPad; i++) cout << " "; cout << titleText;
    for (int i = 0; i < rightPad; i++) cout << " "; cout << "║   NEXT PIECE ║\n";

    // Divider
    cout << "╠";
    for (int i = 0; i < boardWidth; i++) cout << "═"; cout << "╬";
    for (int i = 0; i < nextPieceWidth; i++) cout << "═"; cout << "╣\n";

    // Draw board with next piece
    for (int i = 0; i < BOARD_HEIGHT; i++) {
        cout << "║ ";

        // Draw current piece on board
        for (int j = 0; j < BOARD_WIDTH; j++) {
            bool isPiece = false;

            // Check if current piece occupies this position (only if not game over)
            if (!game.gameOver) {
                for (int pi = 0; pi < 4; pi++) {
                    for (int pj = 0; pj < 4; pj++) {
                        if (SHAPES[game.currentPiece][game.currentRotation][pi][pj]) {
                            if (game.currentY + pi == i && game.currentX + pj == j) {
                                cout << PIECE_COLORS[game.currentPiece] << "██" << COLOR_RESET;
                                isPiece = true;
                                break;
                            }
                        }
                    }
                    if (isPiece) break;
                }
            }

            if (!isPiece) {
                if (game.board[i][j] == -1) {
                    cout << "··";
                } else {
                    cout << PIECE_COLORS[game.board[i][j]] << "██" << COLOR_RESET;
                }
            }
        }

        cout << " ║";

        // Draw next piece preview
        if (i < 6) {
            if (i == 0) {
                for (int k = 0; k < nextPieceWidth; k++) cout << " ";
                cout << "║";
            } else if (i >= 1 && i <= 4) {
                cout << "   ";  // Left padding for centering (3 spaces)
                for (int j = 0; j < 4; j++) {
                    if (SHAPES[game.nextPiece][0][i-1][j]) {
                        cout << PIECE_COLORS[game.nextPiece] << "██" << COLOR_RESET;
                    } else {
                        cout << "  ";
                    }
                }
                cout << "   ║";  // Right padding for centering (3 spaces)
            } else {
                for (int k = 0; k < nextPieceWidth; k++) cout << " ";
                cout << "║";
            }
        } else if (i == 7) {
            for (int k = 0; k < nextPieceWidth; k++) cout << "─";
            cout << "║";
        } else if (i == 8) {
            cout << " SCORE:       ║";
        } else if (i == 9) {
            char scoreStr[20];
            snprintf(scoreStr, sizeof(scoreStr), " %-13d║", game.score);
            cout << scoreStr;
        } else if (i == 10) {
            for (int k = 0; k < nextPieceWidth; k++) cout << " ";
            cout << "║";
        } else if (i == 11) {
            cout << " LEVEL:       ║";
        } else if (i == 12) {
            char levelStr[20];
            snprintf(levelStr, sizeof(levelStr), " %-13d║", game.level);
            cout << levelStr;
        } else if (i == 13) {
            for (int k = 0; k < nextPieceWidth; k++) cout << " ";
            cout << "║";
        } else if (i == 14) {
            cout << " LINES:       ║";
        } else if (i == 15) {
            char linesStr[20];
            snprintf(linesStr, sizeof(linesStr), " %-13d║", game.linesCleared);
            cout << linesStr;
        } else {
            for (int k = 0; k < nextPieceWidth; k++) cout << " ";
            cout << "║";
        }

        cout << "\n";
    }

    // Bottom border
    cout << "╚";
    for (int i = 0; i < boardWidth; i++) cout << "═";
    cout << "╩";
    for (int i = 0; i < nextPieceWidth; i++) cout << "═";
    cout << "╝\n";

    cout << "\nControls: ←→ or A/D (Move)  ↑/W (Rotate)  ↓/S (Soft Drop)  SPACE (Hard Drop)  P (Pause)  Q (Quit)\n";

    if (game.paused) {
        cout << "\n*** PAUSED - Press P to Resume ***\n";
    }

    if (game.gameOver) {
        cout << "\n*** GAME OVER - Press R to Restart or Q to Quit ***\n";
    }
}
void disableRawMode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}
void enableRawMode() {
    // 1. Get current terminal attributes and save them
    tcgetattr(STDIN_FILENO, &orig_termios);

    // 2. Register disableRawMode to be called at program exit
    atexit(disableRawMode);

    // 3. Create a copy of the original terminal settings
    struct termios raw = orig_termios;

    // 4. Disable echo (don't show typed characters) and canonical mode (read input immediately)
    raw.c_lflag &= ~(ECHO | ICANON);

    // 5. Set minimum number of characters to read (0 means return immediately)
    raw.c_cc[VMIN] = 0;

    // 6. Set maximum wait time in deciseconds (0 means return immediately)
    raw.c_cc[VTIME] = 0;

    // 7. Apply the new terminal settings
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    // 8. Set non-blocking mode for the file descriptor
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
}
void hardDrop(GameState& game) {
    int dropped = 0;
    while (movePieceDown(game)) {
        game.score += 2; // Bonus points for hard drop
        dropped++;
    }
}
bool checkCollision(const GameState& game, int piece, int rotation, int x, int y) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (SHAPES[piece][rotation][i][j]) {
                int newX = x + j;
                int newY = y + i;

                // Check boundaries
                if (newX < 0 || newX >= BOARD_WIDTH || newY >= BOARD_HEIGHT) {
                    return true;
                }

                // Check collision with board (but not if above board)
                if (newY >= 0 && game.board[newY][newX] != -1) {
                    return true;
                }
            }
        }
    }
    return false;
}
void lockPiece(GameState& game) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            if (SHAPES[game.currentPiece][game.currentRotation][i][j]) {
                int newX = game.currentX + j;
                int newY = game.currentY + i;
                if (newY >= 0 && newY < BOARD_HEIGHT && newX >= 0 && newX < BOARD_WIDTH) {
                    game.board[newY][newX] = game.currentPiece;
                }
            }
        }
    }
}
void spawnPiece(GameState& game) {
    game.currentPiece = game.nextPiece;
    game.nextPiece = rand() % 7;
    game.currentRotation = 0;
    game.currentX = BOARD_WIDTH / 2 - 2;
    game.currentY = 0;

    // Check if game over - only if new piece immediately collides at spawn
    if (checkCollision(game, game.currentPiece, game.currentRotation, game.currentX, game.currentY)) {
        game.gameOver = true;
    }
}
int clearLines(GameState& game) {
    int cleared = 0;

    for (int i = BOARD_HEIGHT - 1; i >= 0; i--) {
        bool full = true;
        for (int j = 0; j < BOARD_WIDTH; j++) {
            if (game.board[i][j] == -1) {
                full = false;
                break;
            }
        }

        if (full) {
            cleared++;
            // Move all lines above down
            for (int k = i; k > 0; k--) {
                for (int j = 0; j < BOARD_WIDTH; j++) {
                    game.board[k][j] = game.board[k-1][j];
                }
            }
            // Clear top line
            for (int j = 0; j < BOARD_WIDTH; j++) {
                game.board[0][j] = -1;
            }
            i++; // Check this line again
        }
    }

    return cleared;
}
bool movePieceDown(GameState& game) {
    if (!checkCollision(game, game.currentPiece, game.currentRotation, game.currentX, game.currentY + 1)) {
        game.currentY++;
        return true;
    } else {
        // Lock piece and spawn new one
        lockPiece(game);
        int cleared = clearLines(game);

        if (cleared > 0) {
            game.linesCleared += cleared;
            game.score += cleared * 100 * game.level;
            game.level = 1 + game.linesCleared / 10;

            // if (lineClearSound) {
            //     AudioServicesPlaySystemSound(lineClearSound);
            // }
        }

        spawnPiece(game);
        return false;
    }
}
void handleInput(GameState& game, char key) {
    if (game.gameOver) {
        if (key == 'r' || key == 'R') {
            initGame(game);
        }
        return;
    }

    if (key == 'p' || key == 'P') {
        game.paused = !game.paused;
        return;
    }

    if (game.paused) return;

    // Check for timeout and reset buffer
    if (inputBuffer.isTimeout() && inputBuffer.count > 0) {
        inputBuffer.reset();
    }

    // Handle arrow keys (escape sequences)
    if (key == 27) { // ESC
        inputBuffer.reset();
        inputBuffer.buffer[inputBuffer.count++] = key;
        inputBuffer.lastInputTime = chrono::steady_clock::now();
        return;
    } else if (inputBuffer.count > 0 && inputBuffer.count < 3) {
        inputBuffer.buffer[inputBuffer.count++] = key;
        inputBuffer.lastInputTime = chrono::steady_clock::now();

        if (inputBuffer.count == 3) {
            if (inputBuffer.buffer[1] == '[') {
                switch (inputBuffer.buffer[2]) {
                    case 'A': // Up arrow
                        rotatePiece(game, true);
                        break;
                    case 'B': // Down arrow
                        movePieceDown(game);
                        game.score += 1;
                        break;
                    case 'C': // Right arrow
                        movePieceHorizontal(game, 1);
                        break;
                    case 'D': // Left arrow
                        movePieceHorizontal(game, -1);
                        break;
                }
            }
            inputBuffer.reset();
        }
        return;
    }

    // Regular keys
    switch (key) {
        case 'a':
        case 'A':
            movePieceHorizontal(game, -1);
            break;
        case 'd':
        case 'D':
            movePieceHorizontal(game, 1);
            break;
        case 'w':
        case 'W':
            rotatePiece(game, true);
            break;
        case 's':
        case 'S':
            movePieceDown(game);
            game.score += 1;
            break;
        case ' ':
            hardDrop(game);
            break;
    }
}

void rotatePiece(GameState& game, bool clockwise) {
    int newRotation = clockwise ? (game.currentRotation + 1) % 4 : (game.currentRotation + 3) % 4;

    // Try basic rotation
    if (!checkCollision(game, game.currentPiece, newRotation, game.currentX, game.currentY)) {
        game.currentRotation = newRotation;
        return;
    }

    // Wall kick attempts
    int kicks[5][2] = {{0,0}, {-1,0}, {1,0}, {0,-1}, {0,1}};
    for (int i = 0; i < 5; i++) {
        if (!checkCollision(game, game.currentPiece, newRotation,
                          game.currentX + kicks[i][0], game.currentY + kicks[i][1])) {
            game.currentRotation = newRotation;
            game.currentX += kicks[i][0];
            game.currentY += kicks[i][1];
            return;
        }
    }
}

void movePieceHorizontal(GameState& game, int dx) {
    if (!checkCollision(game, game.currentPiece, game.currentRotation, game.currentX + dx, game.currentY)) {
        game.currentX += dx;
    }
}