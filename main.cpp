#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <random>

using namespace std;

constexpr int BOARD_HEIGHT     = 20;
constexpr int BOARD_WIDTH      = 15;
constexpr int NEXT_PICE_WIDTH  = 14;

constexpr int BLOCK_SIZE       = 4;
constexpr int NUM_BLOCK_TYPES  = 7;

// gameplay tuning
constexpr long BASE_DROP_SPEED_US   = 500000; // base drop speed (µs)
constexpr int  DROP_INTERVAL_TICKS  = 5;      // logic steps per drop

struct Position {
    int x{}, y{};
    Position() = default;
    Position(int _x, int _y) : x(_x), y(_y) {}
};

struct GameState {
    bool running{true};
    bool paused{false};
    bool ghostEnabled{true};  // Ghost shadow enabled by default
    bool quitByUser{false};   // Track if user quit manually vs. game over
    int score{0};
    int level{1};
    int linesCleared{0};
};

struct Piece {
    int type{0};
    int rotation{0};
    Position pos{5, 0};
};

struct Board {
    char grid[BOARD_HEIGHT][BOARD_WIDTH]{};

    void init() {
        // Initialize entire grid as empty spaces
        // Borders will be drawn separately in draw() function
        for (int i = 0; i < BOARD_HEIGHT; ++i) {
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                grid[i][j] = ' ';
            }
        }
    }

    void draw(const GameState& state, const string nextPieceLines[4]) const {
        // Build entire frame in a string buffer for single output
        string frame;
        frame.reserve(3072); // Pre-allocate to avoid reallocation

        // Clear screen + move cursor to top-left
        frame += "\033[2J\033[1;1H";
        const string title = "TETRIS GAME";

        // Top border (simple ASCII)
        frame += '+';
        frame.append(BOARD_WIDTH, '-');
        frame += '+';
        frame.append(NEXT_PICE_WIDTH, '-');
        frame += "+\n";

        // Title row
        frame += '|';
        int totalPadding = BOARD_WIDTH - title.size();
        int leftPad = totalPadding / 2;
        int rightPad = totalPadding - leftPad;

        frame.append(leftPad, ' ');
        frame += title;
        frame.append(rightPad, ' ');
        frame += "|  NEXT PIECE  |\n";

        // Divider
        frame += '+';
        frame.append(BOARD_WIDTH, '-');
        frame += '+';
        frame.append(NEXT_PICE_WIDTH, '-');
        frame += "+\n";

        // Draw board rows with borders
        for (int i = 0; i < BOARD_HEIGHT; ++i) {
            // Left border
            frame += '|';

            // Draw board cells
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                frame += grid[i][j];
            }

            // Right border
            frame += '|';

            // Draw right side panel with next piece preview and stats
            if (i == 0) {
                frame += "              |";
            } else if (i >= 1 && i <= 4) {
                // Draw next piece preview line
                frame += "     ";  // Left padding (5 spaces)
                frame += nextPieceLines[i - 1];  // 4 chars for the piece
                frame += "     |";  // Right padding (5 spaces) + border
            } else if (i == 5) {
                frame.append(NEXT_PICE_WIDTH, '-');
                frame += '|';
            } else if (i == 6) {
                // Score display
                char buf[20];
                snprintf(buf, sizeof(buf), " SCORE: %-6d", state.score);
                frame += buf;
                frame += '|';
            } else if (i == 7) {
                // Level display
                char buf[20];
                snprintf(buf, sizeof(buf), " LEVEL: %-6d", state.level);
                frame += buf;
                frame += '|';
            } else if (i == 8) {
                // Lines cleared display
                char buf[20];
                snprintf(buf, sizeof(buf), " LINES: %-6d", state.linesCleared);
                frame += buf;
                frame += '|';
            } else {
                frame.append(NEXT_PICE_WIDTH, ' ');
                frame += '|';
            }

            frame += '\n';
        }

        // Bottom border
        frame += '+';
        frame.append(BOARD_WIDTH, '-');
        frame += '+';
        frame.append(NEXT_PICE_WIDTH, '-');
        frame += "+\n";

        frame += "Controls: ←→ or A/D (Move)  ↑/W (Rotate)  ↓/S (Soft Drop)  SPACE (Hard Drop)  G (Ghost)  P (Pause)  Q (Quit)\n";

        // Single output call
        cout << frame;
        cout.flush();
    }

    int clearLines() {
        int writeRow = BOARD_HEIGHT - 1;
        int linesCleared = 0;

        // Scan from bottom to top
        for (int readRow = BOARD_HEIGHT - 1; readRow >= 0; --readRow) {
            bool full = true;
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                if (grid[readRow][j] == ' ') {
                    full = false;
                    break;
                }
            }

            // Keep non-full rows, skip full ones
            if (!full) {
                if (writeRow != readRow) {
                    for (int j = 0; j < BOARD_WIDTH; ++j) {
                        grid[writeRow][j] = grid[readRow][j];
                    }
                }
                --writeRow;
            } else {
                ++linesCleared;
            }
        }

        // Clear remaining top rows
        while (writeRow >= 0) {
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                grid[writeRow][j] = ' ';
            }
            --writeRow;
        }

        return linesCleared;
    }
};

struct BlockTemplate {
    static char templates[NUM_BLOCK_TYPES][BLOCK_SIZE][BLOCK_SIZE];

    static void setBlockTemplate(int type,
                                 char symbol,
                                 const int shape[BLOCK_SIZE][BLOCK_SIZE]) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                templates[type][i][j] = shape[i][j] ? symbol : ' ';
            }
        }
    }

    static void initializeTemplates() {
        static const int TETROMINOES[7][4][4] = {
            // I
            {
                {0,1,0,0},
                {0,1,0,0},
                {0,1,0,0},
                {0,1,0,0}
            },
            // O
            {
                {0,0,0,0},
                {0,1,1,0},
                {0,1,1,0},
                {0,0,0,0}
            },
            // T
            {
                {0,0,0,0},
                {0,1,0,0},
                {1,1,1,0},
                {0,0,0,0}
            },
            // S
            {
                {0,0,0,0},
                {0,1,1,0},
                {1,1,0,0},
                {0,0,0,0}
            },
            // Z
            {
                {0,0,0,0},
                {1,1,0,0},
                {0,1,1,0},
                {0,0,0,0}
            },
            // J
            {
                {0,0,0,0},
                {1,0,0,0},
                {1,1,1,0},
                {0,0,0,0}
            },
            // L
            {
                {0,0,0,0},
                {0,0,1,0},
                {1,1,1,0},
                {0,0,0,0}
            }
        };

        static const char NAMES[7] = {'I','O','T','S','Z','J','L'};

        for (int i = 0; i < 7; i++) {
            setBlockTemplate(i, NAMES[i], TETROMINOES[i]);
        }
    }

    // rotation: 0-3 (90° steps clockwise)
    static char getCell(int type, int rotation, int row, int col) {
        int r = row;
        int c = col;

        for (int i = 0; i < rotation; ++i) {
            int temp = 3 - c;
            c = r;
            r = temp;
        }

        return templates[type][r][c];
    }
};

char BlockTemplate::templates[NUM_BLOCK_TYPES][BLOCK_SIZE][BLOCK_SIZE];

struct TetrisGame {
    Board board;
    GameState state;
    Piece currentPiece{};
    int nextPieceType{0};  // Store the type of next piece to display

    termios origTermios{};
    long dropSpeedUs{BASE_DROP_SPEED_US};
    int dropCounter{0};
    bool softDropActive{false};  // Track if 's' key is being held for soft drop

    mt19937 rng;

    TetrisGame() {
        random_device rd;
        rng.seed(rd());
    }

    void drawStartScreen() {
        // Build entire start screen in a string buffer for single output
        string screen;
        screen.reserve(512); // Pre-allocate to avoid reallocation

        // Clear screen + move cursor to top-left
        screen += "\033[2J\033[1;1H";

        // Calculate width for start screen (match board display width)
        int totalWidth = BOARD_WIDTH + NEXT_PICE_WIDTH + 2; // +2 for borders

        // Top border (ASCII)
        screen += '+';
        screen.append(totalWidth, '-');
        screen += "+\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Title row - "TETRIS GAME"
        const string title = "TETRIS GAME";
        int titlePadding = totalWidth - title.length();
        int titleLeft = titlePadding / 2;
        int titleRight = titlePadding - titleLeft;

        screen += '|';
        screen.append(titleLeft, ' ');
        screen += title;
        screen.append(titleRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // "Press any key to start..." row
        const string prompt = "Press any key to start...";
        int promptPadding = totalWidth - prompt.length();
        int promptLeft = promptPadding / 2;
        int promptRight = promptPadding - promptLeft;

        screen += '|';
        screen.append(promptLeft, ' ');
        screen += prompt;
        screen.append(promptRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Bottom border
        screen += '+';
        screen.append(totalWidth, '-');
        screen += "+\n";

        // Single output call
        cout << screen;
        cout.flush();
    }

    char waitForKeyPress() {
        // Enable raw mode to capture single key press
        enableRawMode();

        // Wait for any key press
        char key = 0;
        while ((key = getInput()) == 0) {
            usleep(50000); // Sleep 50ms to avoid busy-waiting
        }

        // Flush any additional input
        flushInput();

        return key;
    }

    int saveAndGetRank() {
        // Read existing scores
        vector<int> scores;
        ifstream inFile("high_scores.txt");
        if (inFile.is_open()) {
            int score;
            while (inFile >> score) {
                scores.push_back(score);
            }
            inFile.close();
        }

        // Add current score
        scores.push_back(state.score);

        // Sort in descending order
        sort(scores.begin(), scores.end(), greater<int>());

        // Keep only top 10 scores
        if (scores.size() > 10) {
            scores.resize(10);
        }

        // Write back to file
        ofstream outFile("high_scores.txt");
        if (outFile.is_open()) {
            for (int score : scores) {
                outFile << score << "\n";
            }
            outFile.close();
        }

        // Find rank of current score
        int rank = 1;
        for (int score : scores) {
            if (score == state.score) {
                break;
            }
            rank++;
        }

        return rank;
    }

    void drawGameOverScreen(int rank) {
        // Build entire game over screen in a string buffer for single output
        string screen;
        screen.reserve(1024); // Pre-allocate to avoid reallocation

        // Clear screen + move cursor to top-left
        screen += "\033[2J\033[1;1H";

        // Calculate width for game over screen
        int totalWidth = BOARD_WIDTH + NEXT_PICE_WIDTH + 2;

        // Top border
        screen += '+';
        screen.append(totalWidth, '-');
        screen += "+\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // "GAME OVER" title
        const string title = "GAME OVER";
        int titlePadding = totalWidth - title.length();
        int titleLeft = titlePadding / 2;
        int titleRight = titlePadding - titleLeft;

        screen += '|';
        screen.append(titleLeft, ' ');
        screen += title;
        screen.append(titleRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Score display
        char scoreBuf[64];
        snprintf(scoreBuf, sizeof(scoreBuf), "Final Score: %d", state.score);
        string scoreStr(scoreBuf);
        int scorePadding = totalWidth - scoreStr.length();
        int scoreLeft = scorePadding / 2;
        int scoreRight = scorePadding - scoreLeft;

        screen += '|';
        screen.append(scoreLeft, ' ');
        screen += scoreStr;
        screen.append(scoreRight, ' ');
        screen += "|\n";

        // Level display
        char levelBuf[64];
        snprintf(levelBuf, sizeof(levelBuf), "Level: %d", state.level);
        string levelStr(levelBuf);
        int levelPadding = totalWidth - levelStr.length();
        int levelLeft = levelPadding / 2;
        int levelRight = levelPadding - levelLeft;

        screen += '|';
        screen.append(levelLeft, ' ');
        screen += levelStr;
        screen.append(levelRight, ' ');
        screen += "|\n";

        // Lines display
        char linesBuf[64];
        snprintf(linesBuf, sizeof(linesBuf), "Lines Cleared: %d", state.linesCleared);
        string linesStr(linesBuf);
        int linesPadding = totalWidth - linesStr.length();
        int linesLeft = linesPadding / 2;
        int linesRight = linesPadding - linesLeft;

        screen += '|';
        screen.append(linesLeft, ' ');
        screen += linesStr;
        screen.append(linesRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Rank display with ordinal suffix
        char rankBuf[64];
        const char* suffix = "th";
        if (rank == 1) suffix = "st";
        else if (rank == 2) suffix = "nd";
        else if (rank == 3) suffix = "rd";
        snprintf(rankBuf, sizeof(rankBuf), "Your Rank: %d%s", rank, suffix);
        string rankStr(rankBuf);
        int rankPadding = totalWidth - rankStr.length();
        int rankLeft = rankPadding / 2;
        int rankRight = rankPadding - rankLeft;

        screen += '|';
        screen.append(rankLeft, ' ');
        screen += rankStr;
        screen.append(rankRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // "Press R to Restart or Q to Quit" prompt
        const string prompt = "Press R to Restart or Q to Quit";
        int promptPadding = totalWidth - prompt.length();
        int promptLeft = promptPadding / 2;
        int promptRight = promptPadding - promptLeft;

        screen += '|';
        screen.append(promptLeft, ' ');
        screen += prompt;
        screen.append(promptRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Bottom border
        screen += '+';
        screen.append(totalWidth, '-');
        screen += "+\n";

        // Single output call
        cout << screen;
        cout.flush();
    }

    // ---------- helper methods ----------

    void resetGame() {
        // Reset game state
        state.running = true;
        state.paused = false;
        state.quitByUser = false;
        state.score = 0;
        state.level = 1;
        state.linesCleared = 0;

        // Reset board
        board.init();

        // Reset timing
        dropCounter = 0;
        softDropActive = false;

        // Generate new next piece
        uniform_int_distribution<int> dist(0, NUM_BLOCK_TYPES - 1);
        nextPieceType = dist(rng);

        // Spawn first piece
        spawnNewPiece();
    }

    void drawPauseScreen() const {
        // Build pause overlay in a string buffer
        string screen;
        screen.reserve(1024);

        // Clear screen + move cursor to top-left
        screen += "\033[2J\033[1;1H";

        // Calculate width for pause screen
        int totalWidth = BOARD_WIDTH + NEXT_PICE_WIDTH + 2;

        // Top border
        screen += '+';
        screen.append(totalWidth, '-');
        screen += "+\n";

        // Empty rows for spacing
        for (int i = 0; i < 3; ++i) {
            screen += '|';
            screen.append(totalWidth, ' ');
            screen += "|\n";
        }

        // "GAME PAUSED" title
        const string title = "GAME PAUSED";
        int titlePadding = totalWidth - title.length();
        int titleLeft = titlePadding / 2;
        int titleRight = titlePadding - titleLeft;

        screen += '|';
        screen.append(titleLeft, ' ');
        screen += title;
        screen.append(titleRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Current stats - Score
        char scoreBuf[64];
        snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", state.score);
        string scoreStr(scoreBuf);
        int scorePadding = totalWidth - scoreStr.length();
        int scoreLeft = scorePadding / 2;
        int scoreRight = scorePadding - scoreLeft;

        screen += '|';
        screen.append(scoreLeft, ' ');
        screen += scoreStr;
        screen.append(scoreRight, ' ');
        screen += "|\n";

        // Level
        char levelBuf[64];
        snprintf(levelBuf, sizeof(levelBuf), "Level: %d", state.level);
        string levelStr(levelBuf);
        int levelPadding = totalWidth - levelStr.length();
        int levelLeft = levelPadding / 2;
        int levelRight = levelPadding - levelLeft;

        screen += '|';
        screen.append(levelLeft, ' ');
        screen += levelStr;
        screen.append(levelRight, ' ');
        screen += "|\n";

        // Lines
        char linesBuf[64];
        snprintf(linesBuf, sizeof(linesBuf), "Lines: %d", state.linesCleared);
        string linesStr(linesBuf);
        int linesPadding = totalWidth - linesStr.length();
        int linesLeft = linesPadding / 2;
        int linesRight = linesPadding - linesLeft;

        screen += '|';
        screen.append(linesLeft, ' ');
        screen += linesStr;
        screen.append(linesRight, ' ');
        screen += "|\n";

        // Empty row
        screen += '|';
        screen.append(totalWidth, ' ');
        screen += "|\n";

        // Menu options
        const string resumeOption = "P - Resume";
        int resumePadding = totalWidth - resumeOption.length();
        int resumeLeft = resumePadding / 2;
        int resumeRight = resumePadding - resumeLeft;

        screen += '|';
        screen.append(resumeLeft, ' ');
        screen += resumeOption;
        screen.append(resumeRight, ' ');
        screen += "|\n";

        const string quitOption = "Q - Quit";
        int quitPadding = totalWidth - quitOption.length();
        int quitLeft = quitPadding / 2;
        int quitRight = quitPadding - quitLeft;

        screen += '|';
        screen.append(quitLeft, ' ');
        screen += quitOption;
        screen.append(quitRight, ' ');
        screen += "|\n";

        // Empty rows for spacing
        for (int i = 0; i < 3; ++i) {
            screen += '|';
            screen.append(totalWidth, ' ');
            screen += "|\n";
        }

        // Bottom border
        screen += '+';
        screen.append(totalWidth, '-');
        screen += "+\n";

        // Single output call
        cout << screen;
        cout.flush();
    }

    void getNextPiecePreview(string lines[4]) const {
        // Render the next piece as 4 lines of 4 characters each
        for (int row = 0; row < 4; ++row) {
            lines[row] = "";
            for (int col = 0; col < 4; ++col) {
                char cell = BlockTemplate::getCell(nextPieceType, 0, row, col);
                lines[row] += cell;
            }
        }
    }

    void animateGameOver() {
        // Transform all locked pieces to '#' one by one from bottom to top
        // This creates a cascade effect showing the game is ending

        constexpr int ANIM_DELAY_US = 15000; // 15ms per cell for smooth animation

        // Scan from bottom to top, left to right
        for (int i = BOARD_HEIGHT - 1; i >= 0; --i) {
            bool hasBlock = false;
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                if (board.grid[i][j] != ' ') {
                    hasBlock = true;
                    board.grid[i][j] = '#';

                    // Draw immediately for smooth animation
                    string preview[4];
                    getNextPiecePreview(preview);
                    board.draw(state, preview);

                    usleep(ANIM_DELAY_US);
                }
            }

            // Skip empty rows to speed up animation
            if (!hasBlock) continue;
        }

        // Final pause to see completed effect
        flushInput();
        usleep(500000); // 300ms final pause
        flushInput();
    }

    // ---------- terminal handling (POSIX) ----------

    void enableRawMode() {
        tcgetattr(STDIN_FILENO, &origTermios);

        termios raw = origTermios;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;

        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    }

    void disableRawMode() {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &origTermios);
    }

    char getInput() const {
        char ch = 0;
        ssize_t result = read(STDIN_FILENO, &ch, 1);
        if (result <= 0) return 0;

        // Detect arrow keys (escape sequences)
        // Arrow keys send: ESC [ A/B/C/D
        if (ch == 27) { // ESC
            char seq[2];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) return 27;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) return 27;

            if (seq[0] == '[') {
                switch (seq[1]) {
                    case 'A': return 'w'; // Up arrow -> rotate
                    case 'B': return 's'; // Down arrow -> soft drop
                    case 'C': return 'd'; // Right arrow -> move right
                    case 'D': return 'a'; // Left arrow -> move left
                }
            }
            return 27;
        }

        return ch;
    }

    void flushInput() const {
        tcflush(STDIN_FILENO, TCIFLUSH);
    }

    // ---------- helpers for spawn / movement ----------

    bool isInsidePlayfield(int x, int y) const {
        return x >= 0 && x < BOARD_WIDTH &&
               y >= 0 && y < BOARD_HEIGHT;
    }

    // Calculate where the current piece would land if dropped straight down
    Piece calculateGhostPiece() const {
        Piece ghost = currentPiece;

        // Keep moving down until we hit something
        // Check collision only with locked pieces (not dots or current piece)
        bool canMoveDown = true;
        while (canMoveDown) {
            canMoveDown = false;

            // Check if ghost can move down one more position
            for (int i = 0; i < BLOCK_SIZE; ++i) {
                for (int j = 0; j < BLOCK_SIZE; ++j) {
                    char cell = BlockTemplate::getCell(ghost.type, ghost.rotation, i, j);
                    if (cell == ' ') continue;

                    int xt = ghost.pos.x + j;
                    int yt = ghost.pos.y + i + 1;  // +1 for next position

                    // Check bounds
                    if (yt >= BOARD_HEIGHT) {
                        canMoveDown = false;
                        goto done_checking;
                    }

                    // Check collision with LOCKED pieces only (not dots or spaces)
                    // Locked pieces are letters (I, O, T, S, Z, J, L) or '#'
                    if (yt >= 0) {
                        char gridCell = board.grid[yt][xt];
                        if (gridCell != ' ' && gridCell != '.') {
                            canMoveDown = false;
                            goto done_checking;
                        }
                    }
                }
            }

            canMoveDown = true;
            done_checking:

            if (canMoveDown) {
                ghost.pos.y++;
            }
        }

        return ghost;
    }

    // Check if piece can spawn: verify bounds and no collision with existing blocks
    bool canSpawn(const Piece& piece) const {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(piece.type, piece.rotation, i, j);
                if (cell == ' ') continue;

                int xt = piece.pos.x + j;
                int yt = piece.pos.y + i;

                // Check horizontal bounds
                if (xt < 0 || xt >= BOARD_WIDTH) return false;

                // Check vertical bounds (bottom)
                if (yt >= BOARD_HEIGHT) return false;

                // Check collision with LOCKED blocks only (ignore ghost dots)
                if (yt >= 0) {
                    char gridCell = board.grid[yt][xt];
                    if (gridCell != ' ' && gridCell != '.') {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    bool canMove(int dx, int dy, int newRotation) const {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(
                    currentPiece.type, newRotation, i, j
                );
                if (cell == ' ') continue;

                int xt = currentPiece.pos.x + j + dx;
                int yt = currentPiece.pos.y + i + dy;

                // horizontal bounds (full playable area 0..BOARD_WIDTH-1)
                if (xt < 0 || xt >= BOARD_WIDTH) return false;

                // bottom collision with floor or blocks
                if (yt >= BOARD_HEIGHT) return false;

                // Check collision with LOCKED pieces only (ignore ghost dots)
                if (yt >= 0) {
                    char gridCell = board.grid[yt][xt];
                    if (gridCell != ' ' && gridCell != '.') {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    void placePiece(const Piece& piece, bool place) {
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(
                    piece.type, piece.rotation, i, j
                );
                if (cell == ' ') continue;

                int xt = piece.pos.x + j;
                int yt = piece.pos.y + i;

                if (yt < 0 || yt >= BOARD_HEIGHT ||
                    xt < 0 || xt >= BOARD_WIDTH) {
                    continue;
                }

                board.grid[yt][xt] = place ? cell : ' ';
            }
        }
    }

    void clearAllGhostDots() {
        // Clear all ghost dots from the board
        for (int i = 0; i < BOARD_HEIGHT; ++i) {
            for (int j = 0; j < BOARD_WIDTH; ++j) {
                if (board.grid[i][j] == '.') {
                    board.grid[i][j] = ' ';
                }
            }
        }
    }

    void placeGhostPiece(const Piece& ghostPiece) {
        // Place ghost piece using '.' character for outline effect
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(
                    ghostPiece.type, ghostPiece.rotation, i, j
                );
                if (cell == ' ') continue;

                int xt = ghostPiece.pos.x + j;
                int yt = ghostPiece.pos.y + i;

                if (yt < 0 || yt >= BOARD_HEIGHT ||
                    xt < 0 || xt >= BOARD_WIDTH) {
                    continue;
                }

                // Only draw ghost where there's empty space (don't overwrite actual pieces)
                if (board.grid[yt][xt] == ' ') {
                    board.grid[yt][xt] = '.';
                }
            }
        }
    }

    void placePieceSafe(const Piece& piece) {
        // Place piece without overwriting existing blocks
        // Used for game over visualization to show collision without overlap
        for (int i = 0; i < BLOCK_SIZE; ++i) {
            for (int j = 0; j < BLOCK_SIZE; ++j) {
                char cell = BlockTemplate::getCell(
                    piece.type, piece.rotation, i, j
                );
                if (cell == ' ') continue;

                int xt = piece.pos.x + j;
                int yt = piece.pos.y + i;

                if (yt < 0 || yt >= BOARD_HEIGHT ||
                    xt < 0 || xt >= BOARD_WIDTH) {
                    continue;
                }

                // Only place if cell is empty - don't overwrite existing blocks
                if (board.grid[yt][xt] == ' ') {
                    board.grid[yt][xt] = cell;
                }
            }
        }
    }

    void spawnNewPiece() {
        uniform_int_distribution<int> dist(0, NUM_BLOCK_TYPES - 1);

        // Create temporary piece to test spawn
        Piece testPiece;
        testPiece.type = nextPieceType;
        testPiece.rotation = 0;

        // Spawn near horizontal center, above visible board (y=-1)
        // This makes pieces appear from above the divider line
        int spawnX = (BOARD_WIDTH / 2) - (BLOCK_SIZE / 2);
        testPiece.pos = Position(spawnX, -1);

        // Always set currentPiece so it can be displayed even on game over
        currentPiece = testPiece;

        // Check if spawn is possible - if blocks at y=0 collide, game over
        if (!canSpawn(testPiece)) {
            state.running = false;
            return;
        }

        // Spawn is valid, generate new next piece
        nextPieceType = dist(rng);
    }

    bool lockPieceAndCheck() {
        // permanently place current piece
        placePiece(currentPiece, true);

        // Clear lines and update score
        int lines = board.clearLines();
        if (lines > 0) {
            state.linesCleared += lines;

            // Standard Tetris scoring: 1=40, 2=100, 3=300, 4=1200
            const int scores[] = {0, 40, 100, 300, 1200};
            state.score += scores[lines] * state.level;

            // Level up every 10 lines
            state.level = 1 + (state.linesCleared / 10);
        }

        // Try to spawn next piece - if it fails, game over
        spawnNewPiece();
        return state.running;
    }

    void softDrop() {
        if (canMove(0, 1, currentPiece.rotation)) {
            currentPiece.pos.y++;
        } else {
            // Don't lock if piece is still above visible board
            if (currentPiece.pos.y < 0) {
                state.running = false;
                return;
            }
            state.running = lockPieceAndCheck();
            dropCounter = 0;
        }
    }

    void hardDrop() {
        // Drop piece to the lowest possible position
        while (canMove(0, 1, currentPiece.rotation)) {
            currentPiece.pos.y++;
        }
        // Don't lock if piece is still above visible board
        if (currentPiece.pos.y < 0) {
            state.running = false;
            return;
        }
        state.running = lockPieceAndCheck();
        dropCounter = 0;
    }

    void handleInput() {
        char c = getInput();

        // Always update soft drop state based on current input
        // This ensures it's immediately deactivated when 's' is released
        if (c == 's' && !state.paused) {
            softDropActive = true;
        } else {
            softDropActive = false;
        }

        if (c == 0) return;

        // Handle pause input regardless of pause state
        if (c == 'p') {
            state.paused = !state.paused;
            flushInput(); // Clear input buffer when toggling pause
            if (state.paused) {
                drawPauseScreen();
            }
            return;
        }

        // Handle ghost toggle (can toggle even when paused)
        if (c == 'g') {
            state.ghostEnabled = !state.ghostEnabled;
            return;
        }

        // If paused, only allow quit and pause toggle
        if (state.paused) {
            if (c == 'q') {
                state.running = false;
                state.quitByUser = true;
            }
            return;
        }

        // Game is not paused - handle normal inputs
        switch (c) {
            case 'a': // move left
                if (canMove(-1, 0, currentPiece.rotation)) {
                    currentPiece.pos.x--;
                }
                break;
            case 'd': // move right
                if (canMove(1, 0, currentPiece.rotation)) {
                    currentPiece.pos.x++;
                }
                break;
            case 's': // soft drop (hold) - handled by gravity system
                // Just keep softDropActive = true (already set above)
                break;
            case 'x': // soft drop one cell (instant)
                softDrop();
                break;
            case ' ': // hard drop
                hardDrop();
                flushInput(); // flush repeated spaces
                break;
            case 'w': { // rotate with extended wall kicks
                int newRot = (currentPiece.rotation + 1) % 4;
                // Extended wall kick offsets to handle S/Z blocks near edges
                int kicks[] = {0, -1, 1, -2, 2, -3, 3};
                for (int dx : kicks) {
                    if (canMove(dx, 0, newRot)) {
                        currentPiece.pos.x += dx;
                        currentPiece.rotation = newRot;
                        break;
                    }
                }
                break;
            }
            case 'q':
                state.running = false;
                state.quitByUser = true;
                break;
            default:
                break;
        }
    }

    void handleGravity() {
        if (!state.running || state.paused) return;

        ++dropCounter;

        // Use faster drop interval when soft drop is active
        int effectiveInterval = softDropActive ? 1 : DROP_INTERVAL_TICKS;

        if (dropCounter < effectiveInterval) return;

        dropCounter = 0;

        if (canMove(0, 1, currentPiece.rotation)) {
            currentPiece.pos.y++;
        } else {
            // Don't lock if piece is still above visible board (y < 0)
            // This means the board is full at the top - game over
            if (currentPiece.pos.y < 0) {
                state.running = false;
                return;
            }
            state.running = lockPieceAndCheck();
        }
    }

    void run() {
        BlockTemplate::initializeTemplates();

        // Main game loop with restart support
        bool shouldRestart = true;

        while (shouldRestart) {
            // Reset for new game
            board.init();

            // Initialize the first next piece
            uniform_int_distribution<int> dist(0, NUM_BLOCK_TYPES - 1);
            nextPieceType = dist(rng);

            // Show start screen and wait for key press (only on first run)
            static bool firstRun = true;
            if (firstRun) {
                drawStartScreen();
                waitForKeyPress();
                firstRun = false;
            }

            // Spawn first piece
            spawnNewPiece();

            // Game loop
            while (state.running) {
                handleInput();

                // If user quit, exit immediately without rendering
                if (!state.running) {
                    break;
                }

                // Skip game logic and rendering when paused
                if (state.paused) {
                    usleep(50000); // Sleep 50ms to avoid busy-waiting
                    continue;
                }

                handleGravity();

                // Clear all ghost dots from previous frame
                clearAllGhostDots();

                // Calculate and draw ghost position (if enabled)
                if (state.ghostEnabled) {
                    Piece ghostPiece = calculateGhostPiece();
                    // Only draw ghost if it's different from current piece position
                    if (ghostPiece.pos.y != currentPiece.pos.y) {
                        placeGhostPiece(ghostPiece);
                    }
                }

                // Draw current piece on top
                placePiece(currentPiece, true);

                // Render the frame
                string preview[4];
                getNextPiecePreview(preview);
                board.draw(state, preview);

                // Clear current piece from board for next frame
                placePiece(currentPiece, false);

                usleep(dropSpeedUs / DROP_INTERVAL_TICKS);
            }

            // Game over - show final board state with the last piece (only if player lost)
            if (!state.quitByUser) {
                placePieceSafe(currentPiece);

                string preview[4];
                getNextPiecePreview(preview);
                board.draw(state, preview);

                // Brief pause to see the collision point
                flushInput();
                usleep(800000); // 800ms to see the final state
                flushInput();

                // Animate all locked pieces (including the final one) transforming to '#'
                animateGameOver();
            }

            // Show game over screen and wait for user choice
            int rank = saveAndGetRank();
            drawGameOverScreen(rank);

            char choice = waitForKeyPress();

            if (choice == 'r' || choice == 'R') {
                // Restart the game
                resetGame();
                shouldRestart = true;
            } else {
                // Quit the game
                shouldRestart = false;
            }
        }

        disableRawMode();
    }
};

int main() {
    TetrisGame game;
    game.run();
    return 0;
}