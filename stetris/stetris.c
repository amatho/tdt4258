#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// The game state can be used to detect what happens on the playfield
#define GAMEOVER 0
#define ACTIVE (1 << 0)
#define ROW_CLEAR (1 << 1)
#define TILE_ADDED (1 << 2)

#define FRAME_BUFFER_SIZE 64

// Each pixel in the frame buffer is 16 bits (RGB565)
typedef __u16 fb_pixel_t;

// If you extend this structure, either avoid pointers or adjust
// the game logic allocate/deallocate and reset the memory
typedef struct {
    bool occupied;
    // The RGB565 color value of the tile
    fb_pixel_t color;
} tile;

typedef struct {
    unsigned int x;
    unsigned int y;
} coord;

typedef struct {
    coord const grid;                     // playfield bounds
    unsigned long const uSecTickTime;     // tick rate
    unsigned long const rowsPerLevel;     // speed up after clearing rows
    unsigned long const initNextGameTick; // initial value of nextGameTick

    unsigned int tiles; // number of tiles played
    unsigned int rows;  // number of rows cleared
    unsigned int score; // game score
    unsigned int level; // game level

    tile *rawPlayfield; // pointer to raw memory of the playfield
    tile **playfield;   // This is the play field array
    unsigned int state;
    coord activeTile; // current tile

    unsigned long tick;         // incremeted at tickrate, wraps at nextGameTick
                                // when reached 0, next game state calculated
    unsigned long nextGameTick; // sets when tick is wrapping back to zero
                                // lowers with increasing level, never reaches 0
} gameConfig;

// Sense HAT initialization state
typedef struct {
    // File descriptor of the joystick input
    int joy_fd;
    // File descriptor of the LED frame buffer
    int fb_fd;
    // Fixed screen info of the LED frame buffer
    struct fb_fix_screeninfo fb_fix_info;
    // Variable screen info of the LED frame buffer
    struct fb_var_screeninfo fb_var_info;
    // The memory mapped frame buffer
    fb_pixel_t *led_fb;
} sense_hat_t;

gameConfig game = {
    .grid = {8, 8},
    .uSecTickTime = 10000,
    .rowsPerLevel = 2,
    .initNextGameTick = 50,
};

sense_hat_t SENSE_HAT;

// A table of RGB565 values to use for the tiles
fb_pixel_t tile_color_table[] = {0xF800, 0xFBE0, 0xFFE0, 0x7E0,
                                 0x7FF,  0x1F,   0xF81F};
// Macro for calculating the size of the color table
#define TILE_COLOR_TABLE_SIZE (sizeof(tile_color_table) / sizeof(fb_pixel_t))
// A wrapping index into the color table
unsigned long tile_color_index = 0;

// Find and open the file for the joystick input
int open_joystick() {
    DIR *input_dir = opendir("/dev/input");
    struct dirent *entry;

    // Walk through all input devices
    while ((entry = readdir(input_dir))) {
        // Open the input device
        int fd = openat(dirfd(input_dir), entry->d_name, O_RDONLY);

        // Continue if the device could not be opened
        if (fd < 0) {
            continue;
        }

        // Check if the device is the Sense HAT joystick, in that case return
        // the file descriptor
        char name[32];
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            if (strncmp(name, "Raspberry Pi Sense HAT Joystick", 31) == 0) {
                return fd;
            }
        }

        // Close the input before the next iteration
        close(fd);
    }

    return -1;
}

// Filter function that accepts entries that begin with "/dev/fb"
int frame_buffer_dir_filter(const struct dirent *e) {
    return strcmp(e->d_name, "/dev/fb") >= 0;
}

// Find and open the LED frame buffer
int open_frame_buffer() {
    // Walk the /dev directory using the filter function above, and sorting by
    // name
    int dev_dir = open("/dev", O_RDONLY);
    struct dirent **namelist;
    int n = scandirat(dev_dir, ".", &namelist, &frame_buffer_dir_filter,
                      &alphasort);

    // This should never happen, but return -1 if /dev is empty
    if (n == -1) {
        return -1;
    }

    // Loop through the filtered entries in /dev and find the LED frame buffer
    int fd;
    while (n--) {
        // Open a frame buffer
        fd = openat(dev_dir, namelist[n]->d_name, O_RDWR);

        // Continue if the frame buffer could not be opened
        if (fd < 0) {
            continue;
        }

        // Get the fixed screen info in order to check the identificaiton of the
        // frame buffer, and check if it is the frame buffer we are looking for
        struct fb_fix_screeninfo info;
        if (ioctl(fd, FBIOGET_FSCREENINFO, &info) >= 0) {
            if (strncmp(info.id, "RPi-Sense FB", 12) == 0) {
                break;
            }
        }

        // Close the frame buffer before the next iteration
        close(fd);
    }

    // Remember to free up the namelist allocated by scandir
    free(namelist);
    return fd;
}

// This function is called on the start of your application
// Here you can initialize what ever you need for your task
// return false if something fails, else true
bool initializeSenseHat() {
    int joy_fd = open_joystick();
    if (joy_fd < 0) {
        fprintf(stderr, "could not find the joystick\n");
        return false;
    }

    SENSE_HAT.joy_fd = joy_fd;

    int fb_fd = open_frame_buffer();
    if (fb_fd < 0) {
        fprintf(stderr, "could not find the LED frame buffer\n");
        return false;
    }

    SENSE_HAT.fb_fd = fb_fd;

    if (ioctl(SENSE_HAT.fb_fd, FBIOGET_FSCREENINFO, &SENSE_HAT.fb_fix_info) <
        0) {
        fprintf(stderr, "could not get frame buffer fixed screen info\n");
        return false;
    }

    if (ioctl(SENSE_HAT.fb_fd, FBIOGET_VSCREENINFO, &SENSE_HAT.fb_var_info) <
        0) {
        fprintf(stderr, "could not get frame buffer variable screen info\n");
        return false;
    }

    if (SENSE_HAT.fb_var_info.bits_per_pixel != 16) {
        fprintf(stderr, "frame buffer has invalid bits per pixel\n");
        return false;
    }

    // Memory map the LED frame buffer with read, write, and shared access
    SENSE_HAT.led_fb =
        mmap(0, SENSE_HAT.fb_fix_info.smem_len, PROT_READ | PROT_WRITE,
             MAP_SHARED, SENSE_HAT.fb_fd, 0);

    return true;
}

// This function is called when the application exits
// Here you can free up everything that you might have opened/allocated
void freeSenseHat() {
    munmap(SENSE_HAT.led_fb, SENSE_HAT.fb_fix_info.smem_len);
    close(SENSE_HAT.joy_fd);
    close(SENSE_HAT.fb_fd);
}

// This function should return the key that corresponds to the joystick press
// KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, with the respective direction
// and KEY_ENTER, when the the joystick is pressed
// !!! when nothing was pressed you MUST return 0 !!!
int readSenseHatJoystick() {
    // Poll the joystick input, and check for available events
    struct pollfd fds = {.fd = SENSE_HAT.joy_fd, .events = POLLIN};
    int ev_len = poll(&fds, 1, 0);

    // Check for poll errors
    int key = 0;
    if (ev_len < 0) {
        fprintf(stderr, "joystick poll returned an error");
    } else if (ev_len == 0) {
        return key;
    }

    // Read ev_len input events from the joystick
    struct input_event events[ev_len];
    read(SENSE_HAT.joy_fd, events,
         sizeof(struct input_event) * (unsigned int)ev_len);

    // Check all events for key presses (not key release)
    for (int i = 0; i < ev_len; i++) {
        struct input_event ev = events[i];
        if (ev.type == EV_KEY && ev.value == 1) {
            key = ev.code;
        }
    }

    return key;
}

// This function should render the gamefield on the LED matrix. It is called
// every game tick. The parameter playfieldChanged signals whether the game
// logic has changed the playfield
void renderSenseHatMatrix(bool const playfieldChanged) {
    // No need to update the LEDs if nothing has changed
    if (!playfieldChanged) {
        return;
    }

    // Loop through all tiles and update the corresponding pixel in the frame
    // buffer
    for (unsigned long j = 0; j < game.grid.y; j++) {
        for (unsigned long i = 0; i < game.grid.x; i++) {
            // The frame buffer stores the pixels in a packed format, i.e. a
            // flat array
            SENSE_HAT.led_fb[(j * game.grid.y) + i] =
                game.playfield[j][i].color;
        }
    }
}

// The game logic uses only the following functions to interact with the
// playfield. if you choose to change the playfield or the tile structure, you
// might need to adjust this game logic <> playfield interface

static inline void newTile(coord const target) {
    game.playfield[target.y][target.x].occupied = true;
    // Set the new tile's color to be one of the colors in the table
    game.playfield[target.y][target.x].color =
        tile_color_table[tile_color_index];
    // Update the color table index and make sure to wrap around if it exceeds
    // the length of the table
    tile_color_index = (tile_color_index + 1) % TILE_COLOR_TABLE_SIZE;
}

static inline void copyTile(coord const to, coord const from) {
    memcpy((void *)&game.playfield[to.y][to.x],
           (void *)&game.playfield[from.y][from.x], sizeof(tile));
}

static inline void copyRow(unsigned int const to, unsigned int const from) {
    memcpy((void *)&game.playfield[to][0], (void *)&game.playfield[from][0],
           sizeof(tile) * game.grid.x);
}

static inline void resetTile(coord const target) {
    memset((void *)&game.playfield[target.y][target.x], 0, sizeof(tile));
}

static inline void resetRow(unsigned int const target) {
    memset((void *)&game.playfield[target][0], 0, sizeof(tile) * game.grid.x);
}

static inline bool tileOccupied(coord const target) {
    return game.playfield[target.y][target.x].occupied;
}

static inline bool rowOccupied(unsigned int const target) {
    for (unsigned int x = 0; x < game.grid.x; x++) {
        coord const checkTile = {x, target};
        if (!tileOccupied(checkTile)) {
            return false;
        }
    }
    return true;
}

static inline void resetPlayfield() {
    for (unsigned int y = 0; y < game.grid.y; y++) {
        resetRow(y);
    }
}

// Below here comes the game logic. Keep in mind: You are not allowed to change
// how the game works! that means no changes are necessary below this line! And
// if you choose to change something keep it compatible with what was provided
// to you!

bool addNewTile() {
    game.activeTile.y = 0;
    game.activeTile.x = (game.grid.x - 1) / 2;
    if (tileOccupied(game.activeTile))
        return false;
    newTile(game.activeTile);
    return true;
}

bool moveRight() {
    coord const newTile = {game.activeTile.x + 1, game.activeTile.y};
    if (game.activeTile.x < (game.grid.x - 1) && !tileOccupied(newTile)) {
        copyTile(newTile, game.activeTile);
        resetTile(game.activeTile);
        game.activeTile = newTile;
        return true;
    }
    return false;
}

bool moveLeft() {
    coord const newTile = {game.activeTile.x - 1, game.activeTile.y};
    if (game.activeTile.x > 0 && !tileOccupied(newTile)) {
        copyTile(newTile, game.activeTile);
        resetTile(game.activeTile);
        game.activeTile = newTile;
        return true;
    }
    return false;
}

bool moveDown() {
    coord const newTile = {game.activeTile.x, game.activeTile.y + 1};
    if (game.activeTile.y < (game.grid.y - 1) && !tileOccupied(newTile)) {
        copyTile(newTile, game.activeTile);
        resetTile(game.activeTile);
        game.activeTile = newTile;
        return true;
    }
    return false;
}

bool clearRow() {
    if (rowOccupied(game.grid.y - 1)) {
        for (unsigned int y = game.grid.y - 1; y > 0; y--) {
            copyRow(y, y - 1);
        }
        resetRow(0);
        return true;
    }
    return false;
}

void advanceLevel() {
    game.level++;

    // Rewritten to fix errors about GNU case range extension when using clang
    if (game.nextGameTick >= 2 && game.nextGameTick <= 10)
        game.nextGameTick--;
    else if (game.nextGameTick >= 11 && game.nextGameTick <= 20)
        game.nextGameTick -= 2;
    else if (game.nextGameTick == 0 || game.nextGameTick > 20)
        game.nextGameTick -= 10;
}

void newGame() {
    game.state = ACTIVE;
    game.tiles = 0;
    game.rows = 0;
    game.score = 0;
    game.tick = 0;
    game.level = 0;
    resetPlayfield();
}

void gameOver() {
    game.state = GAMEOVER;
    game.nextGameTick = game.initNextGameTick;
}

bool sTetris(int const key) {
    bool playfieldChanged = false;

    if (game.state & ACTIVE) {
        // Move the current tile
        if (key) {
            playfieldChanged = true;
            switch (key) {
            case KEY_LEFT:
                moveLeft();
                break;
            case KEY_RIGHT:
                moveRight();
                break;
            case KEY_DOWN:
                while (moveDown()) {
                };
                game.tick = 0;
                break;
            default:
                playfieldChanged = false;
            }
        }

        // If we have reached a tick to update the game
        if (game.tick == 0) {
            // We communicate the row clear and tile add over the game state
            // clear these bits if they were set before
            game.state &= (unsigned int)~(ROW_CLEAR | TILE_ADDED);

            playfieldChanged = true;
            // Clear row if possible
            if (clearRow()) {
                game.state |= ROW_CLEAR;
                game.rows++;
                game.score += game.level + 1;
                if ((game.rows % game.rowsPerLevel) == 0) {
                    advanceLevel();
                }
            }

            // if there is no current tile or we cannot move it down,
            // add a new one. If not possible, game over.
            if (!tileOccupied(game.activeTile) || !moveDown()) {
                if (addNewTile()) {
                    game.state |= TILE_ADDED;
                    game.tiles++;
                } else {
                    gameOver();
                }
            }
        }
    }

    // Press any key to start a new game
    if ((game.state == GAMEOVER) && key) {
        playfieldChanged = true;
        newGame();
        addNewTile();
        game.state |= TILE_ADDED;
        game.tiles++;
    }

    return playfieldChanged;
}

int readKeyboard() {
    struct pollfd pollStdin = {.fd = STDIN_FILENO, .events = POLLIN};
    int lkey = 0;

    if (poll(&pollStdin, 1, 0)) {
        lkey = fgetc(stdin);
        if (lkey != 27)
            goto exit;
        lkey = fgetc(stdin);
        if (lkey != 91)
            goto exit;
        lkey = fgetc(stdin);
    }
exit:
    switch (lkey) {
    case 10:
        return KEY_ENTER;
    case 65:
        return KEY_UP;
    case 66:
        return KEY_DOWN;
    case 67:
        return KEY_RIGHT;
    case 68:
        return KEY_LEFT;
    }
    return 0;
}

void renderConsole(bool const playfieldChanged) {
    if (!playfieldChanged)
        return;

    // Goto beginning of console
    fprintf(stdout, "\033[%d;%dH", 0, 0);
    for (unsigned int x = 0; x < game.grid.x + 2; x++) {
        fprintf(stdout, "-");
    }
    fprintf(stdout, "\n");
    for (unsigned int y = 0; y < game.grid.y; y++) {
        fprintf(stdout, "|");
        for (unsigned int x = 0; x < game.grid.x; x++) {
            coord const checkTile = {x, y};
            fprintf(stdout, "%c", (tileOccupied(checkTile)) ? '#' : ' ');
        }
        switch (y) {
        case 0:
            fprintf(stdout, "| Tiles: %10u\n", game.tiles);
            break;
        case 1:
            fprintf(stdout, "| Rows:  %10u\n", game.rows);
            break;
        case 2:
            fprintf(stdout, "| Score: %10u\n", game.score);
            break;
        case 4:
            fprintf(stdout, "| Level: %10u\n", game.level);
            break;
        case 7:
            fprintf(stdout, "| %17s\n",
                    (game.state == GAMEOVER) ? "Game Over" : "");
            break;
        default:
            fprintf(stdout, "|\n");
        }
    }
    for (unsigned int x = 0; x < game.grid.x + 2; x++) {
        fprintf(stdout, "-");
    }
    fflush(stdout);
}

inline unsigned long uSecFromTimespec(struct timespec const ts) {
    return (unsigned long)((ts.tv_sec * 1000000) + (ts.tv_nsec / 1000));
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    // This sets the stdin in a special state where each
    // keyboard press is directly flushed to the stdin and additionally
    // not outputted to the stdout
    {
        struct termios ttystate;
        tcgetattr(STDIN_FILENO, &ttystate);
        ttystate.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
        ttystate.c_cc[VMIN] = 1;
        tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
    }

    // Allocate the playing field structure
    game.rawPlayfield =
        (tile *)malloc(game.grid.x * game.grid.y * sizeof(tile));
    game.playfield = (tile **)malloc(game.grid.y * sizeof(tile *));
    if (!game.playfield || !game.rawPlayfield) {
        fprintf(stderr, "ERROR: could not allocate playfield\n");
        return 1;
    }
    for (unsigned int y = 0; y < game.grid.y; y++) {
        game.playfield[y] = &(game.rawPlayfield[y * game.grid.x]);
    }

    // Reset playfield to make it empty
    resetPlayfield();
    // Start with gameOver
    gameOver();

    if (!initializeSenseHat()) {
        fprintf(stderr, "ERROR: could not initilize sense hat\n");
        return 1;
    };

    // Clear console, render first time
    fprintf(stdout, "\033[H\033[J");
    renderConsole(true);
    renderSenseHatMatrix(true);

    while (true) {
        struct timeval sTv, eTv;
        gettimeofday(&sTv, NULL);

        int key = readSenseHatJoystick();
        if (!key)
            key = readKeyboard();
        if (key == KEY_ENTER)
            break;

        bool playfieldChanged = sTetris(key);
        renderConsole(playfieldChanged);
        renderSenseHatMatrix(playfieldChanged);

        // Wait for next tick
        gettimeofday(&eTv, NULL);
        unsigned long const uSecProcessTime =
            (unsigned long)(((eTv.tv_sec * 1000000) + eTv.tv_usec) -
                            ((sTv.tv_sec * 1000000 + sTv.tv_usec)));
        if (uSecProcessTime < game.uSecTickTime) {
            usleep(game.uSecTickTime - uSecProcessTime);
        }
        game.tick = (game.tick + 1) % game.nextGameTick;
    }

    freeSenseHat();
    free(game.playfield);
    free(game.rawPlayfield);

    return 0;
}
