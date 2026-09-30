// QRGen.cpp : This file contains the 'main' function. Program execution begins and ends there.
// qr_authenticator.c
//
// Fixed QR Code Encoder
// ---------------------
// QR Version:          7
// Error Correction:    M
// Encoding:            Byte mode
// QR matrix:           45 x 45 modules
// Output bitmap:       128 x 128 pixels
// Bitmap format:       1 bit per pixel
//
// Intended for:
//   - Microsoft Visual Studio testing
//   - Later migration to a 32-bit MCU
//
// No external QR libraries required.
//
// | Parameter | Value           | Meaning                       |
// |---        |---              |---                            |
// | Type      | `totp`          | Time-based OTP                |
// | Secret    | Base32          | Shared cryptographic secret   |
// | Issuer    | `MyApplication` | Your server/application       |
// | Algorithm | `SHA1`          | HMAC-SHA1                     |
// | Digits    | `6`             | Six-digit authentication code | ** not required, default is 6 **
// | Period    | `30`            | New code every 30 seconds     | ** not required, default is 30 **
//
// For the QR capacity, maximum URI isabout 113 chars:
// otpauth://totp/IIIIIIIIII:UUUUUUUUUU?secret=SSSSSSSSSS&issuer=IIIIIIIIII&algorithm=SHA1&digits=6&period=30
//
// A fixed QR Version 6, error-correction level M has enough byte-mode capacity for this size. Its native matrix is 41×41 modules.
// For a 128×128 bitmap, a good representation is:
//
// QR Version:       6
// QR matrix:        41 x 41 modules
// Quiet zone:       4 modules each side
// Total:            49 x 49 modules
//
// Scale:            2 pixels/module
// Rendered QR:      98 x 98 pixels
// Canvas:           128 x 128 pixels
// Border remaining: 15 pixels each side
//
//
// unsigned char qr_text[QR_MAX_TEXT] =
// "otpauth://totp/"
// "MyApp:user01"
// "?secret=JBSWY3DPEH"
// "&issuer=MyApp"
// "&algorithm=SHA1"
// "&digits=6"
// "&period=30";
//
// int qr_encode_128(const unsigned char *text,unsigned char *bitmap);
//
// int qr_write_bmp(const char *filename,const unsigned char *bitmap);
//
//
//
//
//
//

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")
#endif

static int Windows_GetRandomBytes(unsigned char* buffer,unsigned int length)
{
#ifdef _WIN32

    NTSTATUS status;

    status =
        BCryptGenRandom(NULL,buffer,(ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status < 0) return -1;

    return 0;

#else 
    //  Do NOT substitute rand().
    //  Implement this using the MCU's * hardware random-number generator.
     

    (void)buffer;
    (void)length;

    return -1;

#endif
}

// ================================================================
// DEBUG CONSOLE + FILE OUTPUT
// ================================================================
//
// All DEBUG_Print() output is written to:
//
//     1. Console
//     2. CurrentDebugRun.txt
//
// DEBUG_Start() opens the file using "w", so the previous
// run is automatically erased.

#include <stdarg.h>

static FILE* debug_file = NULL;


// ------------------------------------------------------------
// START DEBUG LOG
// ------------------------------------------------------------

int DEBUG_Start(void)
{
#ifdef _MSC_VER
    if (fopen_s(&debug_file,"CurrentDebugRun.txt","w") != 0)
    {
        debug_file = NULL;
        printf("ERROR: Could not create CurrentDebugRun.txt\n");
        return -1;
    }
#else
    debug_file = fopen("CurrentDebugRun.txt","w");
    if (debug_file == NULL)
    {
        printf("ERROR: Could not create CurrentDebugRun.txt\n");
        return -1;
    }
#endif
    return 0;
}


// ------------------------------------------------------------
// DEBUG PRINT
// ------------------------------------------------------------
//
// Works like printf(), but writes identical formatted output to both the console and the current debug log.

void DEBUG_Print(const char* format, ...)
{
    va_list args;
    va_list file_args;

    if (format == NULL) return;
    va_start(args, format);//Start variable argument processing.
    va_copy(file_args, args);//Make a copy because a va_list cannot safely be reused after vprintf().
    vprintf(format, args);// Console output.
    if (debug_file != NULL) //Debug file output.
    {
        vfprintf(debug_file,format,file_args);
        fflush(debug_file);//Flush immediately so the debug file remains useful even if the program crashes later.
    }
    va_end(file_args);
    va_end(args);
}


// ------------------------------------------------------------
// CLOSE DEBUG LOG
// ------------------------------------------------------------

void DEBUG_Stop(void)
{
    if (debug_file != NULL)
    {
        fclose( debug_file);
        debug_file = NULL;
    }
}


 // ================================================================
 // CONFIGURATION
 // ================================================================

#define QR_VERSION          7
#define QR_SIZE             45

#define BMP_WIDTH           128
#define BMP_HEIGHT          128

#define BMP_BYTES_PER_ROW   (BMP_WIDTH / 8)
#define BMP_BUFFER_SIZE     (BMP_BYTES_PER_ROW * BMP_HEIGHT)

#define QR_SCALE            2

  // Version 7 / ECC M
  //
  // Total codewords: 196
  //
  // Reed-Solomon block structure:
  //
  // 4 blocks:
  //      31 data + 18 ECC
  //
  // Total data:
  //      124 codewords
  //
  // Total ECC:
  //      72 codewords

#define QR_DATA_CODEWORDS   124
#define QR_ECC_PER_BLOCK    18
#define QR_NUM_BLOCKS       4
#define QR_TOTAL_CODEWORDS  196

#define QR_MAX_INPUT        120


   // ================================================================
   // GLOBAL TEST STRING
   // ================================================================

unsigned char qr_plaintext[] =
"otpauth://totp/"
"MyApp:user01"
"?secret=JBSWY3DPEHPK3PXPJBSWY3DPEHPK3PXP"
"&issuer=MyApp"
"&algorithm=SHA1"
"&digits=6"
"&period=30";


// Final MCU-friendly bitmap.
//
// 128 * 128 / 8 = 2048 bytes.
//
// Bit = 1 -> black
// Bit = 0 -> white

unsigned char qr_bitmap[BMP_BUFFER_SIZE];


// ================================================================
// INTERNAL QR STORAGE
// ================================================================

 // Matrix values:
 //
 // bit 0 = black/white
 // bit 1 = reserved/function module

static uint8_t qr_matrix[QR_SIZE][QR_SIZE];


// Reed-Solomon working tables

static uint8_t gf_exp[512];
static uint8_t gf_log[256];


// ================================================================
// BIT STREAM WRITER
// ================================================================

typedef struct
{
    uint8_t* data;
    int bit_position;
    int max_bits;

} BIT_WRITER;


static void bitwriter_init(
    BIT_WRITER* bw,
    uint8_t* buffer,
    int bytes)
{
    memset(buffer, 0, bytes);

    bw->data = buffer;
    bw->bit_position = 0;
    bw->max_bits = bytes * 8;
}


static int bitwriter_put(
    BIT_WRITER* bw,
    uint32_t value,
    int bit_count)
{
    int i;

    for (i = bit_count - 1; i >= 0; i--)
    {
        int byte_index;
        int bit_index;

        if (bw->bit_position >= bw->max_bits)
            return -1;

        byte_index = bw->bit_position >> 3;
        bit_index = 7 - (bw->bit_position & 7);

        if ((value >> i) & 1)
        {
            bw->data[byte_index] |=
                (uint8_t)(1U << bit_index);
        }

        bw->bit_position++;
    }

    return 0;
}


// ================================================================
// GALOIS FIELD GF(256)
// ================================================================

static void gf_initialize(void)
{
    uint16_t x;
    int i;

    x = 1;

    for (i = 0; i < 255; i++)
    {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;

        x <<= 1;

        if (x & 0x100)
            x ^= 0x11D;
    }

    for (i = 255; i < 512; i++)
    {
        gf_exp[i] = gf_exp[i - 255];
    }
}


static uint8_t gf_multiply(
    uint8_t a,
    uint8_t b)
{
    if ((a == 0) || (b == 0))
        return 0;

    return gf_exp[gf_log[a] + gf_log[b]];
}


// ================================================================
// REED-SOLOMON GENERATOR
// ================================================================

static void rs_generator(
    uint8_t* generator,
    int degree)
{
    uint8_t next[64];

    int current_degree;
    int i;

    memset(generator, 0, 64);

    generator[0] = 1;

    current_degree = 0;

    while (current_degree < degree)
    {
        uint8_t root;

        root = gf_exp[current_degree];

        memset(next, 0, sizeof(next));

        for (i = 0; i <= current_degree; i++)
        {
            next[i] ^= generator[i];

            next[i + 1] ^=
                gf_multiply(generator[i], root);
        }

        current_degree++;

        memcpy(generator,
            next,
            (size_t)(current_degree + 1));
    }
}


// ================================================================
// REED-SOLOMON ENCODER
// ================================================================

static void rs_encode(
    const uint8_t* data,
    int data_length,
    uint8_t* ecc,
    int ecc_length)
{
    uint8_t generator[64];

    int i;
    int j;

    rs_generator(generator, ecc_length);

    memset(ecc, 0, ecc_length);

    for (i = 0; i < data_length; i++)
    {
        uint8_t factor;

        factor = data[i] ^ ecc[0];

        memmove(
            &ecc[0],
            &ecc[1],
            (size_t)(ecc_length - 1));

        ecc[ecc_length - 1] = 0;

        for (j = 0; j < ecc_length; j++)
        {
            ecc[j] ^=
                gf_multiply(
                    generator[j + 1],
                    factor);
        }
    }
}


// ================================================================
// QR MATRIX ACCESS
// ================================================================

static void qr_set_function(
    int x,
    int y,
    int black)
{
    if (x < 0 || x >= QR_SIZE ||
        y < 0 || y >= QR_SIZE)
        return;

    qr_matrix[y][x] =
        (uint8_t)((black ? 1 : 0) | 2);
}


static void qr_set_data(
    int x,
    int y,
    int black)
{
    qr_matrix[y][x] =
        (uint8_t)(black ? 1 : 0);
}


static int qr_is_reserved(
    int x,
    int y)
{
    return (qr_matrix[y][x] & 2) != 0;
}


// ================================================================
// FINDER PATTERN
// ================================================================

static void qr_draw_finder(
    int cx,
    int cy)
{
    int dx;
    int dy;

    // Includes the one-module white separator.

    for (dy = -4; dy <= 4; dy++)
    {
        for (dx = -4; dx <= 4; dx++)
        {
            int x;
            int y;
            int distance;
            int black;

            x = cx + dx;
            y = cy + dy;

            if (x < 0 || x >= QR_SIZE ||
                y < 0 || y >= QR_SIZE)
                continue;

            distance =
                (dx < 0 ? -dx : dx);

            if ((dy < 0 ? -dy : dy) > distance)
                distance =
                (dy < 0 ? -dy : dy);

            black =
                (distance != 2 &&
                    distance != 4);

            qr_set_function(x, y, black);
        }
    }
}


// ================================================================
// ALIGNMENT PATTERN
// ================================================================

static void qr_draw_alignment(
    int cx,
    int cy)
{
    int dx;
    int dy;

    for (dy = -2; dy <= 2; dy++)
    {
        for (dx = -2; dx <= 2; dx++)
        {
            int distance;
            int black;

            distance =
                (dx < 0 ? -dx : dx);

            if ((dy < 0 ? -dy : dy) > distance)
                distance =
                (dy < 0 ? -dy : dy);

            black =
                (distance != 1);

            qr_set_function(
                cx + dx,
                cy + dy,
                black);
        }
    }
}


// ================================================================
// VERSION 7 FUNCTION PATTERNS
// ================================================================

static void qr_draw_function_patterns(void)
{
    // Version 7 alignment centers:
    //
    // 6, 22, 38

    static const int alignment[] =
    {
        6, 22, 38
    };

    int i;
    int j;

    memset(qr_matrix, 0, sizeof(qr_matrix));

    qr_draw_finder(3, 3);
    qr_draw_finder(QR_SIZE - 4, 3);
    qr_draw_finder(3, QR_SIZE - 4);


    // Timing patterns

    for (i = 8; i < QR_SIZE - 8; i++)
    {
        qr_set_function(
            i,
            6,
            (i & 1) == 0);

        qr_set_function(
            6,
            i,
            (i & 1) == 0);
    }


    // Alignment patterns.
    //
    // Skip combinations that overlap finder patterns.

    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < 3; j++)
        {
            int x;
            int y;

            x = alignment[i];
            y = alignment[j];

            if ((x == 6 && y == 6) ||
                (x == 6 && y == 38) ||
                (x == 38 && y == 6))
            {
                continue;
            }

            qr_draw_alignment(x, y);
        }
    }


    // Reserve format information areas.

    for (i = 0; i < 9; i++)
    {
        if (i != 6)
        {
            qr_set_function(8, i, 0);
            qr_set_function(i, 8, 0);
        }
    }

    for (i = 0; i < 8; i++)
    {
        qr_set_function(
            QR_SIZE - 1 - i,
            8,
            0);

        qr_set_function(
            8,
            QR_SIZE - 1 - i,
            0);
    }


    // Dark module.

    qr_set_function(
        8,
        QR_SIZE - 8,
        1);


    // Version information areas.
    //
    // Version >= 7 requires these.

    {
        uint32_t version_bits;
        uint32_t rem;
        int bit;

        version_bits = QR_VERSION << 12;
        rem = version_bits;

        // BCH generator:
        // 0x1F25

        for (i = 17; i >= 12; i--)
        {
            if ((rem >> i) & 1)
            {
                rem ^=
                    (uint32_t)0x1F25 <<
                    (i - 12);
            }
        }

        version_bits |= rem;

        for (bit = 0; bit < 18; bit++)
        {
            int black;
            int a;
            int b;

            black =
                (version_bits >> bit) & 1;

            a = QR_SIZE - 11 + (bit % 3);
            b = bit / 3;

            qr_set_function(a, b, black);
            qr_set_function(b, a, black);
        }
    }
}


// ================================================================
// FORMAT INFORMATION
// ================================================================

static uint16_t qr_format_bits(
    int mask)
{
    // ECC M = binary 00
    //
    // Five-bit format value therefore consists
    // only of the 3-bit mask number.

    uint16_t data;
    uint16_t value;
    uint16_t rem;

    int i;

    data = (uint16_t)mask;

    value = (uint16_t)(data << 10);
    rem = value;

    // BCH generator = 0x537

    for (i = 14; i >= 10; i--)
    {
        if ((rem >> i) & 1)
        {
            rem ^=
                (uint16_t)(0x537 <<
                    (i - 10));
        }
    }

    value |= rem;

    // Required QR format mask.

    value ^= 0x5412;

    return value;
}


static void qr_draw_format_bits(
    int mask)
{
    uint16_t bits;

    int i;

    bits = qr_format_bits(mask);


    // First copy around upper-left finder.

    for (i = 0; i <= 5; i++)
        qr_set_function(8, i,
            (bits >> i) & 1);

    qr_set_function(
        8, 7,
        (bits >> 6) & 1);

    qr_set_function(
        8, 8,
        (bits >> 7) & 1);

    qr_set_function(
        7, 8,
        (bits >> 8) & 1);

    for (i = 9; i < 15; i++)
    {
        qr_set_function(
            14 - i,
            8,
            (bits >> i) & 1);
    }


    // Second copy.

    for (i = 0; i < 8; i++)
    {
        qr_set_function(
            QR_SIZE - 1 - i,
            8,
            (bits >> i) & 1);
    }

    for (i = 8; i < 15; i++)
    {
        qr_set_function(
            8,
            QR_SIZE - 15 + i,
            (bits >> i) & 1);
    }


    // Restore mandatory dark module.

    qr_set_function(
        8,
        QR_SIZE - 8,
        1);
}


// ================================================================
// DATA ENCODING
// ================================================================

static int qr_create_data(
    const unsigned char* text,
    uint8_t* data)
{
    BIT_WRITER bw;

    int length;
    int i;
    int pad_toggle;

    length = (int)strlen(
        (const char*)text);

    // Version 7 byte-mode character count
    // uses 8 bits.

    if (length > QR_MAX_INPUT ||
        length > 255)
        return -1;

    bitwriter_init(
        &bw,
        data,
        QR_DATA_CODEWORDS);


    // Mode indicator:
    //
    // 0100 = byte mode

    if (bitwriter_put(
        &bw,
        0x4,
        4) != 0)
        return -1;


    // Character count

    if (bitwriter_put(
        &bw,
        (uint32_t)length,
        8) != 0)
        return -1;


    // Payload

    for (i = 0; i < length; i++)
    {
        if (bitwriter_put(
            &bw,
            text[i],
            8) != 0)
            return -1;
    }


    // Terminator.

    {
        int remaining;

        remaining =
            bw.max_bits -
            bw.bit_position;

        if (remaining > 4)
            remaining = 4;

        if (remaining > 0)
        {
            if (bitwriter_put(
                &bw,
                0,
                remaining) != 0)
                return -1;
        }
    }


    // Pad to byte boundary.

    while (bw.bit_position & 7)
    {
        if (bitwriter_put( &bw, 0, 1) != 0) return -1;
    }


    // QR alternating pad bytes:
    //
    // EC 11 EC 11 ...

    pad_toggle = 0;

    while (bw.bit_position < bw.max_bits)
    {
        uint8_t pad;

        pad = pad_toggle ? 0x11 :  0xEC;

        if (bitwriter_put(&bw, pad, 8) != 0) return -1;

        pad_toggle ^= 1;
    }

    return 0;
}


// ================================================================
// CREATE FINAL CODEWORDS
// ================================================================

static void qr_create_codewords(const uint8_t* data, uint8_t* output)
{
    uint8_t blocks[QR_NUM_BLOCKS][31];
    uint8_t ecc[QR_NUM_BLOCKS][QR_ECC_PER_BLOCK];

    int block;
    int i;
    int pos;

    // Version 7 M:
    //
    // Four blocks, 31 data bytes each.

    for (block = 0; block < QR_NUM_BLOCKS; block++)
    {
        memcpy(blocks[block], &data[block * 31], 31);
        rs_encode(blocks[block], 31, ecc[block], QR_ECC_PER_BLOCK);
    }


    // Interleave data codewords.

    pos = 0;

    for (i = 0; i < 31; i++)
    {
        for (block = 0; block < QR_NUM_BLOCKS; block++)
        {
            output[pos++] = blocks[block][i];
        }
    }


    // Interleave ECC codewords.

    for (i = 0;  i < QR_ECC_PER_BLOCK; i++)
    {
        for (block = 0; block < QR_NUM_BLOCKS; block++)
        {
            output[pos++] =  ecc[block][i];
        }
    }
}


// ================================================================
// PLACE DATA MODULES
// ================================================================

static void qr_place_data( const uint8_t* codewords)
{
    int bit_index;
    int total_bits;

    int right;
    int upward;

    bit_index = 0;
    total_bits = QR_TOTAL_CODEWORDS * 8;

    right = QR_SIZE - 1;
    upward = 1;

    while (right >= 1)
    {
        int vertical;

        // Skip vertical timing column.

        if (right == 6) right--;

        for (vertical = 0; vertical < QR_SIZE; vertical++)
        {
            int y;
            int column;

            if (upward)
                y = QR_SIZE - 1 - vertical;
            else
                y = vertical;

            for (column = 0; column < 2; column++)
            {
                int x;

                x = right - column;

                if (!qr_is_reserved(x, y))
                {
                    int black;

                    black = 0;

                    if (bit_index < total_bits)
                    {
                        int byte_index;
                        int bit;

                        byte_index =  bit_index >> 3;

                        bit = 7 - (bit_index & 7);

                        black = (codewords[byte_index] >> bit) & 1;
                    }

                    qr_set_data(x, y, black);

                    bit_index++;
                }
            }
        }

        upward ^= 1;

        right -= 2;
    }
}


// ================================================================
// MASKING
// ================================================================

static int qr_mask_condition(
    int mask,
    int x,
    int y)
{
    switch (mask)
    {
    case 0:
        return ((x + y) % 2) == 0;

    case 1:
        return (y % 2) == 0;

    case 2:
        return (x % 3) == 0;

    case 3:
        return ((x + y) % 3) == 0;

    case 4:
        return (((y / 2) + (x / 3)) % 2) == 0;

    case 5:
        return (((x * y) % 2) + ((x * y) % 3)) == 0;

    case 6:
        return ((((x * y) % 2) + ((x * y) % 3)) % 2) == 0;

    case 7:
        return ((((x + y) % 2) + ((x * y) % 3)) % 2) == 0;
    }

    return 0;
}


static void qr_apply_mask( int mask)
{
    int x;
    int y;

    for (y = 0; y < QR_SIZE; y++)
    {
        for (x = 0; x < QR_SIZE; x++)
        {
            if (!qr_is_reserved(x, y))
            {
                if (qr_mask_condition(mask, x, y))
                {
                    qr_matrix[y][x] ^= 1;
                }
            }
        }
    }
}


// ================================================================
// MASK PENALTY
//
// Used to select the best of the eight QR masks.
// ================================================================

static int qr_penalty(void)
{
    int score;
    int x;
    int y;

    score = 0;


 
     // Rule 1: Consecutive modules in rows.
  

    for (y = 0; y < QR_SIZE; y++)
    {
        int run_color;
        int run_length;

        run_color =
            qr_matrix[y][0] & 1;

        run_length = 1;

        for (x = 1; x < QR_SIZE; x++)
        {
            int color;

            color =
                qr_matrix[y][x] & 1;

            if (color == run_color)
            {
                run_length++;

                if (run_length == 5)
                    score += 3;
                else if (run_length > 5)
                    score++;
            }
            else
            {
                run_color = color;
                run_length = 1;
            }
        }
    }


  
   // Rule 1: Columns.
   

    for (x = 0; x < QR_SIZE; x++)
    {
        int run_color;
        int run_length;

        run_color =
            qr_matrix[0][x] & 1;

        run_length = 1;

        for (y = 1; y < QR_SIZE; y++)
        {
            int color;

            color =
                qr_matrix[y][x] & 1;

            if (color == run_color)
            {
                run_length++;

                if (run_length == 5)
                    score += 3;
                else if (run_length > 5)
                    score++;
            }
            else
            {
                run_color = color;
                run_length = 1;
            }
        }
    }


    
    // Rule 2: 2x2 blocks.
   

    for (y = 0; y < QR_SIZE - 1; y++)
    {
        for (x = 0;
            x < QR_SIZE - 1;
            x++)
        {
            int c;

            c = qr_matrix[y][x] & 1;

            if (((qr_matrix[y][x + 1] & 1) == c) &&
                ((qr_matrix[y + 1][x] & 1) == c) &&
                ((qr_matrix[y + 1][x + 1] & 1) == c))
            {
                score += 3;
            }
        }
    }


   
    // Rule 3: Finder-like 1:1:3:1:1 pattern with four white modules before or after.
    

    for (y = 0; y < QR_SIZE; y++)
    {
        for (x = 0; x <= QR_SIZE - 11; x++)
        {
            int b[11];
            int i;

            for (i = 0; i < 11; i++)
                b[i] =
                qr_matrix[y][x + i] & 1;

            if ((b[0] == 1 &&
                b[1] == 0 &&
                b[2] == 1 &&
                b[3] == 1 &&
                b[4] == 1 &&
                b[5] == 0 &&
                b[6] == 1 &&
                b[7] == 0 &&
                b[8] == 0 &&
                b[9] == 0 &&
                b[10] == 0) ||

                (b[0] == 0 &&
                    b[1] == 0 &&
                    b[2] == 0 &&
                    b[3] == 0 &&
                    b[4] == 1 &&
                    b[5] == 0 &&
                    b[6] == 1 &&
                    b[7] == 1 &&
                    b[8] == 1 &&
                    b[9] == 0 &&
                    b[10] == 1))
            {
                score += 40;
            }
        }
    }


    for (x = 0; x < QR_SIZE; x++)
    {
        for (y = 0; y <= QR_SIZE - 11; y++)
        {
            int b[11];
            int i;

            for (i = 0; i < 11; i++)
                b[i] =
                qr_matrix[y + i][x] & 1;

            if ((b[0] == 1 &&
                b[1] == 0 &&
                b[2] == 1 &&
                b[3] == 1 &&
                b[4] == 1 &&
                b[5] == 0 &&
                b[6] == 1 &&
                b[7] == 0 &&
                b[8] == 0 &&
                b[9] == 0 &&
                b[10] == 0) ||

                (b[0] == 0 &&
                    b[1] == 0 &&
                    b[2] == 0 &&
                    b[3] == 0 &&
                    b[4] == 1 &&
                    b[5] == 0 &&
                    b[6] == 1 &&
                    b[7] == 1 &&
                    b[8] == 1 &&
                    b[9] == 0 &&
                    b[10] == 1))
            {
                score += 40;
            }
        }
    }


     // Rule 4: Dark/light balance.
   

    {
        int dark;
        int total;
        int percent;
        int deviation;

        dark = 0;

        total =  QR_SIZE * QR_SIZE;

        for (y = 0; y < QR_SIZE; y++)
        {
            for (x = 0; x < QR_SIZE; x++)
            {
                if (qr_matrix[y][x] & 1)  dark++;
            }
        }

        percent = (dark * 100) / total;

        deviation = percent - 50;

        if (deviation < 0)  deviation = -deviation;

        score +=  (deviation / 5) * 10;
    }

    return score;
}


// ================================================================
// SELECT BEST MASK
// ================================================================
 
static void qr_select_best_mask(void)
{
    uint8_t original[QR_SIZE][QR_SIZE];
    uint8_t best[QR_SIZE][QR_SIZE];

    int mask;
    int best_score;

    memcpy(original,qr_matrix,sizeof(original));

    // Initialize with mask 0.
    //
    // This guarantees that 'best' and 'best_score'
    // always contain a valid candidate before
    // comparing masks 1 through 7.

    memcpy(qr_matrix, original,sizeof(original));
    qr_apply_mask(0);
    qr_draw_format_bits(0);
    best_score = qr_penalty();
    memcpy( best, qr_matrix, sizeof(best));

    // Evaluate remaining masks.
  

    for (mask = 1; mask < 8; mask++)
    {
        int score;
       
         //Restore the unmasked QR matrix.
      
        memcpy( qr_matrix, original,sizeof(original));
    
       //Apply candidate mask and its corresponding format information.
      
        qr_apply_mask(mask);
        qr_draw_format_bits(mask);

        score = qr_penalty();

        if (score < best_score)
        {
            best_score = score;
            memcpy( best,qr_matrix, sizeof(best));
        }
    }
    //Install the lowest-penalty QR matrix.
     memcpy(qr_matrix, best, sizeof(best));
}

// ================================================================
// 128x128 1-BIT BITMAP FUNCTIONS
// ================================================================

static void bitmap_clear( unsigned char* bitmap)
{
    // Internal representation:
    //
    // 0 = white
    // 1 = black

    memset( bitmap, 0, BMP_BUFFER_SIZE);
}


static void bitmap_set_black(unsigned char* bitmap, int x, int y)
{
    int index;
    int bit;

    if (x < 0 || x >= BMP_WIDTH || y < 0 || y >= BMP_HEIGHT) return;

    index = y * BMP_BYTES_PER_ROW + (x >> 3);

    bit = 7 - (x & 7);

    bitmap[index] |= (unsigned char)(1U << bit);
}


// ================================================================
// RENDER QR MATRIX INTO 128x128 BITMAP
// ================================================================

static void qr_render_128(unsigned char* bitmap)
{
    int qr_pixels;
    int origin_x;
    int origin_y;

    int x;
    int y;

    bitmap_clear(bitmap);

    // QR symbol itself:
    //
    // 45 * 2 = 90 pixels.
    //
    // Centering gives 19 pixels around the symbol.
    //
    // This exceeds the required four-module quiet
    // zone:
    //
    // 4 modules * 2 = 8 pixels minimum.

    qr_pixels = QR_SIZE * QR_SCALE;

    origin_x = (BMP_WIDTH - qr_pixels) / 2;

    origin_y = (BMP_HEIGHT - qr_pixels) / 2;

    for (y = 0; y < QR_SIZE; y++)
    {
        for (x = 0; x < QR_SIZE; x++)
        {
            if (qr_matrix[y][x] & 1)
            {
                int px;
                int py;
                int sx;
                int sy;

                px = origin_x + x * QR_SCALE;

                py = origin_y + y * QR_SCALE;

                for (sy = 0;   sy < QR_SCALE;  sy++)
                {
                    for (sx = 0; sx < QR_SCALE; sx++)
                    {
                        bitmap_set_black(bitmap, px + sx, py + sy);
                    }
                }
            }
        }
    }
}


// ================================================================
// PUBLIC QR ENCODER
// ================================================================

int QR_Encode128( const unsigned char* text, unsigned char* bitmap)
{
    uint8_t data[QR_DATA_CODEWORDS];

    uint8_t codewords[ QR_TOTAL_CODEWORDS];

    if (text == NULL || bitmap == NULL) return -1;

    gf_initialize();

    if (qr_create_data(text, data) != 0)
    {
        return -2;
    }

    qr_create_codewords(data, codewords);

    qr_draw_function_patterns();

    qr_place_data(codewords);

    qr_select_best_mask();

    qr_render_128(bitmap);

    return 0;
}


// ================================================================
// LITTLE-ENDIAN FILE HELPERS
// ================================================================

static void file_write_u16(FILE* fp, uint16_t value)
{
    unsigned char b[2];

    b[0] = (unsigned char)(value & 0xFF);
    b[1] = (unsigned char)((value >> 8) & 0xFF);

    fwrite(b, 1, 2, fp);
}


static void file_write_u32(FILE* fp,uint32_t value)
{
unsigned char b[4];

    b[0] = (unsigned char)(value & 0xFF);
    b[1] = (unsigned char)((value >> 8) & 0xFF);
    b[2] = (unsigned char)((value >> 16) & 0xFF);
    b[3] = (unsigned char)((value >> 24) & 0xFF);

    fwrite(b, 1, 4, fp);
}


// ================================================================
// WRITE WINDOWS 1-BIT BMP
// ================================================================

int QR_WriteBMP(const char* filename, const unsigned char* bitmap)
{
FILE* fp;

uint32_t pixel_offset;
uint32_t image_size;
uint32_t file_size;

int y;

    if (filename == NULL ||  bitmap == NULL) return -1;

#ifdef _MSC_VER

    if (fopen_s( &fp, filename, "wb") != 0)
    {
        return -2;
    }

#else

    fp = fopen(filename, "wb");

    if (fp == NULL)
        return -2;

#endif


    
    // BMP:  14 byte file header  40 byte BITMAPINFOHEADER 8 byte 2-entry color palette
     
    pixel_offset = 14 + 40 + 8;
   
    // 128 pixels / 8 = 16 bytes per row.
    //  Already divisible by BMP's four-byte row alignment requirement.
     
    image_size = BMP_BYTES_PER_ROW * BMP_HEIGHT;
    file_size = pixel_offset +  image_size;

    // BITMAPFILEHEADER
  

    fwrite("BM", 1, 2, fp);

    file_write_u32(fp, file_size);
    file_write_u16(fp, 0);
    file_write_u16(fp, 0);
    file_write_u32( fp, pixel_offset);

    // BITMAPINFOHEADER
    file_write_u32(fp, 40);
    file_write_u32(fp, BMP_WIDTH);
    file_write_u32( fp, BMP_HEIGHT);
    file_write_u16(fp, 1);
    file_write_u16(fp, 1);
    file_write_u32(fp, 0);
    file_write_u32( fp, image_size);
    file_write_u32(fp, 2835);
    file_write_u32(fp, 2835);
    file_write_u32(fp, 2);
    file_write_u32(fp, 2);
  
    // Palette entry 0 = white. BMP palette format: // B, G, R, reserved
    
    fputc(255, fp);
    fputc(255, fp);
    fputc(255, fp);
    fputc(0, fp);
 
    //Palette entry 1 = black.
    
    fputc(0, fp);
    fputc(0, fp);
    fputc(0, fp);
    fputc(0, fp);
    
    // BMP rows are stored bottom-up.   

    for (y = BMP_HEIGHT - 1; y >= 0; y--)
    {
        const unsigned char* row;

        row = &bitmap[ y * BMP_BYTES_PER_ROW];

        fwrite( row, 1, BMP_BYTES_PER_ROW, fp);
    }

    fclose(fp);

    return 0;
}

// ================================================================
// TOTP ENROLLMENT SUPPORT
// ================================================================
//
// Generates:
//
//   1. 160-bit random TOTP secret
//   2. 32-character Base32 representation
//   3. otpauth:// provisioning URI
//
// The QR encoder remains independent of TOTP.

#include <stddef.h>

#define TOTP_SECRET_BYTES          20
#define TOTP_SECRET_BASE32_LENGTH  32

#define TOTP_ISSUER_MAX            20
#define TOTP_USERNAME_MAX          32

#define TOTP_URI_MAX               120


 // Error codes

#define TOTP_OK                     0
#define TOTP_ERROR_PARAMETER       -1
#define TOTP_ERROR_ISSUER          -2
#define TOTP_ERROR_USERNAME        -3
#define TOTP_ERROR_RANDOM          -4
#define TOTP_ERROR_URI_SIZE        -5
#define TOTP_ERROR_CHARACTER       -6


  // Random-byte callback.
  //
  // This is intentionally hardware/platform dependent.
  //
  // Return:
  //
  //   0 = success
  //   nonzero = failure
  //
  //
  // typedef int (*TOTP_RANDOM_FUNCTION)(unsigned char* buffer,unsigned int length);


// ================================================================
// BASE32 ENCODER
// ================================================================

static int TOTP_Base32Encode(
    const unsigned char* input,
    unsigned int input_length,
    char* output,
    unsigned int output_size)
{
    static const char base32_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

    unsigned int buffer;
    int bits_left;

    unsigned int input_index;
    unsigned int output_index;

    if (input == NULL ||
        output == NULL)
    {
        return TOTP_ERROR_PARAMETER;
    }

    // For our 20-byte secret:
    //
    // 20 bytes = 160 bits
    // 160 / 5 = 32 Base32 characters

    if (output_size <
        ((input_length * 8U + 4U) / 5U) + 1U)  // Base32 characters + '\0'
    {
        return TOTP_ERROR_PARAMETER;
    }

    buffer = 0;
    bits_left = 0;
    output_index = 0;

    for (input_index = 0;
        input_index < input_length;
        input_index++)
    {
        buffer =
            (buffer << 8) |
            input[input_index];

        bits_left += 8;

        while (bits_left >= 5)
        {
            unsigned int index;

            bits_left -= 5;

            index =
                (buffer >> bits_left) &
                0x1F;

            output[output_index++] =
                base32_table[index];
        }
    }

    if (bits_left > 0)
    {
        unsigned int index;

        index =
            (buffer << (5 - bits_left)) &
            0x1F;

        output[output_index++] =
            base32_table[index];
    }

    output[output_index] = '\0';

    return TOTP_OK;
}


// ================================================================
// CREATE RANDOM TOTP SECRET
// ================================================================
//
// Generates exactly 20 bytes / 160 bits of cryptographically
// secure random data.
//
// PC test implementation:
//     Windows BCryptGenRandom()
//
// ARM implementation:
//     Replace the body with the MCU/platform secure RNG.
//
// Returns:
//      0 = success
//     -1 = invalid parameter
//     -2 = random generator failure

int TOTP_CreateRandom(unsigned char secret[TOTP_SECRET_BYTES])
{
    if (secret == NULL)
    {
        return -1;
    }

#ifdef _WIN32

    {
        NTSTATUS status;
        status = BCryptGenRandom(NULL,secret,TOTP_SECRET_BYTES,BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0)   return -2;
     
    }

#else

    // ARM TARGET:
    //
    // Replace this section with the hardware/platform
    // cryptographically secure random source.
    //
    // Do NOT use rand().

    return -2;

#endif

    return 0;
}




// ================================================================
// URI CHARACTER SUPPORT
// ================================================================
//
// We percent-encode characters that should not be placed directly
// into the URI.
//
// This makes names such as:
//
//   "My Company"
//
// become:
//
//   "My%20Company"
//
// ================================================================

static int TOTP_IsUnreservedCharacter(unsigned char c)
{
    if ((c >= 'A') && (c <= 'Z'))                     return 1;
    if ((c >= 'a') && (c <= 'z'))                     return 1;
    if ((c >= '0') && (c <= '9'))                     return 1;
    if (c == '-' || c == '.' || c == '_' || c == '~') return 1;
    return 0;
}


static char TOTP_HexCharacter( unsigned int value)
{
    value &= 0x0F;
    if (value < 10) return (char)('0' + value);
    return (char)('A' + (value - 10));
}



 // Append one character to the URI.
static int TOTP_URIAppendCharacter(char* uri,unsigned int uri_size,unsigned int* position,char c)
{
    if ((*position + 1) >= uri_size) return TOTP_ERROR_URI_SIZE;

    uri[*position] = c;

    (*position)++;

    uri[*position] = '\0';

    return TOTP_OK;
}



// Append a normal C string.
static int TOTP_URIAppendString(char* uri,unsigned int uri_size,unsigned int* position,const char* text)
{
    while (*text != '\0')
    {
        if (TOTP_URIAppendCharacter(uri,uri_size, position, *text) != TOTP_OK)
        {
            return TOTP_ERROR_URI_SIZE;
        }
        text++;
    }
    return TOTP_OK;
}



 // Append a string using URI percent encoding.
static int TOTP_URIAppendEncoded(char* uri,unsigned int uri_size, unsigned int* position, const char* text)
{
    while (*text != '\0')
    {
        unsigned char c;

        c = (unsigned char)*text;

        if (TOTP_IsUnreservedCharacter(c))
        {
            if (TOTP_URIAppendCharacter(uri,uri_size, position, (char)c) != TOTP_OK)
            {
                return TOTP_ERROR_URI_SIZE;
            }
        }
        else
        {    
             // Percent encoding: space -> %20 @ -> %40
            if (TOTP_URIAppendCharacter(uri,uri_size,position,'%') != TOTP_OK)
            {
                return TOTP_ERROR_URI_SIZE;
            }

            if (TOTP_URIAppendCharacter(uri,uri_size,position,TOTP_HexCharacter(c >> 4))!= TOTP_OK)
            {
                return TOTP_ERROR_URI_SIZE;
            }

            if (TOTP_URIAppendCharacter(uri,uri_size,position,TOTP_HexCharacter(c)) != TOTP_OK)
            {
                return TOTP_ERROR_URI_SIZE;
            }
        }

        text++;
    }

    return TOTP_OK;
}


// ================================================================
// CREATE TOTP ENROLLMENT URI
// ================================================================
//
// Constructs:
//
// otpauth://totp/ISSUER:USERNAME
// ?secret=BASE32
// &issuer=ISSUER
//
// This function:
//
//     DOES NOT generate random numbers.
//     DOES NOT Base32 encode the secret.
//     DOES NOT generate the QR code.
//
// It only constructs the enrollment URI.
//
// We intentionally omit:
//
// algorithm=SHA1
// digits=6
// period=30
//
// Those are our defined TOTP defaults.
// ================================================================

int TOTP_CreateEnrollment(
    const char* issuer,
    const char* username,
    const char* secret_base32,
    char* uri,
    unsigned int uri_size)
{
    unsigned int issuer_length;
    unsigned int username_length;
    unsigned int secret_length;
    unsigned int position;
    int result;
 
	// Validate parameters.
    if (issuer == NULL || username == NULL || secret_base32 == NULL || uri == NULL)
    {
        return TOTP_ERROR_PARAMETER;
    }


    issuer_length   = (unsigned int)strlen(issuer);
    username_length = (unsigned int)strlen(username);
    secret_length   = (unsigned int)strlen(secret_base32);

    if (issuer_length < 1 || issuer_length > TOTP_ISSUER_MAX)        return TOTP_ERROR_ISSUER; // Require at least one character.
    if (username_length < 1 || username_length > TOTP_USERNAME_MAX)  return TOTP_ERROR_USERNAME; // Require at least one character.


    //We use a fixed 160-bit secret 160 bits / 5 bits per Base32 character = exactly 32 characters.
    if (secret_length != TOTP_SECRET_BASE32_LENGTH)                  return TOTP_ERROR_PARAMETER;
  
    position = 0;
    uri[0] = '\0';

    result = TOTP_URIAppendString(uri, uri_size, &position, "otpauth://totp/");
    if (result != TOTP_OK) return result;

    result = TOTP_URIAppendEncoded(uri,uri_size,&position,issuer);  //Label issuer.
    if (result != TOTP_OK)return result;

    result = TOTP_URIAppendCharacter(uri,uri_size,&position,':');
    if (result != TOTP_OK)return result;

    result = TOTP_URIAppendEncoded(uri,uri_size,&position,username); //Account name.
    if (result != TOTP_OK)return result;

    result = TOTP_URIAppendString(uri,uri_size,&position,"?secret=");
    if (result != TOTP_OK) return result;

    result = TOTP_URIAppendString(uri,uri_size,&position,secret_base32);
    if (result != TOTP_OK) return result;

    result = TOTP_URIAppendString(uri,uri_size,&position,"&issuer=");
    if (result != TOTP_OK) return result;

    result = TOTP_URIAppendEncoded(uri,uri_size, &position,issuer);
    if (result != TOTP_OK) return result;

    return TOTP_OK;
}





// ================================================================
// SHA-1 Stuff
// ================================================================
typedef struct
{
    uint32_t state[5];
    uint64_t bit_count;
    unsigned char buffer[64];
    unsigned int buffer_length;
} SHA1_CONTEXT;


static uint32_t SHA1_RotateLeft( uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32 - bits));
}


static void SHA1_Transform( SHA1_CONTEXT* context, const unsigned char block[64])
{
    uint32_t w[80];

    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;

    uint32_t f;
    uint32_t k;
    uint32_t temp;

    int i;



    //  Convert input block to 16 big-endian 32-bit words.
    

    for (i = 0; i < 16; i++)
    {
        w[i] =
            ((uint32_t)block[i * 4] << 24) |
            ((uint32_t)block[i * 4 + 1] << 16) |
            ((uint32_t)block[i * 4 + 2] << 8) |
            ((uint32_t)block[i * 4 + 3]);
    }


  //Expand to 80 words.
    

    for (i = 16; i < 80; i++)
    {
        w[i] =
            SHA1_RotateLeft(
                w[i - 3] ^
                w[i - 8] ^
                w[i - 14] ^
                w[i - 16],
                1);
    }


    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];


    for (i = 0; i < 80; i++)
    {
        if (i < 20)
        {
            f = (b & c) | ((~b) & d);

            k = 0x5A827999UL;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;

            k = 0x6ED9EBA1UL;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);

            k = 0x8F1BBCDCUL;
        }
        else
        {
            f = b ^ c ^ d;

            k = 0xCA62C1D6UL;
        }


        temp = SHA1_RotateLeft(a, 5) + f + e + k + w[i];

        e = d;
        d = c;

        c = SHA1_RotateLeft(b, 30);

        b = a;
        a = temp;
    }


    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}


// ================================================================
// SHA1 INITIALIZE
// ================================================================

static void SHA1_Init(SHA1_CONTEXT* context)
{
    context->state[0] = 0x67452301UL;

    context->state[1] = 0xEFCDAB89UL;

    context->state[2] = 0x98BADCFEUL;

    context->state[3] = 0x10325476UL;

    context->state[4] = 0xC3D2E1F0UL;

    context->bit_count = 0;

    context->buffer_length = 0;
}


// ================================================================
// SHA1 UPDATE
// ================================================================

static void SHA1_Update(SHA1_CONTEXT* context, const unsigned char* data, unsigned int length)
{
    unsigned int i;

    for (i = 0; i < length; i++)
    {
        context->buffer[
            context->buffer_length++] =
            data[i];

            context->bit_count += 8;

            if (context->buffer_length == 64)
            {
                SHA1_Transform(
                    context,
                    context->buffer);

                context->buffer_length = 0;
            }
    }
}


// ================================================================
// SHA1 FINAL
// ================================================================

static void SHA1_Final( SHA1_CONTEXT* context, unsigned char digest[20])
{
    uint64_t original_bit_count;
    unsigned int i;
    unsigned char length_bytes[8];

    // Save the original message length before adding SHA-1 padding.
    original_bit_count = context->bit_count;

    // Append 0x80.
    context->buffer[ context->buffer_length++] = 0x80;


     
       // If insufficient room remains for the 64-bit length, finish this block first.
        if (context->buffer_length > 56)
        {
            while (context->buffer_length < 64)
            {
                context->buffer[context->buffer_length++] = 0;
            }

            SHA1_Transform( context,context->buffer);

            context->buffer_length = 0;
        }


        // Pad with zeros until byte 56.
        while (context->buffer_length < 56)
        {
            context->buffer[context->buffer_length++] = 0;
        }


       
        //SHA-1 length is big endian.
        for (i = 0; i < 8; i++)
        {
            length_bytes[7 - i] = (unsigned char)(original_bit_count >> (i * 8));
        }


        for (i = 0; i < 8; i++)
        {
            context->buffer[context->buffer_length++] = length_bytes[i];
        }


        SHA1_Transform(context, context->buffer);


   
         // Convert state to big-endian digest.
        for (i = 0; i < 5; i++)
        {
            digest[i * 4]     = (unsigned char)(context->state[i] >> 24);

            digest[i * 4 + 1] = (unsigned char)(context->state[i] >> 16);

            digest[i * 4 + 2] = (unsigned char)(context->state[i] >> 8);

            digest[i * 4 + 3] = (unsigned char)(context->state[i]);
        }
}


// ================================================================
// HMAC-SHA1
// ================================================================ 

static void HMAC_SHA1(
    const unsigned char* key,
    unsigned int key_length,
    const unsigned char* message,
    unsigned int message_length,
    unsigned char digest[20])
{
    unsigned char key_block[64];

    unsigned char inner_pad[64];
    unsigned char outer_pad[64];

    unsigned char inner_digest[20];

    SHA1_CONTEXT context;

    unsigned int i;


    memset(key_block, 0,sizeof(key_block));


    
     // TOTP keys in this project are 20 bytes, 
     // therefore normally less than SHA-1's 64-byte block size.
     // Keep support for larger keys anyway.
     

    if (key_length > 64)
    {
        SHA1_CONTEXT key_context;

        unsigned char key_digest[20];

        SHA1_Init(&key_context);

        SHA1_Update(&key_context, key, key_length);

        SHA1_Final( &key_context, key_digest);

        memcpy( key_block, key_digest, 20);
    }
    else
    {
        memcpy( key_block,key, key_length);
    }


    
     // Construct inner and outer pads.
     

    for (i = 0; i < 64; i++)
    {
        inner_pad[i] = key_block[i] ^ 0x36;

        outer_pad[i] = key_block[i] ^ 0x5C;
    }


 
     //Inner hash: SHA1( ipad || message* )


    SHA1_Init(&context);

    SHA1_Update(&context,inner_pad, 64);

    SHA1_Update(&context, message, message_length);

    SHA1_Final( &context, inner_digest);


  
    // Outer hash: SHA1( opad ||  inner_digest)
   

    SHA1_Init(&context);

    SHA1_Update(&context,outer_pad,64);

    SHA1_Update(&context,inner_digest,20);

    SHA1_Final(&context, digest);
}


// ================================================================
// GENERATE TOTP
// ================================================================
//
// unix_time:
//
//     Seconds since 1970-01-01 UTC.
//
// Standard configuration:
//
//     T0     = 0
//     period = 30 seconds
//     digits = 6
//     hash   = SHA-1
//
// ================================================================

static uint32_t TOTP_Generate(
    const unsigned char secret[TOTP_SECRET_BYTES],
    uint64_t unix_time)
{
    uint64_t counter;

    unsigned char counter_bytes[8];

    unsigned char hash[20];

    unsigned int offset;

    uint32_t binary;

    uint32_t otp;

    int i;




    counter = unix_time / 30; //Calculate the 30-second moving counter.


    // HOTP/TOTP counter is represented as an 8-byte big-endian integer.

    for (i = 0; i < 8; i++)
    {
        counter_bytes[7 - i] = (unsigned char)(counter >> (i * 8));
    }


    // Calculate HMAC-SHA1.

    HMAC_SHA1(secret,TOTP_SECRET_BYTES,counter_bytes,8,hash);


    // RFC dynamic truncation.

    offset = hash[19] & 0x0F;


    binary =
        ((uint32_t)(hash[offset] & 0x7F) << 24) |
        ((uint32_t)hash[offset + 1] << 16) |
        ((uint32_t)hash[offset + 2] << 8) |
        ((uint32_t)hash[offset + 3]);



	otp = binary % 1000000UL;  // 6 digits

    return otp;
}


// ================================================================
// VERIFY TOTP
// ================================================================
//
// window = 0:
//
//     Accept current 30-second period only.
//
// window = 1:
//
//     Accept:
//
//       previous period
//       current period
//       next period
//
// The +/-1 window is normally preferable because
// clocks are never perfectly synchronized.
//
// ================================================================

static int TOTP_Verify(
    const unsigned char secret[TOTP_SECRET_BYTES],
    uint64_t unix_time,
    uint32_t user_code,
    int window)
{
    int offset;

    for (offset = -window; offset <= window; offset++)
    {
        uint64_t test_time;
        uint32_t expected_code;


        // Avoid unsigned underflow.

        if (offset < 0)
        {
            uint64_t adjustment;

            adjustment = (uint64_t)(-offset) * 30;

            if (unix_time < adjustment)
                continue;

            test_time = unix_time -  adjustment;
        }
        else
        {
            test_time =  unix_time +  ((uint64_t)offset * 30);
        }


        expected_code = TOTP_Generate(secret, test_time);


        if (expected_code == user_code)
        {
            return 1;
        }
    }

    return 0;
}


// Read one line from stdin.
//
// If the line does not fit in the buffer, the rest of it is read and
// discarded so it cannot leak into the next read.
//
// Returns:
//      0 = success
//     -1 = EOF / read error
//     -2 = line too long (discarded)
static int read_line(char* buffer, int size)
{
    int c;

    if (fgets(buffer, size, stdin) == NULL) return -1;

    if (strchr(buffer, '\n') != NULL) return 0;

    // No newline: either the line was too long, or EOF ended it.
    c = getchar();
    if (c == EOF) return 0;
    if (c == '\n') return 0; // Line exactly filled the buffer.

    while (c != '\n' && c != EOF) c = getchar();
    return -2;
}


static int sanitize_userINput(char *input)
{


        int i, entered_code;

        for (i = 0; i < 6; i++)
        {
            if (input[i] < '0' || input[i] > '9')
            {
                DEBUG_Print("\nInvalid code.\n");
                DEBUG_Print("Please enter exactly six digits.\n");
                return -1;
            }
        }

        // Accept "\n", "\r\n" or end of string after the six digits.
        i = 6;
        if (input[i] == '\r') i++;
        if (input[i] != '\n' && input[i] != '\0')
        {
            DEBUG_Print("\nInvalid code length.\n");
            return -1;
        }
 
        entered_code = 0;


     for (i = 0; i < 6; i++)
            {
                entered_code = entered_code * 10 + (uint32_t)(input[i] - '0');
            }
   

	return entered_code;
}

// ================================================================
// DEBUG: DUMP 2048-BYTE QR BITMAP TO CSV
// ================================================================
//
// Creates a CSV containing the complete qr_bitmap[] as hexadecimal
// byte values.
//
// Format:
//
// 0x00,0x00,0x3F,0x80,...
//
// 16 bytes are written per line. This corresponds to exactly one
// 128-pixel bitmap row because:
//
//     128 pixels / 8 bits = 16 bytes
//
// Therefore the output contains 128 lines x 16 bytes = 2048 bytes.
//
// The resulting data can also be pasted directly into a C array.
//
//
// 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
// 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
//
//
// 128 pixels
// <---------------------->
//
// Byte 0                  Byte 15
// |                        |
// v                        v
// xxxxxxxx xxxxxxxx ... xxxxxxxx
// <------ 16 bytes ----------->
//
//
//
// For debugging, the mismatch index is also easily converted to a bitmap row and byte position:
// index = result - 1;
//
// row = index / 16;
// byte_in_row = index % 16;
//
int QR_DumpBitmapCSV(const char* filename, const unsigned char* bitmap)
{
    FILE* fp;
    int i;

    if (filename == NULL || bitmap == NULL)
    {
        return -1;
    }

#ifdef _MSC_VER

    if (fopen_s(&fp,filename, "w") != 0)
    {
        return -2;
    }

#else

    fp = fopen(filename,"w");

    if (fp == NULL) return -2;

#endif

    for (i = 0; i < BMP_BUFFER_SIZE; i++)
    {
        fprintf(fp,"0x%02X",(unsigned int)bitmap[i]);                 
        if (i != (BMP_BUFFER_SIZE - 1)) fprintf(fp, ",");   // Add comma unless this is the final byte.               
        if (((i + 1) % BMP_BYTES_PER_ROW) == 0) fprintf(fp, "\n");// 16 bytes = one complet 128-pixel bitmap row.     
    }
    fclose(fp);
    return 0;
}
// ================================================================
// TEST APPLICATION
// ================================================================
 //
 // Microsoft Authenticator TOTP Test
 // =================================
 //
 // Generated Base32 secret:
 // WTSK5HKRTH6VPM4GWQLMBCOAWDHTNFRA
 //
 // Enrollment URI:
 // otpauth://totp/MyApp:user01?secret=WTSK5HKRTH6VPM4GWQLMBCOAWDHTNFRA&issuer=MyApp
 //
 // QR code written to:
 // authenticator.bmp
 //
 // Open authenticator.bmp and scan it
 // with Microsoft Authenticator.
 //
 // After the account has been added,
 // press ENTER to continue...
 //
 //
 // Enter the 6-digit code currently
 // displayed by Microsoft Authenticator:
 //
 // > 083417
 //
 // Unix time: 1790300000
 // 30-second counter: 59676666
 // Calculated current TOTP: 083417
 //
 // *****************************
 //   AUTHENTICATION PASSED    *
 // *****************************
int main(void)
{
    unsigned char secret[TOTP_SECRET_BYTES];
    char secret_base32[TOTP_SECRET_BASE32_LENGTH + 1];
    char uri[TOTP_URI_MAX];
    char input[32];
    uint32_t entered_code;
    uint64_t unix_time;
    int result;

    if (DEBUG_Start() != 0)        return 1;
    
    DEBUG_Print( "Microsoft Authenticator TOTP Test\n");
    DEBUG_Print("=================================\n\n");

    result = TOTP_CreateRandom(secret);
    if (result != TOTP_OK)
    {
        DEBUG_Print("TOTP_CreateRandom failed: %d\n", result);
        DEBUG_Stop();
        return 1;
    }

    result =  TOTP_Base32Encode(secret,TOTP_SECRET_BYTES,secret_base32,sizeof(secret_base32));
    if (result != TOTP_OK)
    {
        DEBUG_Print("TOTP_Base32Encode failed: %d\n",result);
        DEBUG_Stop();
        return 1;
    }

    result = TOTP_CreateEnrollment("MyApp","user01",secret_base32,uri, sizeof(uri));

    if (result != TOTP_OK)
    {
        DEBUG_Print("TOTP_CreateEnrollment failed: %d\n",result);
        DEBUG_Stop();
        return 1;
    }

    DEBUG_Print("Generated Base32 secret:\n");
    DEBUG_Print("%s\n\n",secret_base32);
    DEBUG_Print("Enrollment URI:\n");
    DEBUG_Print("%s\n\n",uri);

    result = QR_Encode128( (const unsigned char*)uri, qr_bitmap);
    if (result != 0)
    {
        DEBUG_Print("QR_Encode128 failed: %d\n", result);
        DEBUG_Stop();
        return 1;
    }

    result =  QR_DumpBitmapCSV("qr_bitmap.csv", qr_bitmap);
    if (result != 0)
    {
        DEBUG_Print(  "QR_DumpBitmapCSV failed: %d\n", result);
        DEBUG_Stop();
        return 1;
    }
    DEBUG_Print("Raw QR bitmap written to qr_bitmap.csv\n");

    result = QR_WriteBMP("authenticator.bmp", qr_bitmap);
    if (result != 0)
    {
        DEBUG_Print("QR_WriteBMP failed: %d\n", result);
        DEBUG_Stop();
        return 1;
    }

    DEBUG_Print( "QR code written to: authenticator.bmp\n\n");


    ///  TEST
    DEBUG_Print("Open authenticator.bmp and scan it\n");
    DEBUG_Print("with Microsoft Authenticator.\n\n");
    DEBUG_Print("After the account has been added,\n");
    DEBUG_Print("press ENTER to continue...");


    fflush(stdout);
    /// user input delay
    if (read_line(input, sizeof(input)) == -1)
    {
        DEBUG_Print("\nInput error.\n");
        DEBUG_Stop();
        return 1;
    }
    DEBUG_Print("\n");
    DEBUG_Print("Enter the 6-digit code currently\n");
    DEBUG_Print("displayed by Microsoft Authenticator:\n\n");
    DEBUG_Print("> ");
    fflush(stdout);

    result = read_line(input, sizeof(input));
    if (result == -1)
    {
        DEBUG_Print("\nInput error.\n");
        DEBUG_Stop();
        return 1;
    }
    if (result == -2)
    {
        DEBUG_Print("\nInvalid code length.\n");
        DEBUG_Stop();
        return 1;
    }
    result = sanitize_userINput(input); // Require exactly six decimal digits.
    if (result < 0)
    {
        DEBUG_Print("\nInvalid code entered.\n");
        DEBUG_Stop();
        return 1;
    }
    entered_code = (uint32_t)result;
    DEBUG_Print("Entered TOTP: %06u\n", (unsigned int)entered_code);

    unix_time =  (uint64_t)time(NULL);
    DEBUG_Print( "\nUnix time: %llu\n", (unsigned long long)unix_time);
    DEBUG_Print( "30-second counter: %llu\n", (unsigned long long)( unix_time / 30));
    DEBUG_Print("Calculated current TOTP: %06u\n", (unsigned int) TOTP_Generate(secret,unix_time));
    //     Window = 1 permits:    previous, current, next  30-second interval
    result = TOTP_Verify(secret,unix_time,entered_code,1);
    DEBUG_Print("\n");

    if (result)
    {
        DEBUG_Print("******************************\n");
        DEBUG_Print("*   AUTHENTICATION PASSED    *\n");
        DEBUG_Print("******************************\n");
    }
    else
    {
        DEBUG_Print("******************************\n");
        DEBUG_Print("*   AUTHENTICATION FAILED    *\n");
        DEBUG_Print("******************************\n");
        DEBUG_Print("\n");
        DEBUG_Print("Authenticator entered: %06u\n",(unsigned int)entered_code);
        DEBUG_Print("Our current code:       %06u\n",(unsigned int)TOTP_Generate(secret,unix_time));
    }

    DEBUG_Print("\nPress ENTER to exit...");
    (void)read_line(input, sizeof(input));
    return 0;
}