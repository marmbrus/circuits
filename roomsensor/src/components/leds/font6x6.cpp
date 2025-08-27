#include "font6x6.h"
#include "LEDStrip.h"

namespace leds { namespace font6x6 {

// Each glyph is defined by 6 lines of up to 6 characters.
// '*' = full pixel, '-' = dim pixel (use 1/4 intensity), ' ' = off
// We render inside an 8x8 cell with a one-pixel outer margin:
// draw rows at top_row + 1..6 and cols at left_col + 1..6

struct Glyph {
    const char* rows[6];
};

static inline void put_pixel(LEDStrip& s, size_t r, size_t c, uint8_t R, uint8_t G, uint8_t B, uint8_t W) {
    if (r >= s.rows() || c >= s.cols()) return;
    size_t idx = s.index_for_row_col(r, c);
    s.set_pixel(idx, R, G, B, W);
}

static inline void draw_row(LEDStrip& s, const char* pattern,
                            size_t row, size_t left, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    for (int i = 0; pattern[i] != '\0'; ++i) {
        char ch = pattern[i];
        if (ch == '*') {
            put_pixel(s, row, left + static_cast<size_t>(i), r, g, b, w);
        } else if (ch == '-') {
            put_pixel(s, row, left + static_cast<size_t>(i), r / 4u, g / 4u, b / 4u, w / 4u);
        }
    }
}

// Minimal subset seeded for digits '0'..'9' and ':' based on provided Go patterns.
// Extend this table with letters by transcribing the Go font lines here.
static const Glyph DIGITS[] = {
    // '0'
    {{"-****-","**--**","**--**","**--**","**--**","-****-"}},
    // '1' (with leading space offsets)
    {{" -**"," ***"," -**","  **","  **-"," ****"}},
    // '2'
    {{"-****-","*- -**","  -**-"," -**-"," -**-*","******"}},
    // '3'
    {{"-****-","**--**","   **-","   -**","**--**","-****-"}},
    // '4'
    {{" -***"," -*-**"," *--**-","******","  -**-","  ****"}},
    // '5'
    {{"******","**-","*****-","   -**","**--**","-****-"}},
    // '6'
    {{"-***","-**-","**-","*****-","**--**","-****-"}},
    // '7'
    {{"******","*- -**","   -**","  -**-","  **-","  **"}},
    // '8'
    {{"-****-","**--**","-****-","**--**","**--**","-****-"}},
    // '9'
    {{"-****-","**--**","-*****","   -**","  -**-"," -***-"}},
};

static const Glyph COLON = {{" **-"," -**","",""," **-"," -**"}};

// Lookup table for letters and punctuation, transcribed from the Go font
static const Glyph* get_letter_glyph(char ch) {
    // Uppercase
    static const Glyph GA = {{" -**-","-****-","**--**","******","**--**","**  **"}};
    static const Glyph GB = {{"*****-","-**-**"," ****-"," ** **","-** **","*****-"}};
    static const Glyph GC = {{"-****-","**- -*","**","**","**- -*","-****-"}};
    static const Glyph GD = {{"****-","**-**-","** -**","** -**","**-**-","****-"}};
    static const Glyph GE = {{"-*****","**---*"," ****-"," **--","**- **","*****-"}};
    static const Glyph GF = {{"*****-","**--**"," **-"," ****"," **-"," *-"}};
    static const Glyph GG = {{"-****-","**--**","**","** ***","**--**","-***-*"}};
    static const Glyph GH = {{"**  **","**--**","******","**--**","**  **","**  **"}};
    static const Glyph GI = {{" ****"," -**-","  **","  **"," -**-"," ****"}};
    static const Glyph GJ = {{" ****"," -**-","   **","** **","**-**","-***-"}};
    static const Glyph GK = {{"***-**","-****-","***-","****-","-**-**","*** **"}};
    static const Glyph GL = {{" ****"," -**-","  **","  **"," -**--*"," ******"}};
    static const Glyph GM = {{" **-**"," **-**"," *-*-*"," *- -*"," *- -*"," *   *"}};
    static const Glyph GN = {{"**  **","**- **","***-**","**-***","** -**","**  **"}};
    static const Glyph GO = {{"-****-","**--**","**  **","**  **","**--**","-****-"}};
    static const Glyph GP = {{"*****-","-**-**"," ****-"," **-","-**-"," ****"}};
    static const Glyph GQ = {{"-****-","**--**","** -**","**-***","-****-"," -**"}};
    static const Glyph GR = {{"****-","-*--*-"," *--*-"," ***-","-*-**-","**--**"}};
    static const Glyph GS = {{"-****-","**--**"," -***-","  -**","**--**","-****-"}};
    static const Glyph GT = {{"******","*-**-*","  **","  **"," -**-"," ****"}};
    static const Glyph GU = {{"**  **","**  **","**  **","**  **","**--**","-****-"}};
    static const Glyph GV = {{"**  **","**  **","**  **","**--**"," -****-"," -**-"}};
    static const Glyph GW = {{"*   *","*- -*","*- -*","*-*-*","**-**","-*-*-"}};
    static const Glyph GX = {{"**--**","-****-"," -**-"," -**-","-****-","**--**"}};
    static const Glyph GY = {{"**  **","**--**"," -****-"," -**-"," -**-"," ****"}};
    static const Glyph GZ = {{"******","*- -**","  -**"," -**-"," -**--*","******"}};
    static const Glyph GAE = {{"-*****","**-**-","******","**-**-","** **-","** ***"}};
    static const Glyph GOE = {{"-***-*","*--**-","*-**-*","*-**-*","-**--*","*-***-"}};
    static const Glyph GAA = {{"  **","  --","-****-","**--**","******","**--**"}}; // Å (approx)

    // Lowercase
    static const Glyph Ga = {{
        " ***-",
        " --**",
        "-****",
        "**-**-",
        "-**-**",""}};
    static const Glyph Gb = {{"***"," -**-"," ****-"," ** **","-**-**","**-**-"}};
    static const Glyph Gc = {{" -****-"," **--**"," ** ---"," **--**"," -****-",""}};
    static const Glyph Gd = {{"   ***","   -**","-*****","**--**","**  **","-***-*"}};
    static const Glyph Ge = {{" -****-"," ** -**"," ******"," **-"," -****-",""}};
    static const Glyph Gf = {{" -***-"," -**--*"," ****"," -**-","  **"," ****"}};
    static const Glyph Gg = {{" -**-**"," ** **-"," -****","  -**"," ****-",""}};
    static const Glyph Gh = {{"***"," -**-"," ****-"," **-**","-** **","*** **"}};
    static const Glyph Gi = {{"  **","  --"," ***"," -**"," -**-"," ****"}};
    static const Glyph Gj = {{"   **","   --","  ***","  -**"," **-**"," -***-"}};
    static const Glyph Gk = {{"***"," -**"," **-**"," ****-"," -**-**"," *** **"}};
    static const Glyph Gl = {{" ***"," -**","  **","  **"," -**-"," ****"}};
    static const Glyph Gm = {{" **-**-"," -*****"," *-*-*"," *-*-*"," *-*-*",""}};
    static const Glyph Gn = {{" **-**-"," -*****"," **-**"," ** **"," ** **",""}};
    static const Glyph Go = {{" -****-"," **--**"," **  **"," **--**"," -****-",""}};
    static const Glyph Gp = {{" **-**-"," -**--*"," ****"," -**-"," ****",""}};
    static const Glyph Gq = {{" -**-**"," *--**-"," -****","  -**","  ****",""}};
    static const Glyph Gr = {{" **-**-"," -*****"," **--*"," -**-"," ****",""}};
    static const Glyph Gs = {{" -*****"," **-"," -****-","   -**"," *****-",""}};
    static const Glyph Gt = {{"  -*"," -**-"," ****"," -**-"," **-*"," -**-"}};
    static const Glyph Gu = {{" ** **"," ** **"," ** **"," **-**-"," -**-**",""}};
    static const Glyph Gv = {{" **  **"," **  **"," **--**"," -****-"," -**-",""}};
    static const Glyph Gw = {{" *   *"," *- -*"," *-*-*"," *-*-*"," -*-*-",""}};
    static const Glyph Gx = {{" **--**"," -****-","  -**-"," -****-"," **--**",""}};
    static const Glyph Gy = {{" **  **"," **--**"," -****-","  -*"," *****-",""}};
    static const Glyph Gz = {{" ******"," *--**-","  -**-"," -**--*"," ******",""}};
    static const Glyph Gae = {{" ****-"," -*-*"," -***-"," *-*-"," -****",""}};
    static const Glyph Goe = {{" -***-*"," *--**-"," *-**-*"," -**--*"," *-***-",""}};
    static const Glyph Gaa = {{"  -**-","  ***-","  --**"," -****"," **-**-"," -**-**"}};

    // Punctuation
    static const Glyph Gdot = {{"",""," **-"," -**","",""}};
    static const Glyph Gcolon = COLON;
    static const Glyph Gsemi = {{" **-"," -**","  -*"," *-","",""}};
    static const Glyph Gcomma = {{"",""," -*"," *-","",""}};
    static const Glyph Gapos = {{" -*"," *-","","","",""}};
    static const Glyph Gquote = {{" -* -*"," *- *-","","","",""}};
    static const Glyph Gstar = {{" * *","  *"," * *","","",""}};
    static const Glyph Gplus = {{"   *","   *"," *****","   *","   *",""}};
    static const Glyph Gbang = {{" **"," -**-"," -**-"," **","  --"," **"}};
    static const Glyph Gqmark = {{" -****-"," **--**","  -**-"," **-","  --","  **"}};
    static const Glyph Gdash = {{"",""," -****-","","",""}};
    static const Glyph Geq = {{" -****-",""," -****-","","",""}};
    static const Glyph Gunder = {{"","","",""," -****-",""}};
    static const Glyph Gslash = {{"   *","  *-","  *-","  *-","  *-","  *-"}};
    static const Glyph Glparen = {{"  -***","  **-","  **-","  **-","  **-","  -***"}};
    static const Glyph Grparen = {{"***-"," -**"," -**"," -**"," -**","***-"}};
    // Braces not included (7-row in source). Use brackets instead.
    static const Glyph Glbrack = {{" ****"," **"," **"," **"," **"," ****"}};
    static const Glyph Grbrack = {{" ****","   **","   **","   **","   **"," ****"}};
    static const Glyph Glt = {{"   *-","  *-"," *-","  *-","   *-",""}};
    static const Glyph Ggt = {{" -*"," -*","  -*"," -*"," -*",""}};
    static const Glyph Gamb = {{
        "  **",
        " ** *",
        "  **  *",
        "  **-*",
        "*  **",
        " ** **"}};
    static const Glyph Gpipe = {{"   *","   *","   *","   *","   *","   *"}};
    static const Glyph Gbslash = {{"  **","  -**","   -**","    -**","     -**",""}};
    static const Glyph Garrow = {{"  -*-","   -*-"," ******","   -*-","  -*-",""}}; // '→'

    switch (ch) {
        // Uppercase
        case 'A': return &GA; case 'B': return &GB; case 'C': return &GC; case 'D': return &GD; case 'E': return &GE; case 'F': return &GF; case 'G': return &GG; case 'H': return &GH; case 'I': return &GI; case 'J': return &GJ; case 'K': return &GK; case 'L': return &GL; case 'M': return &GM; case 'N': return &GN; case 'O': return &GO; case 'P': return &GP; case 'Q': return &GQ; case 'R': return &GR; case 'S': return &GS; case 'T': return &GT; case 'U': return &GU; case 'V': return &GV; case 'W': return &GW; case 'X': return &GX; case 'Y': return &GY; case 'Z': return &GZ; case 'Æ': return &GAE; case 'Ø': return &GOE; case 'Å': return &GAA;
        // Lowercase
        case 'a': return &Ga; case 'b': return &Gb; case 'c': return &Gc; case 'd': return &Gd; case 'e': return &Ge; case 'f': return &Gf; case 'g': return &Gg; case 'h': return &Gh; case 'i': return &Gi; case 'j': return &Gj; case 'k': return &Gk; case 'l': return &Gl; case 'm': return &Gm; case 'n': return &Gn; case 'o': return &Go; case 'p': return &Gp; case 'q': return &Gq; case 'r': return &Gr; case 's': return &Gs; case 't': return &Gt; case 'u': return &Gu; case 'v': return &Gv; case 'w': return &Gw; case 'x': return &Gx; case 'y': return &Gy; case 'z': return &Gz; case 'æ': return &Gae; case 'ø': return &Goe; case 'å': return &Gaa;
        // Punctuation and symbols
        case '.': return &Gdot; case ':': return &Gcolon; case ';': return &Gsemi; case ',': return &Gcomma; case '\'': return &Gapos; case '"': return &Gquote; case '*': return &Gstar; case '+': return &Gplus; case '!': return &Gbang; case '?': return &Gqmark; case '-': return &Gdash; case '=': return &Geq; case '_': return &Gunder; case '/': return &Gslash; case '(': return &Glparen; case ')': return &Grparen; case '[': return &Glbrack; case ']': return &Grbrack; case '<': return &Glt; case '>': return &Ggt; case '&': return &Gamb; case '|': return &Gpipe; case '\\': return &Gbslash;
        default: return nullptr;
    }
}

void draw_glyph(LEDStrip& strip, char ch, size_t top_row, size_t left_col,
                uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    const Glyph* gph = nullptr;
    if (ch >= '0' && ch <= '9') {
        gph = &DIGITS[ch - '0'];
    } else if (ch == ':') {
        gph = &COLON;
    } else {
        gph = get_letter_glyph(ch);
        if (!gph) return;
    }
    if (!gph) return;

    size_t base_r = top_row + 1;
    size_t base_c = left_col + 1;
    for (int i = 0; i < 6; ++i) {
        const char* row = gph->rows[i];
        if (!row || row[0] == '\0') continue;
        draw_row(strip, row, base_r + static_cast<size_t>(i), base_c, r, g, b, w);
    }
}

size_t draw_text(LEDStrip& strip, const char* text, size_t top_row, size_t left_col,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    size_t x = left_col;
    if (!text) return x;
    for (const char* p = text; *p; ++p) {
        draw_glyph(strip, *p, top_row, x, r, g, b, w);
        x += 8; // advance to next cell
    }
    return x;
}

} } // namespace leds::font6x6


