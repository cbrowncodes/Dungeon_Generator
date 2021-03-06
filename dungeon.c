#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "endian.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <limits.h>

#include "dungeon.h"
#include "utils.h"
#include "heap.h"
#include "move.h"

#include <unistd.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"
/*
static int point_in_room_p(room_t *r, pair_t p)
{
    return ((r->position[dim_x] <= p[dim_x]) &&
            ((r->position[dim_x] + r->size[dim_x]) > p[dim_x]) &&
            (r->position[dim_y] <= p[dim_y]) &&
            ((r->position[dim_y] + r->size[dim_y]) > p[dim_y]));
}
*/

typedef struct corridor_path {
    heap_node_t *hn;
    uint8_t pos[2];
    uint8_t from[2];
    /* Because paths can meander about the dungeon, they can be *
     * significantly longer than DUNGEON_X.                     */
    int32_t cost;
} corridor_path_t;

static int32_t corridor_path_cmp(const void *key, const void *with) {
    return ((corridor_path_t *) key)->cost - ((corridor_path_t *) with)->cost;
}

/* This is the same basic algorithm as in move.c, but *
 * subtle differences make it difficult to reuse.     */
static void dijkstra_corridor(dungeon_t *d, pair_t from, pair_t to)
{
    static corridor_path_t path[DUNGEON_Y][DUNGEON_X], *p;
    static uint32_t initialized = 0;
    heap_t h;
    uint32_t x, y;
    
    if (!initialized) {
        for (y = 0; y < DUNGEON_Y; y++) {
            for (x = 0; x < DUNGEON_X; x++) {
                path[y][x].pos[dim_y] = y;
                path[y][x].pos[dim_x] = x;
            }
        }
        initialized = 1;
    }
    
    for (y = 0; y < DUNGEON_Y; y++) {
        for (x = 0; x < DUNGEON_X; x++) {
            path[y][x].cost = INT_MAX;
        }
    }
    
    path[from[dim_y]][from[dim_x]].cost = 0;
    
    heap_init(&h, corridor_path_cmp, NULL);
    
    for (y = 0; y < DUNGEON_Y; y++) {
        for (x = 0; x < DUNGEON_X; x++) {
            if (mapxy(x, y) != ter_wall_immutable) {
                path[y][x].hn = heap_insert(&h, &path[y][x]);
            } else {
                path[y][x].hn = NULL;
            }
        }
    }
    
    while ((p = heap_remove_min(&h))) {
        p->hn = NULL;
        
        if ((p->pos[dim_y] == to[dim_y]) && p->pos[dim_x] == to[dim_x]) {
            for (x = to[dim_x], y = to[dim_y];
                 (x != from[dim_x]) || (y != from[dim_y]);
                 p = &path[y][x], x = p->from[dim_x], y = p->from[dim_y]) {
                if (mapxy(x, y) != ter_floor_room) {
                    mapxy(x, y) = ter_floor_hall;
                    hardnessxy(x, y) = 0;
                }
            }
            heap_delete(&h);
            return;
        }
        if ((path[p->pos[dim_y] - 1][p->pos[dim_x]    ].hn) &&
            (path[p->pos[dim_y] - 1][p->pos[dim_x]    ].cost >
             p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
             ((p->pos[dim_x] != from[dim_x]) ? 48  : 0))) {
                path[p->pos[dim_y] - 1][p->pos[dim_x]    ].cost =
                p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
                ((p->pos[dim_x] != from[dim_x]) ? 48  : 0);
                path[p->pos[dim_y] - 1][p->pos[dim_x]    ].from[dim_y] = p->pos[dim_y];
                path[p->pos[dim_y] - 1][p->pos[dim_x]    ].from[dim_x] = p->pos[dim_x];
                heap_decrease_key_no_replace(&h, path[p->pos[dim_y] - 1]
                                             [p->pos[dim_x]    ].hn);
            }
        if ((path[p->pos[dim_y]    ][p->pos[dim_x] - 1].hn) &&
            (path[p->pos[dim_y]    ][p->pos[dim_x] - 1].cost >
             p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
             ((p->pos[dim_y] != from[dim_y]) ? 48  : 0))) {
                path[p->pos[dim_y]    ][p->pos[dim_x] - 1].cost =
                p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
                ((p->pos[dim_y] != from[dim_y]) ? 48  : 0);
                path[p->pos[dim_y]    ][p->pos[dim_x] - 1].from[dim_y] = p->pos[dim_y];
                path[p->pos[dim_y]    ][p->pos[dim_x] - 1].from[dim_x] = p->pos[dim_x];
                heap_decrease_key_no_replace(&h, path[p->pos[dim_y]    ]
                                             [p->pos[dim_x] - 1].hn);
            }
        if ((path[p->pos[dim_y]    ][p->pos[dim_x] + 1].hn) &&
            (path[p->pos[dim_y]    ][p->pos[dim_x] + 1].cost >
             p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
             ((p->pos[dim_y] != from[dim_y]) ? 48  : 0))) {
                path[p->pos[dim_y]    ][p->pos[dim_x] + 1].cost =
                p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
                ((p->pos[dim_y] != from[dim_y]) ? 48  : 0);
                path[p->pos[dim_y]    ][p->pos[dim_x] + 1].from[dim_y] = p->pos[dim_y];
                path[p->pos[dim_y]    ][p->pos[dim_x] + 1].from[dim_x] = p->pos[dim_x];
                heap_decrease_key_no_replace(&h, path[p->pos[dim_y]    ]
                                             [p->pos[dim_x] + 1].hn);
            }
        if ((path[p->pos[dim_y] + 1][p->pos[dim_x]    ].hn) &&
            (path[p->pos[dim_y] + 1][p->pos[dim_x]    ].cost >
             p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
             ((p->pos[dim_x] != from[dim_x]) ? 48  : 0))) {
                path[p->pos[dim_y] + 1][p->pos[dim_x]    ].cost =
                p->cost + (hardnesspair(p->pos) ? hardnesspair(p->pos) : 8) +
                ((p->pos[dim_x] != from[dim_x]) ? 48  : 0);
                path[p->pos[dim_y] + 1][p->pos[dim_x]    ].from[dim_y] = p->pos[dim_y];
                path[p->pos[dim_y] + 1][p->pos[dim_x]    ].from[dim_x] = p->pos[dim_x];
                heap_decrease_key_no_replace(&h, path[p->pos[dim_y] + 1]
                                             [p->pos[dim_x]    ].hn);
            }
    }
}

/* Randomly decides whether to choose the x or the y direction first,   *
 * then draws the corridor connecting two points with one or two lines. */
static int connect_two_points(dungeon_t *d,
                              pair_t min_x, pair_t max_x,
                              pair_t min_y, pair_t max_y)
{
    uint32_t i;
    uint32_t x_first;
    
    x_first = rand() & 1;
    
    for (i = min_x[dim_x]; i <= max_x[dim_x]; i++) {
        if (mapxy(i, x_first ? min_y[dim_y] : max_y[dim_y]) == ter_wall) {
            mapxy(i, x_first ? min_y[dim_y] : max_y[dim_y]) = ter_floor_hall;
            hardnessxy(i, x_first ? min_y[dim_y] : max_y[dim_y]) = 0;
        }
    }
    for (i = min_y[dim_y]; i <= max_y[dim_y]; i++) {
        if (mapxy(x_first ? max_y[dim_x] : min_y[dim_x], i) == ter_wall) {
            mapxy(x_first ? max_y[dim_x] : min_y[dim_x], i) = ter_floor_hall;
            hardnessxy(x_first ? max_y[dim_x] : min_y[dim_x], i) = 0;
        }
    }
    
    return 0;
}

/* Recursively subdivides the space between two points until some minimum *
 * closeness is reached, then draws the corridors connecting them.        */
int connect_two_points_recursive(dungeon_t *d, pair_t e1, pair_t e2)
{
    pair_t mid;
    int16_t *min_x, *min_y, *max_x, *max_y;
    
    min_x = ((e1[dim_x] < e2[dim_x]) ? e1 : e2);
    max_x = ((min_x == e1) ? e2 : e1);
    min_y = ((e1[dim_y] < e2[dim_y]) ? e1 : e2);
    max_y = ((min_y == e1) ? e2 : e1);
    
    if ((max_x[dim_x] - min_x[dim_x]) +
        (max_y[dim_y] - min_y[dim_y]) < 15
        ) {
        return connect_two_points(d, min_x, max_x, min_y, max_y);
    }
    
    mid[dim_x] = rand_range(min_x[dim_x], max_x[dim_x]);
    mid[dim_y] = rand_range(min_y[dim_y], max_y[dim_y]);
    
    connect_two_points_recursive(d, e1, mid);
    connect_two_points_recursive(d, mid, e2);
    
    return 0;
}

/* Chooses a random point inside each room and connects them with a *
 * corridor.  Random internal points prevent corridors from exiting *
 * rooms in predictable locations.                                  */
static int connect_two_rooms(dungeon_t *d, room_t *r1, room_t *r2)
{
    pair_t e1, e2;
    
    e1[dim_y] = rand_range(r1->position[dim_y],
                           r1->position[dim_y] + r1->size[dim_y] - 1);
    e1[dim_x] = rand_range(r1->position[dim_x],
                           r1->position[dim_x] + r1->size[dim_x] - 1);
    e2[dim_y] = rand_range(r2->position[dim_y],
                           r2->position[dim_y] + r2->size[dim_y] - 1);
    e2[dim_x] = rand_range(r2->position[dim_x],
                           r2->position[dim_x] + r2->size[dim_x] - 1);
    
    /*  return connect_two_points_recursive(d, e1, e2);*/
    dijkstra_corridor(d, e1, e2);
    return 0;
}

static int connect_rooms(dungeon_t *d)
{
    uint32_t i;
    
    for (i = 1; i < d->num_rooms; i++) {
        connect_two_rooms(d, d->rooms + i - 1, d->rooms + i);
    }
    
    return 0;
}

int connect_rooms_old(dungeon_t *d)
{
    uint32_t i, j;
    uint32_t dist, dist_tmp;
    uint32_t connected;
    uint32_t r1, r2;
    uint32_t min_con, max_con;
    
    connected = 0;
    
    /* Find the closest pair and connect them.  Do this until everybody *
     * is connected to room zero.  Because of the nature of the path    *
     * drawing algorithm, it's possible for two paths to cross, or for  *
     * a path to touch another room, but what this produces is *almost* *
     * a free tree.                                                     */
    while (!connected) {
        dist = DUNGEON_X + DUNGEON_Y;
        for (i = 0; i < d->num_rooms; i++) {
            for (j = i + 1; j < d->num_rooms; j++) {
                if (d->rooms[i].connected != d->rooms[j].connected) {
                    dist_tmp = (abs(d->rooms[i].position[dim_x] -
                                    d->rooms[j].position[dim_x]) +
                                abs(d->rooms[i].position[dim_y] -
                                    d->rooms[j].position[dim_y]));
                    if (dist_tmp < dist) {
                        r1 = i;
                        r2 = j;
                        dist = dist_tmp;
                    }
                }
            }
        }
        connect_two_rooms(d, d->rooms + r1, d->rooms + r2);
        min_con = (d->rooms[r1].connected < d->rooms[r2].connected ?
                   d->rooms[r1].connected : d->rooms[r2].connected);
        max_con = (d->rooms[r1].connected > d->rooms[r2].connected ?
                   d->rooms[r1].connected : d->rooms[r2].connected);
        for (connected = 1, i = 1; i < d->num_rooms; i++) {
            if (d->rooms[i].connected == max_con) {
                d->rooms[i].connected = min_con;
            }
            if (d->rooms[i].connected) {
                connected = 0;
            }
        }
    }
    
    /* Now let's add a handful of random, extra connections to create *
     * loops and a more exciting look for the dungeon.  Use a do loop *
     * to guarantee that it always adds at least one such path.       */
    
    do {
        r1 = rand_range(0, d->num_rooms - 1);
        while ((r2 = rand_range(0, d->num_rooms - 1)) == r1)
            ;
        connect_two_rooms(d, d->rooms + r1, d->rooms + r2);
    } while (rand_under(1, 2));
    
    return 0;
}

static int place_room(dungeon_t *d, room_t *r)
{
    pair_t p;
    uint32_t tries;
    uint32_t qualified;
    
    for (;;) {
        for (tries = 0; tries < MAX_PLACEMENT_ATTEMPTS; tries++) {
            p[dim_x] = rand() % DUNGEON_X;
            p[dim_y] = rand() % DUNGEON_Y;
            if (p[dim_x] - ROOM_SEPARATION < 0                      ||
                p[dim_x] + r->size[dim_x] + ROOM_SEPARATION > DUNGEON_X ||
                p[dim_y] - ROOM_SEPARATION < 0                      ||
                p[dim_y] + r->size[dim_y] + ROOM_SEPARATION > DUNGEON_Y) {
                continue;
            }
            r->position[dim_x] = p[dim_x];
            r->position[dim_y] = p[dim_y];
            for (qualified = 1, p[dim_y] = r->position[dim_y] - ROOM_SEPARATION;
                 p[dim_y] < r->position[dim_y] + r->size[dim_y] + ROOM_SEPARATION;
                 p[dim_y]++) {
                for (p[dim_x] = r->position[dim_x] - ROOM_SEPARATION;
                     p[dim_x] < r->position[dim_x] + r->size[dim_x] + ROOM_SEPARATION;
                     p[dim_x]++) {
                    if (mappair(p) >= ter_floor) {
                        qualified = 0;
                    }
                }
            }
        }
        if (!qualified) {
            if (rand_under(1, 2)) {
                if (r->size[dim_x] > 8) {
                    r->size[dim_x]--;
                } else if (r->size[dim_y] > 5) {
                    r->size[dim_y]--;
                }
            } else {
                if (r->size[dim_y] > 8) {
                    r->size[dim_y]--;
                } else if (r->size[dim_x] > 5) {
                    r->size[dim_x]--;
                }
            }
        } else {
            for (p[dim_y] = r->position[dim_y];
                 p[dim_y] < r->position[dim_y] + r->size[dim_y];
                 p[dim_y]++) {
                for (p[dim_x] = r->position[dim_x];
                     p[dim_x] < r->position[dim_x] + r->size[dim_x];
                     p[dim_x]++) {
                    mappair(p) = ter_floor_room;
                    hardnesspair(p) = 0;
                }
            }
            for (p[dim_x] = r->position[dim_x] - 1;
                 p[dim_x] < r->position[dim_x] + r->size[dim_x] + 1;
                 p[dim_x]++) {
                mapxy(p[dim_x], r->position[dim_y] - 1)                    =
                mapxy(p[dim_x], r->position[dim_y] + r->size[dim_y] + 1) =
                ter_wall;
            }
            for (p[dim_y] = r->position[dim_y] - 1;
                 p[dim_y] < r->position[dim_y] + r->size[dim_y] + 1;
                 p[dim_y]++) {
                mapxy(r->position[dim_x] - 1, p[dim_y])                    =
                mapxy(r->position[dim_x] + r->size[dim_x] + 1, p[dim_y]) =
                ter_wall;
            }
            
            return 0;
        }
    }
    
    return 0;
}

static int place_rooms(dungeon_t *d)
{
    uint32_t i;
    
    for (i = 0; i < d->num_rooms; i++) {
        place_room(d, d->rooms + i);
    }
    
    return 0;
}

static int make_rooms(dungeon_t *d)
{
    uint32_t i;
    
    for (i = 0; i < MIN_ROOMS; i++) {
        d->rooms[i].exists = 1;
    }
    for (; i < MAX_ROOMS && rand_under(6, 8); i++) {
        d->rooms[i].exists = 1;
    }
    d->num_rooms = i;
    
    for (; i < MAX_ROOMS; i++) {
        d->rooms[i].exists = 0;
    }
    
    for (i = 0; i < d->num_rooms; i++) {
        d->rooms[i].size[dim_x] = ROOM_MIN_X;
        d->rooms[i].size[dim_y] = ROOM_MIN_Y;
        while (rand_under(7, 8)) {
            d->rooms[i].size[dim_x]++;
        }
        while (rand_under(3, 4)) {
            d->rooms[i].size[dim_y]++;
        }
        /* Initially, every room is connected only to itself. */
        d->rooms[i].connected = i;
    }
    
    return 0;
}

static int empty_dungeon(dungeon_t *d)
{
    uint8_t x, y;
    
    for (y = 0; y < DUNGEON_Y; y++) {
        for (x = 0; x < DUNGEON_X; x++) {
            mapxy(x, y) = ter_wall;
            hardnessxy(x, y) = rand_range(1, 254);
            if (y == 0 || y == DUNGEON_Y - 1 ||
                x == 0 || x == DUNGEON_X - 1) {
                mapxy(x, y) = ter_wall_immutable;
                hardnessxy(x, y) = 255;
            }
        }
    }
    
    return 0;
}

int gen_dungeon(dungeon_t *d)
{
    /*
     pair_t p1, p2;
     p1[dim_x] = rand_range(1, 158);
     p1[dim_y] = rand_range(1, 94);
     p2[dim_x] = rand_range(1, 158);
     p2[dim_y] = rand_range(1, 94);
     */
    
    empty_dungeon(d);
    
    /*
     connect_two_points_recursive(d, p1, p2);
     return 0;
     */
    
    make_rooms(d);
    place_rooms(d);
    connect_rooms(d);
    
    return 0;
}

void render_dungeon(dungeon_t *d)
{
    pair_t p;
    
    for (p[dim_y] = 0; p[dim_y] < DUNGEON_Y; p[dim_y]++) {
        for (p[dim_x] = 0; p[dim_x] < DUNGEON_X; p[dim_x]++) {
            switch (mappair(p)) {
                case ter_wall:
                case ter_wall_no_room:
                case ter_wall_no_floor:
                case ter_wall_immutable:
                    //putchar('#');
                    printf("%s#",ANSI_COLOR_RESET);
                    break;
                case ter_floor:
                case ter_floor_room:
                case ter_floor_hall:
                case ter_floor_tentative:
                    //putchar('.');
                    printf("%s.",ANSI_COLOR_RESET);
                    break;
                case ter_monster_dt:
                    //putchar('A');
                    printf("%sA",ANSI_COLOR_RED);
                    break;
                case ter_monster_dn:
                    //putchar('B');
                    printf("%sB",ANSI_COLOR_CYAN);
                    break;
                case ter_monster_et:
                    //putchar('C');
                    printf("%sC",ANSI_COLOR_GREEN);
                    break;
                case ter_monster_en:
                    //putchar('D');
                    printf("%sD",ANSI_COLOR_YELLOW);
                    break;
                case ter_player:
                    //putchar('@');
                    printf("%s@",ANSI_COLOR_BLUE);
                    break;
                case ter_debug:
                    //putchar('*');
                    printf("%s*",ANSI_COLOR_MAGENTA);
                    break;
            }
        }
        putchar('\n');
    }
}

static int write_dungeon_map(dungeon_t *d, FILE *f)
{
    uint32_t x, y;
    uint8_t zero = 0;
    uint8_t non_zero = 1;
    uint32_t found_error;
    
    for (found_error = 0, y = 0; y < DUNGEON_Y; y++) {
        for (x = 0; x < DUNGEON_X; x++) {
            /* First byte is zero if wall, non-zero if floor */
            switch (mapxy(x, y)) {
                case ter_debug:
                    if (!found_error) {
                        found_error = 1;
                        fprintf(stderr, "Bad cell in dungeon.  Output may be corrupt.\n");
                    }
                    /* Allow to fall through and write a wall cell. */
                case ter_wall:
                case ter_wall_no_room:
                case ter_wall_no_floor:
                case ter_wall_immutable:
                    fwrite(&zero, sizeof (zero), 1, f);
                    break;
                case ter_floor:
                case ter_floor_room:
                case ter_floor_hall:
                case ter_floor_tentative:
                    fwrite(&non_zero, sizeof (non_zero), 1, f);
                    break;
                default:
                    if (!found_error) {
                        found_error = 1;
                        fprintf(stderr, "Bad cell in dungeon.  Output may be corrupt.\n");
                    }
                    fwrite(&zero, sizeof (zero), 1, f);
            }
            
            /* Second byte is non-zero if room, zero otherwise */
            switch (mapxy(x, y)) {
                case ter_debug:
                case ter_wall:
                case ter_wall_no_room:
                case ter_wall_no_floor:
                case ter_wall_immutable:
                case ter_floor:
                    fwrite(&zero, sizeof (zero), 1, f);
                    break;
                case ter_floor_room:
                    fwrite(&non_zero, sizeof (non_zero), 1, f);
                    break;
                case ter_floor_hall:
                case ter_floor_tentative:
                default:
                    fwrite(&zero, sizeof (zero), 1, f);
            }
            
            /* Third byte is non-zero if a corridor, zero otherwise */
            switch (mapxy(x, y)) {
                case ter_debug:
                case ter_wall:
                case ter_wall_no_room:
                case ter_wall_no_floor:
                case ter_wall_immutable:
                case ter_floor:
                case ter_floor_room:
                    fwrite(&zero, sizeof (zero), 1, f);
                    break;
                case ter_floor_hall:
                    fwrite(&non_zero, sizeof (non_zero), 1, f);
                    break;
                case ter_floor_tentative:
                default:
                    fwrite(&zero, sizeof (zero), 1, f);
            }
            
            /* And the fourth byte is the hardness */
            fwrite(&hardnessxy(x, y), sizeof (hardnessxy(x, y)), 1, f);
            
        }
    }
    
    return 0;
}

int write_rooms(dungeon_t *d, FILE *f)
{
    uint32_t i;
    
    for (i = 0; i < d->num_rooms; i++) {
        /* write order is xpos, ypos, width, height */
        fwrite(&d->rooms[i].position[dim_x], 1, 1, f);
        fwrite(&d->rooms[i].position[dim_y], 1, 1, f);
        fwrite(&d->rooms[i].size[dim_x], 1, 1, f);
        fwrite(&d->rooms[i].size[dim_y], 1, 1, f);
    }
    
    return 0;
}

uint32_t calculate_dungeon_size(dungeon_t *d)
{
    return ((DUNGEON_X * DUNGEON_Y * 4 /* hard-coded value from the spec */)  +
            2 /* Also hard-coded from the spec; storage for the room count */ +
            (4 /* Also hard-coded, bytes per room */ * d->num_rooms));
}

int write_dungeon(dungeon_t *d)
{
    char *home;
    char *filename;
    FILE *f;
    size_t len;
    uint32_t be32;
    uint16_t be16;
    
    if (!(home = getenv("HOME"))) {
        fprintf(stderr, "\"HOME\" is undefined.  Using working directory.\n");
        home = ".";
    }
    
    len = (strlen(home) + strlen(SAVE_DIR) + strlen(DUNGEON_SAVE_FILE) +
           1 /* The NULL terminator */                                 +
           2 /* The slashes */);
    
    filename = malloc(len * sizeof (*filename));
    sprintf(filename, "%s/%s/", home, SAVE_DIR);
    makedirectory(filename);
    strcat(filename, DUNGEON_SAVE_FILE);
    
    if (!(f = fopen(filename, "w"))) {
        perror(filename);
        free(filename);
        
        return 1;
    }
    free(filename);
    
    /* The semantic, which is 6 bytes, 0-5 */
    fwrite(DUNGEON_SAVE_SEMANTIC, 1, strlen(DUNGEON_SAVE_SEMANTIC), f);
    
    /* The version, 4 bytes, 6-9 */
    be32 = htobe32(DUNGEON_SAVE_VERSION);
    fwrite(&be32, sizeof (be32), 1, f);
    
    /* The size of the rest of the file, 4 bytes, 10-13 */
    be32 = htobe32(calculate_dungeon_size(d));
    fwrite(&be32, sizeof (be32), 1, f);
    
    /* The dungeon map, 61440 bytes, 14-61453 */
    write_dungeon_map(d, f);
    
    /* The room count, 2 bytes, 61454-61455 */
    be16 = htobe16(d->num_rooms);
    fwrite(&be16, sizeof (be16), 1, f);
    
    /* And the rooms, be16 * 4 bytes, 61456-end */
    write_rooms(d, f);
    
    fclose(f);
    
    return 0;
}

static int read_dungeon_map(dungeon_t *d, FILE *f)
{
    uint32_t x, y;
    uint8_t open, room, corr;
    
    for (y = 0; y < DUNGEON_Y; y++) {
        for (x = 0; x < DUNGEON_X; x++) {
            fread(&open, sizeof (open), 1, f);
            fread(&room, sizeof (room), 1, f);
            fread(&corr, sizeof (corr), 1, f);
            fread(&hardnessxy(x, y), sizeof (hardnessxy(x, y)), 1, f);
            if (room) {
                mapxy(x, y) = ter_floor_room;
            } else if (corr) {
                mapxy(x, y) = ter_floor_hall;
            } else if (y == 0 || y == DUNGEON_Y - 1 ||
                       x == 0 || x == DUNGEON_X - 1) {
                mapxy(x, y) = ter_wall_immutable;
            } else {
                mapxy(x, y) = ter_wall;
            }
        }
    }
    
    return 0;
}

static int read_rooms(dungeon_t *d, FILE *f)
{
    uint32_t i;
    
    for (i = 0; i < d->num_rooms; i++) {
        fread(&d->rooms[i].position[dim_x], 1, 1, f);
        fread(&d->rooms[i].position[dim_y], 1, 1, f);
        fread(&d->rooms[i].size[dim_x], 1, 1, f);
        fread(&d->rooms[i].size[dim_y], 1, 1, f);
    }
    
    return 0;
}

int read_dungeon(dungeon_t *d, char *file)
{
    char semantic[6];
    uint32_t be32;
    uint16_t be16;
    FILE *f;
    char *home;
    size_t len;
    char *filename;
    struct stat buf;
    
    if (!file) {
        if (!(home = getenv("HOME"))) {
            fprintf(stderr, "\"HOME\" is undefined.  Using working directory.\n");
            home = ".";
        }
        
        len = (strlen(home) + strlen(SAVE_DIR) + strlen(DUNGEON_SAVE_FILE) +
               1 /* The NULL terminator */                                 +
               2 /* The slashes */);
        
        filename = malloc(len * sizeof (*filename));
        sprintf(filename, "%s/%s/%s", home, SAVE_DIR, DUNGEON_SAVE_FILE);
        
        if (!(f = fopen(filename, "r"))) {
            perror(filename);
            free(filename);
            exit(-1);
        }
        
        if (stat(filename, &buf)) {
            perror(filename);
            exit(-1);
        }
        
        free(filename);
    } else {
        if (!(f = fopen(file, "r"))) {
            perror(file);
            exit(-1);
        }
        if (stat(file, &buf)) {
            perror(file);
            exit(-1);
        }
        
    }
    
    d->num_rooms = 0;
    /* 16 is the 14 byte header + the two byte room count */
    if (buf.st_size < 16 + calculate_dungeon_size(d)) {
        fprintf(stderr, "Dungeon appears to be truncated.\n");
        exit(-1);
    }
    
    fread(semantic, sizeof(semantic), 1, f);
    if (strncmp(semantic, DUNGEON_SAVE_SEMANTIC, 6)) {
        fprintf(stderr, "Not an RLG229 save file.\n");
        exit(-1);
    }
    fread(&be32, sizeof (be32), 1, f);
    if (be32toh(be32) != 0) { /* Since we expect zero, be32toh() is a no-op. */
        fprintf(stderr, "File version mismatch.\n");
        exit(-1);
    }
    fread(&be32, sizeof (be32), 1, f);
    if (buf.st_size - 14 != be32toh(be32)) {
        fprintf(stderr, "File size mismatch.\n");
        exit(-1);
    }
    read_dungeon_map(d, f);
    fread(&be16, sizeof (be16), 1, f);
    d->num_rooms = be16toh(be16);
    if (buf.st_size != 14 + calculate_dungeon_size(d)) {
        fprintf(stderr, "Incorrect file size.\n");
        exit(-1);
    }
    read_rooms(d, f);
    
    fclose(f);
    
    return 0;
}

void delete_dungeon(dungeon_t *d)
{
}

void init_dungeon(dungeon_t *d)
{
    empty_dungeon(d);
}

void place_characters(dungeon_t *d, uint32_t monsters) {
    d->num_monsters = monsters + 1;
    uint32_t room; uint32_t i;
    room = rand_range(0, d->num_rooms - 1);
    room_spot(d, room);
    character_t c;
    c.room_number = room;
    c.current_pair[dim_x] = d->spots.x; c.current_pair[dim_y] = d->spots.y;
    c.previous_pair[dim_x] = d->spots.x; c.previous_pair[dim_y] = d->spots.y;
    c.speed = 10; c.intelligence = 4; c.cost = 10; c.player = 1;
    d->game_characters[d->character_count] = c;
    d->character_count++;
    d->map[d->spots.y][d->spots.x] = ter_player;
    for(i = 0; i < monsters; i++) {
        room = rand_range(0, d->num_rooms - 1);
        room_spot(d, room);
        character_t c;
        c.room_number = room;
        c.current_pair[dim_x] = d->spots.x;
        c.current_pair[dim_y] = d->spots.y;
        c.previous_pair[dim_x] = d->spots.x;
        c.previous_pair[dim_y] = d->spots.y;
        c.intelligence = rand_range(0, 3);
        c.speed = rand_range(5, 20);
        c.cost = c.speed;
        c.player = 0;
        d->game_characters[d->character_count] = c;
        d->character_count++;
        if(c.intelligence == 0) {
            d->map[c.current_pair[dim_y]][c.current_pair[dim_x]] = ter_monster_dt;
        } else if(c.intelligence == 1) {
            d->map[c.current_pair[dim_y]][c.current_pair[dim_x]] = ter_monster_dn;
        } else if(c.intelligence == 2) {
            d->map[c.current_pair[dim_y]][c.current_pair[dim_x]] = ter_monster_et;
        } else {
            d->map[c.current_pair[dim_y]][c.current_pair[dim_x]] = ter_monster_en;
        }
    }
}

void room_spot(dungeon_t *d, uint32_t r) {
    d->spots.x = rand_range(d->rooms[r].position[dim_x] + 1, d->rooms[r].position[dim_x] + d->rooms[r].size[dim_x]) - 1;
    d->spots.y = rand_range(d->rooms[r].position[dim_y] + 1, d->rooms[r].position[dim_y] + d->rooms[r].size[dim_y]) - 1;
}

int check_monsters(dungeon_t *d) {
    int i;
    for(i = 0; i < d->num_monsters; i++) {
        if(d->game_characters[i].alive == 0) {
            return 2;
        }
    }
    return 0;
}

int kill_character(dungeon_t *d, uint32_t x, uint32_t y) {
    int i;
    for(i = 0; i < d->num_monsters; i++) {
        if(d->game_characters[i].current_pair[dim_x] == x && d->game_characters[i].current_pair[dim_y] == y) {
            d->game_characters[i].current_pair[dim_x] = 0;
            d->game_characters[i].current_pair[dim_y] = 0;
            d->game_characters[i].alive = 1;
        }
    }
    return check_monsters(d);
}

int move_player(dungeon_t *d) {
    int valid; uint32_t random_x; uint32_t random_y; int win;
    valid = 0; win = 2;
    while(valid == 0) {
        uint32_t random_boo_x; uint32_t random_boo_y;
        random_boo_x = rand_range(1, 100); random_boo_y = rand_range(1, 100);
        uint32_t add_x; uint32_t add_y;
        add_x = rand_range(0, 1); add_y = rand_range(0, 1);
        if(add_x == 0 && add_y == 0) add_y = 1;
        if(random_boo_x % 2 == 0) {
            random_x = d->game_characters[0].current_pair[dim_x] + add_x;
        } else {
            random_x = d->game_characters[0].current_pair[dim_x] - add_x;
        }
        if(random_boo_y % 2 == 0) {
            random_y = d->game_characters[0].current_pair[dim_y] + add_y;
        } else {
            random_y = d->game_characters[0].current_pair[dim_y] - add_y;
        }
        if(d->map[random_y][random_x] > ter_wall_immutable) {
            valid = 1;
        } else if (d->map[random_y][random_x] > ter_floor_tentative) {
            win = kill_character(d, random_x, random_y);
            valid = 1;
        }
    }
    d->game_characters[0].previous_pair[dim_x] = d->game_characters[0].current_pair[dim_x];
    d->game_characters[0].previous_pair[dim_y] = d->game_characters[0].current_pair[dim_y];
    d->game_characters[0].current_pair[dim_x] = random_x;
    d->game_characters[0].current_pair[dim_y] = random_y;
    d->map[d->game_characters[0].previous_pair[dim_y]][d->game_characters[0].previous_pair[dim_x]] = ter_floor_tentative;
    d->map[random_y][random_x] = ter_player;
    sleep(3);
    return win;
}

spot_t euclidean(dungeon_t *d, pair_t from, pair_t to) {
    spot_t move;
    move.x = from[dim_x];
    move.y = from[dim_y];
    int tox; int toy; int fromx; int fromy;
    tox = to[dim_x]; toy = to[dim_y];
    fromx = from[dim_x]; fromy = from[dim_y];
    char comp;
    comp = 'p';
    if(tox > fromx) {
        comp = d->map[from[dim_y]][from[dim_x + 1]];
        if(comp == ter_floor || comp == ter_floor_room || comp == ter_floor_hall || comp == ter_floor_tentative || comp == ter_player) {
            move.x = from[dim_x] + 1;
            return move;
        }
    } else if(fromx > tox) {
        comp = d->map[from[dim_y]][from[dim_x - 1]];
        if(comp == ter_floor || comp == ter_floor_room || comp == ter_floor_hall || comp == ter_floor_tentative || comp == ter_player) {
            move.x = from[dim_x] - 1;
            return move;
        }
    } else if(fromy > toy) {
        comp = d->map[from[dim_y - 1]][from[dim_x]];
        if(comp == ter_floor || comp == ter_floor_room || comp == ter_floor_hall || comp == ter_floor_tentative || comp == ter_player) {
            move.y = from[dim_y] - 1;
            return move;
        }
    } else if(fromy < toy) {
        comp = d->map[from[dim_y - 1]][from[dim_x]];
        if(comp == ter_floor || comp == ter_floor_room || comp == ter_floor_hall || comp == ter_floor_tentative || comp == ter_player) {
            move.y = from[dim_y] + 1;
            return move;
        }
    }
    return move;
}

int move_monster_dt(dungeon_t *d, int c) {
    int win = 2;
    spot_t move; pair_t to; pair_t from;
    to[dim_x] = d->game_characters[0].current_pair[dim_x]; to[dim_y] = d->game_characters[0].current_pair[dim_y];
    from[dim_x] = d->game_characters[c].current_pair[dim_x]; from[dim_y] = d->game_characters[c].current_pair[dim_y];
    move = dijkstra(d, from, to);
    if(d->map[move.y][move.x] == ter_player) {
        win = 1;
    }
    d->map[d->game_characters[c].current_pair[dim_y]][d->game_characters[c].current_pair[dim_x]] = ter_floor_tentative;
    d->game_characters[c].previous_pair[dim_x] = d->game_characters[c].current_pair[dim_x];
    d->game_characters[c].previous_pair[dim_y] = d->game_characters[c].current_pair[dim_y];
    d->game_characters[c].current_pair[dim_x] = move.x;
    d->game_characters[c].current_pair[dim_y] = move.y;
    d->map[move.y][move.x] = ter_monster_dt;
    return win;
}

int move_monster_dn(dungeon_t *d, int c) {
    if(d->game_characters[c].room_number == d->game_characters[0].room_number) {
        int win = 2;
        spot_t move; pair_t to; pair_t from;
        to[dim_x] = d->game_characters[0].current_pair[dim_x]; to[dim_y] = d->game_characters[0].current_pair[dim_y];
        from[dim_x] = d->game_characters[c].current_pair[dim_x]; from[dim_y] = d->game_characters[c].current_pair[dim_y];
        move = dijkstra(d, from, to);
        if(d->map[move.y][move.x] == ter_player) {
            win = 1;
        }
        d->map[d->game_characters[c].current_pair[dim_y]][d->game_characters[c].current_pair[dim_x]] = ter_floor_tentative;
        d->game_characters[c].previous_pair[dim_x] = d->game_characters[c].current_pair[dim_x];
        d->game_characters[c].previous_pair[dim_y] = d->game_characters[c].current_pair[dim_y];
        d->game_characters[c].current_pair[dim_x] = move.x;
        d->game_characters[c].current_pair[dim_y] = move.y;
        d->map[move.y][move.x] = ter_monster_dn;
        return win;
    }
    return 2;
}

int move_monster_et(dungeon_t *d, int c) {
    int win = 2;
    spot_t move; pair_t to; pair_t from;
    to[dim_x] = d->game_characters[0].current_pair[dim_x]; to[dim_y] = d->game_characters[0].current_pair[dim_y];
    from[dim_x] = d->game_characters[c].current_pair[dim_x]; from[dim_y] = d->game_characters[c].current_pair[dim_y];
    move = euclidean(d, from, to);
    if(d->map[move.y][move.x] == ter_player) {
        win = 1;
    }
    d->map[d->game_characters[c].current_pair[dim_y]][d->game_characters[c].current_pair[dim_x]] = ter_floor_tentative;
    d->game_characters[c].previous_pair[dim_x] = d->game_characters[c].current_pair[dim_x];
    d->game_characters[c].previous_pair[dim_y] = d->game_characters[c].current_pair[dim_y];
    d->game_characters[c].current_pair[dim_x] = move.x;
    d->game_characters[c].current_pair[dim_y] = move.y;
    d->map[move.y][move.x] = ter_monster_et;
    return win;
}

int move_monster_en(dungeon_t *d, int c) {
    if(d->game_characters[c].room_number == d->game_characters[0].room_number) {
        int win = 2;
        spot_t move; pair_t to; pair_t from;
        to[dim_x] = d->game_characters[0].current_pair[dim_x]; to[dim_y] = d->game_characters[0].current_pair[dim_y];
        from[dim_x] = d->game_characters[c].current_pair[dim_x]; from[dim_y] = d->game_characters[c].current_pair[dim_y];
        move = euclidean(d, from, to);
        if(d->map[move.y][move.x] == ter_player) {
            win = 1;
        }
        d->map[d->game_characters[c].current_pair[dim_y]][d->game_characters[c].current_pair[dim_x]] = ter_floor_tentative;
        d->game_characters[c].previous_pair[dim_x] = d->game_characters[c].current_pair[dim_x];
        d->game_characters[c].previous_pair[dim_y] = d->game_characters[c].current_pair[dim_y];
        d->game_characters[c].current_pair[dim_x] = move.x;
        d->game_characters[c].current_pair[dim_y] = move.y;
        d->map[move.y][move.x] = ter_monster_en;
        return win;
    }
    return 2;
}

int simulate_moves(dungeon_t *d) {
    int win;
    int value = 0; int character = 0; int i;
    for(i = 0; i < d->num_monsters; i++) {
        if(d->game_characters[i].cost > value) {
            value = d->game_characters[i].cost;
            character = i;
        }
    }
    d->game_characters[character].cost--;
    if(value == 0) {
        int j;
        for(j = 0; j < d->num_monsters; j++) {
            d->game_characters[j].cost = d->game_characters[j].speed;
        }
        return 2;
    }
    if(d->game_characters[character].intelligence == 0) {
        win = move_monster_dt(d, character);
    } else if(d->game_characters[character].intelligence == 1) {
        win = move_monster_dn(d, character);
    } else if(d->game_characters[character].intelligence == 2) {
        win = move_monster_et(d, character);
    } else if(d->game_characters[character].intelligence == 3) {
        win = move_monster_en(d, character);
    } else {
        win = move_player(d);
    }
    return win;
}