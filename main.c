/* ===========================================================================
   Trapped Knight -- psychedelic chessboard-leaper visualiser.

   Modes (all driven by scenes.cfg):

     trapped              -- one leaper greedily walks the spiral-numbered
                             board to the lowest unvisited reachable square,
                             until trapped (the knight: 2016 squares, A316667).
     competitive (2D)     -- armies take turns placing a piece on the lowest-
                             numbered square not attacked by an opposing army;
                             interlocking territories grow outward, ring by ring.
     competitive (3D)     -- the same in a 3D lattice (24-move 3D leapers),
                             revealed shell by shell as a growing glowing sphere.

   The walk / placement is computed once on the CPU; the result is drawn as
   instanced glowing quads on the GPU.

   Builds two ways from this one source:
     Desktop:  make                 -- GLFW + OpenGL 3.3 core + GLEW
     Web:      ./build_web.sh        -- Emscripten -> WebAssembly + WebGL2
   =========================================================================== */
#define _POSIX_C_SOURCE 200809L

#ifdef __EMSCRIPTEN__
  #include <emscripten.h>
  #include <emscripten/html5.h>
  #include <GLES3/gl3.h>
#else
  #include <GL/glew.h>
#endif
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <limits.h>
#include <time.h>
#include <sys/stat.h>

/* ===== leapers ============================================================ */
/* a leaper jumps so its move's sorted |components| equal {a,b,c}; the roster
   is loaded from leapers.cfg (or a built-in default set) */
#define MAX_LEAPERS 64
typedef struct { char name[24]; int a, b, c; } Leaper;
static Leaper gLeapers[MAX_LEAPERS];
static int    gLeaperN = 0;

static const float DEF_TINT[8][3] = {
    { 1.00, 0.25, 0.30 }, { 0.25, 0.55, 1.00 }, { 0.35, 1.00, 0.45 },
    { 1.00, 0.80, 0.20 }, { 0.85, 0.35, 1.00 }, { 0.20, 1.00, 0.95 },
    { 1.00, 0.50, 0.20 }, { 0.95, 0.95, 0.95 },
};

/* 2D leaper moves: all (+/-a,+/-b) and (+/-b,+/-a), de-duplicated */
static int gen_moves(int a, int b, int out[8][2])
{
    int cand[8][2] = {
        { a, b }, { a,-b }, {-a, b }, {-a,-b },
        { b, a }, { b,-a }, {-b, a }, {-b,-a },
    };
    int n = 0;
    for (int i = 0; i < 8; i++) {
        if (cand[i][0] == 0 && cand[i][1] == 0) continue;
        int dup = 0;
        for (int j = 0; j < n; j++)
            if (out[j][0] == cand[i][0] && out[j][1] == cand[i][1]) dup = 1;
        if (!dup) { out[n][0] = cand[i][0]; out[n][1] = cand[i][1]; n++; }
    }
    return n;
}

static void sort3(int *p)
{
    int t;
    if (p[0] > p[1]) { t = p[0]; p[0] = p[1]; p[1] = t; }
    if (p[1] > p[2]) { t = p[1]; p[1] = p[2]; p[2] = t; }
    if (p[0] > p[1]) { t = p[0]; p[0] = p[1]; p[1] = t; }
}

/* 3D leaper moves: every (dx,dy,dz) whose sorted |components| match the
   leaper's {a,b,c}. */
static int gen_moves_3d(int a, int b, int c, int out[48][3])
{
    int tgt[3] = { a, b, c };
    sort3(tgt);
    int hi = tgt[2], n = 0;
    for (int dx = -hi; dx <= hi; dx++)
    for (int dy = -hi; dy <= hi; dy++)
    for (int dz = -hi; dz <= hi; dz++) {
        int p[3] = { dx<0?-dx:dx, dy<0?-dy:dy, dz<0?-dz:dz };
        sort3(p);
        if (p[0] == tgt[0] && p[1] == tgt[1] && p[2] == tgt[2] && n < 48) {
            out[n][0] = dx; out[n][1] = dy; out[n][2] = dz; n++;
        }
    }
    return n;
}

/* ===== square spiral (2D) ================================================= */
static long spiral_index(int x, int y)
{
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int r = ax > ay ? ax : ay;
    if (r == 0) return 1;
    long s = (long)(2*r - 1) * (2*r - 1) + 1;
    if (x ==  r && y > -r) return s        + (y + r - 1);
    if (y ==  r && x <  r) return s + 2L*r + ((r - 1) - x);
    if (x == -r && y <  r) return s + 4L*r + ((r - 1) - y);
    return                        s + 6L*r + (x + r - 1);
}

typedef struct { short x, y; } SCell;
static SCell *gSpiral = NULL;
static int    gSpiralMax = 0;

static void build_spiral_table(int maxn)
{
    gSpiral = malloc(sizeof(SCell) * ((size_t)maxn + 1));
    if (!gSpiral) { fprintf(stderr, "out of memory (spiral)\n"); exit(1); }
    gSpiralMax = maxn;
    int dirs[4][2] = { { 1,0 }, { 0,1 }, { -1,0 }, { 0,-1 } };
    int x = 0, y = 0, n = 1, di = 0, run = 1, turns = 0;
    gSpiral[1].x = 0; gSpiral[1].y = 0;
    while (n < maxn) {
        for (int s = 0; s < run && n < maxn; s++) {
            x += dirs[di][0]; y += dirs[di][1];
            n++;
            gSpiral[n].x = (short)x; gSpiral[n].y = (short)y;
        }
        di = (di + 1) & 3;
        if (++turns == 2) { turns = 0; run++; }
    }
}

/* ===== shell-ordered 3D lattice =========================================== */
/* the web build uses smaller tables to keep memory and WebGL load modest */
#ifdef __EMSCRIPTEN__
  #define SHELL3D    70
  #define MAX_PIECE  250000
  #define SPIRAL_MAX 900000
#else
  #define SHELL3D    100
  #define MAX_PIECE  800000
  #define SPIRAL_MAX 2600000
#endif
typedef struct { short x, y, z; } Cell3;
static Cell3 *gCell3 = NULL;
static int    gCell3N = 0;

/* lattice cells ordered by squared distance from the origin, so competitive
   placement fills a BALL and the shell reveal grows as a sphere */
static void build_cell3_table(void)
{
    int M = SHELL3D, side = 2*M + 1;
    long total = (long)side * side * side;
    int  maxr2 = 3 * M * M;
    gCell3 = malloc(sizeof(Cell3) * (size_t)total);
    int *cur = calloc((size_t)(maxr2 + 2), sizeof(int));
    if (!gCell3 || !cur) { fprintf(stderr, "out of memory (cell3)\n"); exit(1); }

    for (int x = -M; x <= M; x++)
    for (int y = -M; y <= M; y++)
    for (int z = -M; z <= M; z++)
        cur[x*x + y*y + z*z + 1]++;                /* histogram of r^2 */
    for (int i = 1; i <= maxr2 + 1; i++) cur[i] += cur[i - 1];   /* prefix sum */
    for (int x = -M; x <= M; x++)
    for (int y = -M; y <= M; y++)
    for (int z = -M; z <= M; z++) {
        int i = cur[x*x + y*y + z*z]++;
        gCell3[i].x = (short)x; gCell3[i].y = (short)y; gCell3[i].z = (short)z;
    }
    gCell3N = (int)total;
    free(cur);
}

#define GRID_R    850
#define MOVE_CAP  120000

/* ===== trapped-knight walk ================================================ */
typedef struct { float x, y; } Pt;
static Pt   *gPath = NULL;
static int   gPathN = 0;
static long  gTrapSquare = 0;
static int   gHitCap = 0;

static void compute_path(int leaper)
{
    int mv[8][2];
    int nm = gen_moves(gLeapers[leaper].a, gLeapers[leaper].b, mv);
    int side = 2 * GRID_R + 1;
    unsigned char *vis = calloc((size_t)side * side, 1);
    Pt *path = malloc(sizeof(Pt) * MOVE_CAP);
    if (!vis || !path) { fprintf(stderr, "out of memory (walk)\n"); exit(1); }

    int cx = 0, cy = 0, n = 0;
    long lastIdx = 1;
#define VIS(X,Y) vis[(size_t)((Y) + GRID_R) * side + ((X) + GRID_R)]
    VIS(cx, cy) = 1;
    path[n].x = 0; path[n].y = 0; n++;
    for (;;) {
        long bestIdx = LONG_MAX;
        int  bx = 0, by = 0, found = 0;
        for (int m = 0; m < nm; m++) {
            int tx = cx + mv[m][0], ty = cy + mv[m][1];
            if (tx < -GRID_R+2 || tx > GRID_R-2 ||
                ty < -GRID_R+2 || ty > GRID_R-2) continue;
            if (VIS(tx, ty)) continue;
            long idx = spiral_index(tx, ty);
            if (idx < bestIdx) { bestIdx = idx; bx = tx; by = ty; found = 1; }
        }
        if (!found) break;
        cx = bx; cy = by;
        VIS(cx, cy) = 1;
        path[n].x = (float)cx; path[n].y = (float)cy;
        lastIdx = bestIdx;
        if (++n >= MOVE_CAP) break;
    }
#undef VIS
    free(vis);
    free(gPath);
    gPath = realloc(path, sizeof(Pt) * (size_t)(n > 0 ? n : 1));
    gPathN = n;
    gTrapSquare = lastIdx;
    gHitCap = (n >= MOVE_CAP);
    printf("[walk]  %-9s : %d squares, %s on spiral square %ld\n",
           gLeapers[leaper].name, n,
           gHitCap ? "stopped at move cap" : "trapped", gTrapSquare);
}

/* ===== competitive placement ============================================== */
typedef struct { float x, y, z; int army; } Piece;
static Piece *gPieces = NULL;
static int    gPieceN = 0;

/* 2D: armies place on the lowest-numbered empty square not attacked by an
   opposing army (allies are friendly). */
static void compute_competitive_2d(const int *armies, int K, int cap)
{
    if (K < 1) K = 1;
    if (K > 8) K = 8;
    if (cap < 1)         cap = 1;
    if (cap > MAX_PIECE) cap = MAX_PIECE;

    int mv[8][8][2], nm[8];
    for (int a = 0; a < K; a++)
        nm[a] = gen_moves(gLeapers[armies[a]].a, gLeapers[armies[a]].b, mv[a]);

    int side = 2 * GRID_R + 1;
    unsigned char *occ   = calloc((size_t)side * side, 1);
    unsigned char *amask = calloc((size_t)side * side, 1);
    Piece *pc = malloc(sizeof(Piece) * (size_t)cap);
    if (!occ || !amask || !pc) { fprintf(stderr, "out of memory (place)\n"); exit(1); }

    int cursor[8];
    for (int a = 0; a < 8; a++) cursor[a] = 1;
    int n = 0;
#define IDX(X,Y) ((size_t)((Y) + GRID_R) * side + ((X) + GRID_R))
    for (int p = 0; p < cap; p++) {
        int a = p % K, placed = 0;
        for (int s = cursor[a]; s <= gSpiralMax; s++) {
            int x = gSpiral[s].x, y = gSpiral[s].y;
            if (x < -GRID_R+8 || x > GRID_R-8 ||
                y < -GRID_R+8 || y > GRID_R-8) break;
            size_t i = IDX(x, y);
            if (occ[i]) continue;
            if (amask[i] & ~(1u << a)) continue;
            occ[i] = (unsigned char)(a + 1);
            for (int m = 0; m < nm[a]; m++) {
                int tx = x + mv[a][m][0], ty = y + mv[a][m][1];
                if (tx >= -GRID_R && tx <= GRID_R &&
                    ty >= -GRID_R && ty <= GRID_R)
                    amask[IDX(tx, ty)] |= (unsigned char)(1u << a);
            }
            pc[n].x = (float)x; pc[n].y = (float)y; pc[n].z = 0.0f;
            pc[n].army = a;
            n++;
            cursor[a] = s;
            placed = 1;
            break;
        }
        if (!placed) break;
    }
#undef IDX
    free(occ); free(amask);
    free(gPieces);
    gPieces = realloc(pc, sizeof(Piece) * (size_t)(n > 0 ? n : 1));
    gPieceN = n;
    printf("[place] 2D %d arm%s : %d pieces\n", K, K == 1 ? "y" : "ies", n);
}

/* 3D: the same rule on the shell-ordered 3D lattice with 24-move leapers. */
static void compute_competitive_3d(const int *armies, int K, int cap)
{
    if (K < 1) K = 1;
    if (K > 8) K = 8;
    if (cap < 1)         cap = 1;
    if (cap > MAX_PIECE) cap = MAX_PIECE;

    int mv[8][48][3], nm[8];
    for (int a = 0; a < K; a++)
        nm[a] = gen_moves_3d(gLeapers[armies[a]].a, gLeapers[armies[a]].b,
                             gLeapers[armies[a]].c, mv[a]);

    int M = SHELL3D, side = 2*M + 1;
    long vol = (long)side * side * side;
    unsigned char *occ   = calloc((size_t)vol, 1);
    unsigned char *amask = calloc((size_t)vol, 1);
    Piece *pc = malloc(sizeof(Piece) * (size_t)cap);
    if (!occ || !amask || !pc) { fprintf(stderr, "out of memory (place3d)\n"); exit(1); }

    int cursor[8];
    for (int a = 0; a < 8; a++) cursor[a] = 0;
    int n = 0;
#define I3(X,Y,Z) ((size_t)(((Z)+M)*(long)side + ((Y)+M)) * side + ((X)+M))
    for (int p = 0; p < cap; p++) {
        int a = p % K, placed = 0;
        for (int s = cursor[a]; s < gCell3N; s++) {
            int x = gCell3[s].x, y = gCell3[s].y, z = gCell3[s].z;
            size_t i = I3(x, y, z);
            if (occ[i]) continue;
            if (amask[i] & ~(1u << a)) continue;
            occ[i] = (unsigned char)(a + 1);
            for (int m = 0; m < nm[a]; m++) {
                int tx = x + mv[a][m][0], ty = y + mv[a][m][1], tz = z + mv[a][m][2];
                if (tx >= -M && tx <= M && ty >= -M && ty <= M &&
                    tz >= -M && tz <= M)
                    amask[I3(tx, ty, tz)] |= (unsigned char)(1u << a);
            }
            pc[n].x = (float)x; pc[n].y = (float)y; pc[n].z = (float)z;
            pc[n].army = a;
            n++;
            cursor[a] = s;
            placed = 1;
            break;
        }
        if (!placed) break;
    }
#undef I3
    free(occ); free(amask);
    free(gPieces);
    gPieces = realloc(pc, sizeof(Piece) * (size_t)(n > 0 ? n : 1));
    gPieceN = n;
    printf("[place] 3D %d arm%s : %d pieces\n", K, K == 1 ? "y" : "ies", n);
}

/* ===== shaders ============================================================ */
static const char *FS_VERT =
    "#version 330 core\n"
    "void main(){\n"
    "  vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));\n"
    "  gl_Position = vec4(p*2.0-1.0, 0.0, 1.0);\n"
    "}\n";

static GLuint gBgProg = 0, gPathProg = 0, gCloudProg = 0;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

#ifndef __EMSCRIPTEN__
static time_t file_mtime(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) ? st.st_mtime : 0;
}
#endif

#ifdef __EMSCRIPTEN__
/* rewrite a desktop GLSL 3.30 shader as WebGL2 (GLSL ES 3.00): swap the
   #version line and add the precision qualifiers ES requires */
static char *web_shader(const char *src)
{
    const char *body = src;
    if (strncmp(src, "#version 330 core", 17) == 0) {
        body = src + 17;
        while (*body == '\r' || *body == '\n') body++;
    }
    static const char *hdr =
        "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    size_t n = strlen(hdr) + strlen(body) + 1;
    char *out = malloc(n);
    snprintf(out, n, "%s%s", hdr, body);
    return out;
}
#endif

static GLuint compile_shader(GLenum type, const char *src)
{
#ifdef __EMSCRIPTEN__
    char *webbed = web_shader(src);
    const char *use = webbed;
#else
    const char *use = src;
#endif
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &use, NULL);
    glCompileShader(s);
#ifdef __EMSCRIPTEN__
    free(webbed);
#endif
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "[shader] %s compile error:\n%s\n",
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint make_program(const char *vsrc, const char *fsrc)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vsrc);
    if (!vs) return 0;
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fsrc);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "[shader] link error:\n%s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

static int reload_prog(GLuint *dst, const char *vfile, const char *ffile,
                       const char *vinline)
{
    char *v = vinline ? NULL : read_file(vfile);
    char *f = read_file(ffile);
    const char *vsrc = vinline ? vinline : v;
    int ok = 0;
    if (vsrc && f) {
        GLuint p = make_program(vsrc, f);
        if (p) { if (*dst) glDeleteProgram(*dst); *dst = p; ok = 1; }
    } else {
        fprintf(stderr, "[shader] cannot read %s / %s\n",
                vinline ? "(builtin)" : vfile, ffile);
    }
    free(v); free(f);
    return ok;
}

/* ===== scenes ============================================================= */
typedef enum { MODE_TRAPPED, MODE_COMPETITIVE } Mode;

typedef struct {
    char  name[48];
    Mode  mode;
    int   dim;                    /* 2 or 3 (competitive); trapped is 2 */
    int   leaper;
    int   armies[8], nArmies;
    float tint[8][3];
    int   pieceCap;
    float growth;                 /* shells/sec; 0 = fast default */
    int   palette;
} Scene;

static Scene gScenes[32];
static int   gSceneN = 0, gCurScene = 0;

static int leaper_by_name(const char *s)
{
    for (int i = 0; i < gLeaperN; i++)
        if (strcasecmp(s, gLeapers[i].name) == 0) return i;
    return -1;
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1]==' '||e[-1]=='\t'||e[-1]=='\r'||e[-1]=='\n')) *--e = 0;
    return s;
}

static void parse_hex(const char *s, float rgb[3])
{
    while (*s == ' ' || *s == '#') s++;
    unsigned v = 0;
    sscanf(s, "%x", &v);
    rgb[0] = ((v >> 16) & 255) / 255.0f;
    rgb[1] = ((v >>  8) & 255) / 255.0f;
    rgb[2] = ( v        & 255) / 255.0f;
}

static void scene_defaults(Scene *s)
{
    s->name[0] = 0;
    s->mode = MODE_TRAPPED;
    s->dim = 2;
    s->leaper = 0;
    s->nArmies = 0;
    s->pieceCap = 6000;
    s->growth = 0.0f;
    s->palette = 0;
    for (int a = 0; a < 8; a++) {
        s->armies[a] = 0;
        for (int c = 0; c < 3; c++) s->tint[a][c] = DEF_TINT[a][c];
    }
}

/* Parse leapers.cfg lines "name = a b c"; returns the leaper count.
   Returns 0 (roster left untouched) if the file is missing or has no valid
   entries -- there is NO built-in fallback. */
static int parse_leapers(void)
{
    char *txt = read_file("leapers.cfg");
    if (!txt) return 0;

    Leaper tmp[MAX_LEAPERS];
    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *name = trim(t), *val = trim(eq + 1);
        if (!*name || n >= MAX_LEAPERS) continue;
        for (char *p = val; *p; p++) if (*p == ',') *p = ' ';
        int a = 0, b = 0, c = 0;
        int got = sscanf(val, "%d %d %d", &a, &b, &c);
        if (got < 2) continue;
        if (got < 3) c = 0;
        snprintf(tmp[n].name, sizeof tmp[n].name, "%s", name);
        tmp[n].a = a; tmp[n].b = b; tmp[n].c = c;
        n++;
    }
    free(txt);
    if (n == 0) return 0;
    for (int i = 0; i < n; i++) gLeapers[i] = tmp[i];
    gLeaperN = n;
    return n;
}

static void parse_scenes(void)
{
    char *txt = read_file("scenes.cfg");
    if (!txt) {
        gSceneN = 2;
        scene_defaults(&gScenes[0]);
        snprintf(gScenes[0].name, sizeof gScenes[0].name, "Classic Knight");
        scene_defaults(&gScenes[1]);
        snprintf(gScenes[1].name, sizeof gScenes[1].name, "Red vs Black");
        gScenes[1].mode = MODE_COMPETITIVE;
        gScenes[1].nArmies = 2;
        gScenes[1].pieceCap = 5000;
        return;
    }
    gSceneN = 0;
    Scene *cur = NULL;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        if (*t == '[') {
            char *e = strchr(t, ']');
            if (e) *e = 0;
            if (gSceneN >= 32) break;
            cur = &gScenes[gSceneN++];
            scene_defaults(cur);
            snprintf(cur->name, sizeof cur->name, "%s", trim(t + 1));
            continue;
        }
        if (!cur) continue;
        char *eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = trim(t), *val = trim(eq + 1);
        if      (!strcasecmp(key, "mode"))
            cur->mode = !strcasecmp(val, "competitive") ? MODE_COMPETITIVE
                                                        : MODE_TRAPPED;
        else if (!strcasecmp(key, "dim"))     cur->dim      = atoi(val) == 3 ? 3 : 2;
        else if (!strcasecmp(key, "palette")) cur->palette  = atoi(val);
        else if (!strcasecmp(key, "pieces"))  cur->pieceCap = atoi(val);
        else if (!strcasecmp(key, "growth"))  cur->growth   = (float)atof(val);
        else if (!strcasecmp(key, "leaper")) {
            int l = leaper_by_name(val);
            if (l >= 0) cur->leaper = l;
        }
        else if (!strcasecmp(key, "armies")) {
            cur->nArmies = 0;
            char *s2 = NULL;
            for (char *tok = strtok_r(val, ",", &s2); tok && cur->nArmies < 8;
                 tok = strtok_r(NULL, ",", &s2)) {
                int l = leaper_by_name(trim(tok));
                if (l >= 0) cur->armies[cur->nArmies++] = l;
            }
        }
        else if (!strcasecmp(key, "tint")) {
            int k = 0;
            char *s2 = NULL;
            for (char *tok = strtok_r(val, ",", &s2); tok && k < 8;
                 tok = strtok_r(NULL, ",", &s2))
                parse_hex(trim(tok), cur->tint[k++]);
        }
    }
    free(txt);
    if (gSceneN == 0) {
        gSceneN = 1;
        scene_defaults(&gScenes[0]);
        snprintf(gScenes[0].name, sizeof gScenes[0].name, "Classic Knight");
    }
}

/* ===== GPU geometry ======================================================= */
static GLuint gBgVAO, gQuadVBO;
static GLuint gPathVAO, gPathVBO;       /* trapped: 5-float segments  */
static GLuint gCloudVAO, gCloudVBO;     /* competitive: pos.xyz+army+shell */
static int    gSegCount = 0;            /* trapped instance count */
static int    gMaxShell = 0;            /* competitive: largest shell index */
static Mode   gMode = MODE_TRAPPED;
static float  gTint[8][3];
static float  gMinX, gMinY, gMinZ, gMaxX, gMaxY, gMaxZ;

static void compute_bbox(void)
{
    gMinX = gMinY = gMinZ = -10.0f;
    gMaxX = gMaxY = gMaxZ =  10.0f;
    if (gMode == MODE_TRAPPED && gPathN > 0) {
        gMinX = gMaxX = gPath[0].x; gMinY = gMaxY = gPath[0].y;
        gMinZ = gMaxZ = 0.0f;
        for (int i = 1; i < gPathN; i++) {
            if (gPath[i].x < gMinX) gMinX = gPath[i].x;
            if (gPath[i].x > gMaxX) gMaxX = gPath[i].x;
            if (gPath[i].y < gMinY) gMinY = gPath[i].y;
            if (gPath[i].y > gMaxY) gMaxY = gPath[i].y;
        }
    } else if (gMode == MODE_COMPETITIVE && gPieceN > 0) {
        gMinX = gMaxX = gPieces[0].x; gMinY = gMaxY = gPieces[0].y;
        gMinZ = gMaxZ = gPieces[0].z;
        for (int i = 1; i < gPieceN; i++) {
            if (gPieces[i].x < gMinX) gMinX = gPieces[i].x;
            if (gPieces[i].x > gMaxX) gMaxX = gPieces[i].x;
            if (gPieces[i].y < gMinY) gMinY = gPieces[i].y;
            if (gPieces[i].y > gMaxY) gMaxY = gPieces[i].y;
            if (gPieces[i].z < gMinZ) gMinZ = gPieces[i].z;
            if (gPieces[i].z > gMaxZ) gMaxZ = gPieces[i].z;
        }
    }
}

static void build_instances(void)
{
    if (gMode == MODE_TRAPPED) {
        int count = gPathN - 1;
        if (count < 0) count = 0;
        gSegCount = count;
        if (count > 0) {
            float *buf = malloc(sizeof(float) * 5 * (size_t)count);
            for (int i = 0; i < count; i++) {
                buf[i*5+0] = gPath[i].x;   buf[i*5+1] = gPath[i].y;
                buf[i*5+2] = gPath[i+1].x; buf[i*5+3] = gPath[i+1].y;
                buf[i*5+4] = (float)i / (float)count;
            }
            glBindBuffer(GL_ARRAY_BUFFER, gPathVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 5 * (size_t)count,
                         buf, GL_DYNAMIC_DRAW);
            free(buf);
        }
    } else {
        int count = gPieceN;
        gMaxShell = 0;
        if (count > 0) {
            float *buf = malloc(sizeof(float) * 5 * (size_t)count);
            for (int i = 0; i < count; i++) {
                float x = gPieces[i].x, y = gPieces[i].y, z = gPieces[i].z;
                int sh = (int)(sqrtf(x*x + y*y + z*z) + 0.5f);   /* radial shell */
                if (sh > gMaxShell) gMaxShell = sh;
                buf[i*5+0] = x; buf[i*5+1] = y; buf[i*5+2] = z;
                buf[i*5+3] = (float)gPieces[i].army;
                buf[i*5+4] = (float)sh;
            }
            glBindBuffer(GL_ARRAY_BUFFER, gCloudVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 5 * (size_t)count,
                         buf, GL_DYNAMIC_DRAW);
            free(buf);
        }
    }
}

/* ===== camera + animation state =========================================== */
static int    winW = 1280, winH = 720;
static int    vsync = 1, shotReq = 0;
#ifndef __EMSCRIPTEN__
static int    fullscreen = 0, sx, sy, sw, sh;
#endif
static int    gPalette = 0;
static int    gPaused = 0, gCine = 1, gAutoCycle = 0, gTrail = 0, gStepMode = 0;
static int    gTangent = 0;                 /* 0 = billboard, 1 = surface tile */
static int    gDragging = 0;
static double gDragX = 0.0, gDragY = 0.0;
static double gTime = 0.0, gHoldTimer = 0.0;
static const double gHoldTime = 5.0;
static float  gCamX = 0, gCamY = 0, gCamZ = 0;
static float  gWorldH = 60.0f, gBaseWorldH = 60.0f, gRadius = 30.0f;
static float  gAz = 0.6f, gEl = 1.5708f, gBaseEl = 1.5708f;
static float  gHead = 0.0f;                 /* trapped: revealed instances */
static float  gReveal = 0.0f, gShellRate = 60.0f;  /* competitive: shells */
static int    gPieceCap = 6000;             /* competitive: piece count cap */
static float  gGlowR = 0.42f, gCoreW = 0.05f, gPointR = 0.55f;

/* ----- loop state (file-scope so the browser frame callback can see it) --- */
static GLFWwindow *gWin = NULL;
static double      gPrev = 0.0, gTitleAcc = 0.0;
static int         gFrames = 0;
static long        gTotalFrames = 0;
static int         gBench = 0, gShotMode = 0;
static float       gShotFrac = 1.0f;
static const char *gRend = NULL;
#ifndef __EMSCRIPTEN__
static time_t gMtBg, gMtPv, gMtPf, gMtCv, gMtCf, gMtSc, gMtLc;
#endif

static void fit_camera(void)
{
    gCamX = 0.5f * (gMinX + gMaxX);
    gCamY = 0.5f * (gMinY + gMaxY);
    gCamZ = 0.5f * (gMinZ + gMaxZ);
    float dx = gMaxX - gMinX, dy = gMaxY - gMinY, dz = gMaxZ - gMinZ;
    gRadius = 0.5f * sqrtf(dx*dx + dy*dy + dz*dz);
    float aspect = (float)winW / (float)winH;
    float need = 2.0f * gRadius * 1.16f;
    if (aspect < 1.0f) need /= aspect;
    gBaseWorldH = need + 4.0f;
    if (gBaseWorldH < 8.0f) gBaseWorldH = 8.0f;
    gWorldH = gBaseWorldH;
}

/* recompute the current competitive scene at the current piece cap */
static void rebuild_competitive(void)
{
    Scene *s = &gScenes[gCurScene];
    if (s->dim == 3) compute_competitive_3d(s->armies, s->nArmies, gPieceCap);
    else             compute_competitive_2d(s->armies, s->nArmies, gPieceCap);
    build_instances();
    compute_bbox();
    fit_camera();
}

static void apply_scene(int idx)
{
    if (gSceneN < 1) return;
    idx = (idx % gSceneN + gSceneN) % gSceneN;
    gCurScene = idx;
    Scene *s = &gScenes[idx];
    gMode = s->mode;
    gPalette = s->palette;

    if (gMode == MODE_TRAPPED) {
        compute_path(s->leaper);
        build_instances();
        gHead = (float)gSegCount;          /* the pattern, not the walk: instant */
        gGlowR = 0.42f; gCoreW = 0.05f;
        gBaseEl = 1.5708f;
    } else {
        if (s->nArmies < 1) { s->nArmies = 1; s->armies[0] = 0; }
        for (int a = 0; a < 8; a++)
            for (int c = 0; c < 3; c++) gTint[a][c] = s->tint[a][c];
        gPieceCap = s->pieceCap;
        rebuild_competitive();
        gReveal = 0.0f;
        gPointR = (s->dim == 3) ? 0.85f : 0.55f;
        gShellRate = (s->growth > 0.0f) ? s->growth
                                        : (float)(gMaxShell + 1) / 0.6f * 6.2749f;
        gBaseEl = (s->dim == 3) ? 0.42f : 1.5708f;
    }
    gEl = gBaseEl;
    compute_bbox();
    fit_camera();
    gHoldTimer = 0.0;
    gStepMode = 0;
    printf("[scene] %d/%d  \"%s\"\n", idx + 1, gSceneN, s->name);
}

/* ===== screenshot (desktop only) ========================================== */
static void save_screenshot(void)
{
#ifndef __EMSCRIPTEN__
    unsigned char *px = malloc((size_t)winW * winH * 3);
    if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, winW, winH, GL_RGB, GL_UNSIGNED_BYTE, px);
    char name[64];
    time_t now = time(NULL);
    strftime(name, sizeof name, "trapped_%Y%m%d_%H%M%S.ppm", localtime(&now));
    FILE *f = fopen(name, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", winW, winH);
        for (int y = winH - 1; y >= 0; y--)
            fwrite(px + (size_t)y * winW * 3, 1, (size_t)winW * 3, f);
        fclose(f);
        fprintf(stderr, "[shot] saved %s\n", name);
    }
    free(px);
#endif
}

static void print_help(void)
{
    puts(
    "\n  TRAPPED KNIGHT -- controls\n"
    "  ----------------------------------------------------\n"
    "   tab          next scene (from scenes.cfg)\n"
    "   space        pause / resume\n"
    "   r            restart the reveal\n"
    "   up / down    growth rate (shells per second)\n"
    "   , / .        step the reveal back / forward one shell\n"
    "   - / =        fewer / more pieces (sphere size)\n"
    "   c            cinematic camera (orbit + breathe)\n"
    "   o            auto-cycle scenes on / off\n"
    "   t            comet trail on / off (trapped mode)\n"
    "   m            piece style: billboard / surface-tile (3D)\n"
    "   left-drag    rotate the view (orbit in 3D)\n"
    "   w a s d      pan      z / x  zoom      b  fit\n"
    "   [ / ]        glow / piece size       1..5  palette\n"
    "   f  fullscreen      h  help      esc / q  quit\n"
    "  ----------------------------------------------------\n");
}

/* ===== callbacks ========================================================== */
static void on_error(int code, const char *desc)
{
    fprintf(stderr, "[glfw] error %d: %s\n", code, desc);
}

static void on_fbsize(GLFWwindow *win, int w, int h)
{
    (void)win;
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    fit_camera();
}

static void on_scroll(GLFWwindow *win, double dx, double dy)
{
    (void)win; (void)dx;
    gWorldH *= (dy > 0.0) ? 0.92f : 1.087f;
    if (gWorldH < 4.0f)    gWorldH = 4.0f;
    if (gWorldH > 8000.0f) gWorldH = 8000.0f;
    gBaseWorldH = gWorldH;
    gCine = 0;
}

static void on_key(GLFWwindow *win, int key, int sc, int action, int mods)
{
    (void)sc; (void)mods;
    if (action == GLFW_RELEASE) return;
    int tap = (action == GLFW_PRESS);
    float pan = gWorldH * 0.04f;

    switch (key) {
    case GLFW_KEY_ESCAPE:
    case GLFW_KEY_Q:     if (tap) glfwSetWindowShouldClose(win, 1);          break;
    case GLFW_KEY_SPACE: if (tap) gPaused ^= 1;                             break;
    case GLFW_KEY_R:     if (tap) { gReveal = 0.0f; gHoldTimer = 0.0;
                                    gStepMode = 0; }                       break;
    case GLFW_KEY_H:     if (tap) print_help();                             break;
    case GLFW_KEY_G:     if (tap) shotReq = 1;                              break;
    case GLFW_KEY_TAB:   if (tap) apply_scene(gCurScene + 1);               break;
    case GLFW_KEY_O:     if (tap) gAutoCycle ^= 1;                          break;
    case GLFW_KEY_T:     if (tap) gTrail ^= 1;                              break;
    case GLFW_KEY_M:     if (tap) gTangent ^= 1;                            break;
    case GLFW_KEY_C:     if (tap) { gCine ^= 1; if (gCine) fit_camera(); }  break;
    case GLFW_KEY_B:     if (tap) { fit_camera(); gCine = 1; }              break;
    case GLFW_KEY_V:     if (tap) { vsync ^= 1; glfwSwapInterval(vsync); }  break;
    case GLFW_KEY_F:     if (tap) {
#ifdef __EMSCRIPTEN__
            emscripten_request_fullscreen("#canvas", 1);
#else
            if (!fullscreen) {
                glfwGetWindowPos(win, &sx, &sy);
                glfwGetWindowSize(win, &sw, &sh);
                GLFWmonitor *m = glfwGetPrimaryMonitor();
                const GLFWvidmode *vm = glfwGetVideoMode(m);
                glfwSetWindowMonitor(win, m, 0, 0, vm->width, vm->height,
                                     vm->refreshRate);
                fullscreen = 1;
            } else {
                glfwSetWindowMonitor(win, NULL, sx, sy, sw, sh, 0);
                fullscreen = 0;
            }
#endif
        } break;
    case GLFW_KEY_1: if (tap) gPalette = 0; break;
    case GLFW_KEY_2: if (tap) gPalette = 1; break;
    case GLFW_KEY_3: if (tap) gPalette = 2; break;
    case GLFW_KEY_4: if (tap) gPalette = 3; break;
    case GLFW_KEY_5: if (tap) gPalette = 4; break;
    case GLFW_KEY_UP:    gShellRate *= 1.3f;  if (gShellRate > 1e5f) gShellRate = 1e5f; break;
    case GLFW_KEY_DOWN:  gShellRate /= 1.3f;  if (gShellRate < 0.5f) gShellRate = 0.5f; break;
    case GLFW_KEY_Z:     gWorldH *= 1.05f; gBaseWorldH = gWorldH; gCine = 0; break;
    case GLFW_KEY_X:     gWorldH *= 0.95f; gBaseWorldH = gWorldH; gCine = 0; break;
    case GLFW_KEY_W:     gCamY += pan; gCine = 0; break;
    case GLFW_KEY_S:     gCamY -= pan; gCine = 0; break;
    case GLFW_KEY_A:     gCamX -= pan; gCine = 0; break;
    case GLFW_KEY_D:     gCamX += pan; gCine = 0; break;
    case GLFW_KEY_LEFT_BRACKET:
        if (gMode == MODE_TRAPPED) { gGlowR *= 0.93f; if (gGlowR < 0.05f) gGlowR = 0.05f; }
        else                      { gPointR *= 0.93f; if (gPointR < 0.1f) gPointR = 0.1f; }
        break;
    case GLFW_KEY_RIGHT_BRACKET:
        if (gMode == MODE_TRAPPED) { gGlowR *= 1.075f; if (gGlowR > 4.0f) gGlowR = 4.0f; }
        else                      { gPointR *= 1.075f; if (gPointR > 3.0f) gPointR = 3.0f; }
        break;
    case GLFW_KEY_PERIOD:                    /* step the reveal +1 shell */
        if (gMode == MODE_COMPETITIVE) {
            gStepMode = 1;
            int shp = (int)(gReveal + 0.5f) + 1;
            if (shp > gMaxShell + 1) shp = gMaxShell + 1;
            gReveal = (float)shp;
        }
        break;
    case GLFW_KEY_COMMA:                     /* step the reveal -1 shell */
        if (gMode == MODE_COMPETITIVE) {
            gStepMode = 1;
            int shm = (int)(gReveal + 0.5f) - 1;
            if (shm < 0) shm = 0;
            gReveal = (float)shm;
        }
        break;
    case GLFW_KEY_EQUAL:                     /* more pieces -> bigger sphere */
        if (tap && gMode == MODE_COMPETITIVE) {
            gPieceCap = (int)(gPieceCap * 1.4f);
            if (gPieceCap > MAX_PIECE) gPieceCap = MAX_PIECE;
            rebuild_competitive();
        }
        break;
    case GLFW_KEY_MINUS:                     /* fewer pieces */
        if (tap && gMode == MODE_COMPETITIVE) {
            gPieceCap = (int)(gPieceCap / 1.4f);
            if (gPieceCap < 2000) gPieceCap = 2000;
            rebuild_competitive();
        }
        break;
    default: break;
    }
}

static void on_mousebutton(GLFWwindow *win, int button, int action, int mods)
{
    (void)mods;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    if (action == GLFW_PRESS) {
        gDragging = 1;
        glfwGetCursorPos(win, &gDragX, &gDragY);
        gCine = 0;                          /* manual control takes over */
    } else if (action == GLFW_RELEASE) {
        gDragging = 0;
    }
}

static void on_cursorpos(GLFWwindow *win, double x, double y)
{
    (void)win;
    if (!gDragging) return;
    float dx = (float)(x - gDragX), dy = (float)(y - gDragY);
    gDragX = x; gDragY = y;
    gAz -= dx * 0.006f;                     /* drag L/R -> spin / orbit */
    if (gMode == MODE_COMPETITIVE && gScenes[gCurScene].dim == 3) {
        gEl += dy * 0.006f;                 /* drag U/D -> elevation (3D) */
        if (gEl >  1.55f) gEl =  1.55f;
        if (gEl < -1.55f) gEl = -1.55f;
    }
}

/* ===== uniform helpers ==================================================== */
static void uf(GLuint p, const char *n, float v)
{ glUniform1f(glGetUniformLocation(p, n), v); }
static void ui(GLuint p, const char *n, int v)
{ glUniform1i(glGetUniformLocation(p, n), v); }
static void u2(GLuint p, const char *n, float a, float b)
{ glUniform2f(glGetUniformLocation(p, n), a, b); }
static void u3(GLuint p, const char *n, float a, float b, float c)
{ glUniform3f(glGetUniformLocation(p, n), a, b, c); }

/* ===== browser control hooks ============================================== */
#ifdef __EMSCRIPTEN__
/* the HTML control panel drives the app through these.  tk_key reuses the
   whole keyboard handler, so a button is just a synthetic key press. */
EMSCRIPTEN_KEEPALIVE void tk_key(int key) { on_key(gWin, key, 0, GLFW_PRESS, 0); }
EMSCRIPTEN_KEEPALIVE void tk_scene(int i) { apply_scene(i); }
EMSCRIPTEN_KEEPALIVE void tk_resize(int w, int h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    fit_camera();
}
#endif

/* ===== per-frame body ===================================================== */
static void frame(void)
{
    double now = glfwGetTime();
    double dt  = now - gPrev;
    gPrev = now;
    int dim3 = (gMode == MODE_COMPETITIVE && gScenes[gCurScene].dim == 3);

    if (!gPaused) {
        gTime += dt;
        if (gMode == MODE_COMPETITIVE && !gStepMode &&
            gReveal <= (float)gMaxShell + 1.0f)
            gReveal += gShellRate * (float)dt;
    }
    int revealed = (gMode == MODE_TRAPPED) || (gReveal > (float)gMaxShell);
    if (revealed && !gPaused && !gStepMode) {
        gHoldTimer += dt;
        if (gAutoCycle && gHoldTimer > gHoldTime && gSceneN > 1)
            apply_scene(gCurScene + 1);
    }

#ifndef __EMSCRIPTEN__
    /* hot-reload of shaders / configs (desktop only -- web files are static) */
    time_t b  = file_mtime("bg.frag");
    time_t pv = file_mtime("path.vert"),  pf = file_mtime("path.frag");
    time_t cv = file_mtime("cloud.vert"), cf = file_mtime("cloud.frag");
    time_t scf = file_mtime("scenes.cfg"), lcf = file_mtime("leapers.cfg");
    if (b != gMtBg) { gMtBg = b;
        if (reload_prog(&gBgProg, NULL, "bg.frag", FS_VERT))
            fprintf(stderr, "[shader] bg reloaded\n"); }
    if (pv != gMtPv || pf != gMtPf) { gMtPv = pv; gMtPf = pf;
        if (reload_prog(&gPathProg, "path.vert", "path.frag", NULL))
            fprintf(stderr, "[shader] path reloaded\n"); }
    if (cv != gMtCv || cf != gMtCf) { gMtCv = cv; gMtCf = cf;
        if (reload_prog(&gCloudProg, "cloud.vert", "cloud.frag", NULL))
            fprintf(stderr, "[shader] cloud reloaded\n"); }
    if (scf != gMtSc || lcf != gMtLc) { gMtSc = scf; gMtLc = lcf;
        if (parse_leapers()) {
            parse_scenes();
            apply_scene(gCurScene);
            gHead = (float)gSegCount;
            gReveal = (float)gMaxShell + 2.0f;
            fprintf(stderr, "[config] reloaded\n");
        } else {
            fprintf(stderr, "[config] leapers.cfg unreadable -- kept previous\n");
        }
    }
#endif

    /* cinematic camera */
    if (gCine) {
        gAz += (float)dt * (dim3 ? 0.22f : 0.09f);
        gWorldH = gBaseWorldH * (1.0f + 0.06f * sinf((float)gTime * 0.20f));
        if (dim3)
            gEl = gBaseEl + 0.18f * sinf((float)gTime * 0.13f);
    }

    float trail = -1.0f;
    if (gTrail && gMode == MODE_TRAPPED)
        trail = (float)fmod(gTime * 0.16, 1.0);

    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_BLEND);
    glUseProgram(gBgProg);
    u2(gBgProg, "uRes", (float)winW, (float)winH);
    uf(gBgProg, "uTime", (float)gTime);
    glBindVertexArray(gBgVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glBlendEquation(dim3 ? GL_MAX : GL_FUNC_ADD);   /* MAX: no 3D whiteout */
    if (gMode == MODE_TRAPPED && gSegCount > 0) {
        glUseProgram(gPathProg);
        u2(gPathProg, "uCam", gCamX, gCamY);
        uf(gPathProg, "uScale", 2.0f / gWorldH);
        u2(gPathProg, "uAspect", (float)winH / (float)winW, 1.0f);
        uf(gPathProg, "uRot", gAz);
        uf(gPathProg, "uGlowR", gGlowR);
        uf(gPathProg, "uCoreW", gCoreW);
        uf(gPathProg, "uHead", gHead);
        uf(gPathProg, "uTime", (float)gTime);
        uf(gPathProg, "uTrail", trail);
        ui(gPathProg, "uPalette", gPalette);
        ui(gPathProg, "uMode", 0);
        glUniform3fv(glGetUniformLocation(gPathProg, "uTint"), 8, &gTint[0][0]);
        glBindVertexArray(gPathVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, gSegCount);
    } else if (gMode == MODE_COMPETITIVE && gPieceN > 0) {
        glUseProgram(gCloudProg);
        u3(gCloudProg, "uCenter", gCamX, gCamY, gCamZ);
        uf(gCloudProg, "uAz", gAz);
        uf(gCloudProg, "uEl", gEl);
        uf(gCloudProg, "uScale", 2.0f / gWorldH);
        u2(gCloudProg, "uAspect", (float)winH / (float)winW, 1.0f);
        uf(gCloudProg, "uPointR", gPointR);
        uf(gCloudProg, "uReveal", gReveal);
        uf(gCloudProg, "uRadius", gRadius);
        uf(gCloudProg, "uDim3D", dim3 ? 1.0f : 0.0f);
        uf(gCloudProg, "uTangent", (gTangent && dim3) ? 1.0f : 0.0f);
        glUniform3fv(glGetUniformLocation(gCloudProg, "uTint"), 8, &gTint[0][0]);
        glBindVertexArray(gCloudVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, gPieceN);
    }
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_BLEND);

    if (gShotMode && gTotalFrames == 3) shotReq = 1;
    if (shotReq) {
        save_screenshot();
        shotReq = 0;
        if (gShotMode) glfwSetWindowShouldClose(gWin, 1);
    }

    glfwSwapBuffers(gWin);
    glfwPollEvents();

    gFrames++;
    gTotalFrames++;
    gTitleAcc += dt;
    if (gTitleAcc >= 0.5) {
        int count = (gMode == MODE_TRAPPED) ? gSegCount : gPieceN;
        char rev[32] = "";
        if (gMode == MODE_COMPETITIVE) {
            int sh = (int)gReveal;
            if (sh > gMaxShell) sh = gMaxShell;
            snprintf(rev, sizeof rev, "  shell %d/%d", sh, gMaxShell);
        }
        char title[256];
        snprintf(title, sizeof title,
            "Trapped Knight  |  \"%s\" [%s%s]  |  %d items%s  |  %.0f fps%s%s",
            gScenes[gCurScene].name,
            gMode == MODE_COMPETITIVE ? "competitive" : "trapped",
            dim3 ? " 3D" : "", count, rev, gFrames / gTitleAcc,
            gStepMode ? "  STEP" : (gAutoCycle ? "  cycle" : ""),
            gPaused ? "  [PAUSED]" : "");
#ifdef __EMSCRIPTEN__
        EM_ASM({ if (window.tkStatus) tkStatus(UTF8ToString($0)); }, title);
#else
        glfwSetWindowTitle(gWin, title);
        if (gBench)
            fprintf(stderr, "[fps] %.0f  (%s)\n",
                    gFrames / gTitleAcc, gRend ? gRend : "?");
#endif
        gFrames = 0;
        gTitleAcc = 0.0;
    }
}

/* ===== main =============================================================== */
int main(void)
{
    gBench = (getenv("TK_BENCH") != NULL);
    const char *shotEnv = getenv("TK_SHOT");
    gShotMode = (shotEnv != NULL);
    gShotFrac = (shotEnv && atof(shotEnv) > 0.0 && atof(shotEnv) < 1.0)
                ? (float)atof(shotEnv) : 1.0f;
    int startScene = getenv("TK_SCENE") ? atoi(getenv("TK_SCENE")) : 0;
    if (getenv("TK_TILES")) gTangent = 1;

#ifndef __EMSCRIPTEN__
    {
        struct stat dxg;
        if (stat("/dev/dxg", &dxg) == 0) {
            setenv("GALLIUM_DRIVER", "d3d12", 0);
            setenv("MESA_LOADER_DRIVER_OVERRIDE", "d3d12", 0);
        }
    }
#endif

    glfwSetErrorCallback(on_error);
#ifdef GLFW_PLATFORM_X11
    if (!getenv("TK_WAYLAND"))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
    if (!glfwInit()) { fprintf(stderr, "glfwInit failed\n"); return 1; }

#ifdef __EMSCRIPTEN__
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    gWin = glfwCreateWindow(winW, winH, "Trapped Knight", NULL, NULL);
    if (!gWin) {
        fprintf(stderr, "window / GL context creation failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(gWin);
    if (gBench) vsync = 0;
    glfwSwapInterval(vsync);

#ifndef __EMSCRIPTEN__
    glewExperimental = GL_TRUE;
    GLenum ge = glewInit();
    if (ge != GLEW_OK) fprintf(stderr, "[glew] %s\n", glewGetErrorString(ge));
    while (glGetError() != GL_NO_ERROR) { }
#endif

    gRend = (const char *)glGetString(GL_RENDERER);
    printf("[gl] renderer: %s\n[gl] version : %s\n",
           gRend, (const char *)glGetString(GL_VERSION));

    glfwGetFramebufferSize(gWin, &winW, &winH);
    glViewport(0, 0, winW, winH);
    glfwSetFramebufferSizeCallback(gWin, on_fbsize);
    glfwSetKeyCallback(gWin, on_key);
    glfwSetScrollCallback(gWin, on_scroll);
    glfwSetMouseButtonCallback(gWin, on_mousebutton);
    glfwSetCursorPosCallback(gWin, on_cursorpos);

    const float quad[12] = { 0,-1,  1,-1,  1,1,   0,-1,  1,1,  0,1 };
    glGenVertexArrays(1, &gBgVAO);
    glGenBuffers(1, &gQuadVBO);
    glGenVertexArrays(1, &gPathVAO);
    glGenBuffers(1, &gPathVBO);
    glGenVertexArrays(1, &gCloudVAO);
    glGenBuffers(1, &gCloudVBO);

    glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);

    /* trapped path VAO: quad + (segment A, segment B, path param) */
    glBindVertexArray(gPathVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, gPathVBO);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float),
                          (void*)(2*sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, 5*sizeof(float),
                          (void*)(4*sizeof(float)));
    glVertexAttribDivisor(3, 1);

    /* competitive cloud VAO: quad + (piece pos.xyz, [army, shell]) */
    glBindVertexArray(gCloudVAO);
    glBindBuffer(GL_ARRAY_BUFFER, gQuadVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), (void*)0);
    glBindBuffer(GL_ARRAY_BUFFER, gCloudVBO);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5*sizeof(float), (void*)0);
    glVertexAttribDivisor(1, 1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 5*sizeof(float),
                          (void*)(3*sizeof(float)));
    glVertexAttribDivisor(2, 1);
    glBindVertexArray(0);

    if (!reload_prog(&gBgProg, NULL, "bg.frag", FS_VERT) ||
        !reload_prog(&gPathProg, "path.vert", "path.frag", NULL) ||
        !reload_prog(&gCloudProg, "cloud.vert", "cloud.frag", NULL)) {
        fprintf(stderr, "initial shader build failed -- exiting\n");
        glfwTerminate();
        return 1;
    }
#ifndef __EMSCRIPTEN__
    gMtBg = file_mtime("bg.frag");
    gMtPv = file_mtime("path.vert");  gMtPf = file_mtime("path.frag");
    gMtCv = file_mtime("cloud.vert"); gMtCf = file_mtime("cloud.frag");
    gMtSc = file_mtime("scenes.cfg"); gMtLc = file_mtime("leapers.cfg");
#endif

    build_spiral_table(SPIRAL_MAX);
    build_cell3_table();
    for (int a = 0; a < 8; a++)
        for (int c = 0; c < 3; c++) gTint[a][c] = DEF_TINT[a][c];
    if (!parse_leapers()) {
        fprintf(stderr, "error: leapers.cfg is missing or empty -- "
                        "the program needs it to define the pieces.\n");
        glfwTerminate();
        return 1;
    }
    parse_scenes();
    apply_scene(startScene);
    print_help();

#ifdef __EMSCRIPTEN__
    /* hand the scene list to the HTML control panel */
    for (int i = 0; i < gSceneN; i++)
        EM_ASM({ if (window.tkAddScene) tkAddScene($0, UTF8ToString($1)); },
               i, gScenes[i].name);
    EM_ASM({ if (window.tkReady) tkReady($0); }, gCurScene);
#endif

    if (gShotMode) {
        gHead = (float)gSegCount;
        gReveal = (gShotFrac >= 1.0f) ? (float)gMaxShell + 2.0f
                                      : (float)gMaxShell * gShotFrac;
        gTime = 20.0; gAz = 0.7f;
    }

    gPrev = glfwGetTime();

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, 1);
#else
    while (!glfwWindowShouldClose(gWin)) frame();
    free(gPath);
    free(gPieces);
    free(gSpiral);
    free(gCell3);
    glDeleteProgram(gBgProg);
    glDeleteProgram(gPathProg);
    glDeleteProgram(gCloudProg);
    glfwDestroyWindow(gWin);
    glfwTerminate();
#endif
    return 0;
}
