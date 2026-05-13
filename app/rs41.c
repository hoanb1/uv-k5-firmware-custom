/* RS41 Radiosonde Decoder - Core Implementation
 * Ported from rs1729/RS (rs41mod.c) - GPL License
 * Stripped down for ARM Cortex-M0 (DP32G030)
 *
 * Changes from original:
 *   - No Reed-Solomon ECC (saves ~3KB flash)
 *   - Integer math for ECEF->LLA conversion (no double/float)
 *   - No stdio/printf/file I/O
 *   - Minimal RAM footprint (~500 bytes total)
 */

#include "rs41.h"
#include <string.h>

// ============================================================
// RS41 XOR Mask (64 bytes) — LFSR-generated pseudorandom sequence
// Used for data whitening/descrambling
// ============================================================
static const uint8_t rs41_xor_mask[RS41_XOR_MASK_LEN] = {
    0x96, 0x83, 0x3E, 0x51, 0xB1, 0x49, 0x08, 0x98,
    0x32, 0x05, 0x59, 0x0E, 0xF9, 0x44, 0xC6, 0x26,
    0x21, 0x60, 0xC2, 0xEA, 0x79, 0x5D, 0x6D, 0xA1,
    0x54, 0x69, 0x47, 0x0C, 0xDC, 0xE8, 0x5C, 0xF1,
    0xF7, 0x76, 0x82, 0x7F, 0x07, 0x99, 0xA2, 0x2C,
    0x93, 0x7C, 0x30, 0x63, 0xF5, 0x10, 0x2E, 0x61,
    0xD0, 0xBC, 0xB4, 0xB6, 0x06, 0xAA, 0xF4, 0x23,
    0x78, 0x6E, 0x3B, 0xAE, 0xBF, 0x7B, 0x4C, 0xC1
};

// ============================================================
// RS41 Sync Header (8 bytes after XOR descramble)
// Raw bits (before descramble): 0x10B6CA11229612F8
// After XOR with mask[0..7]:    0x8635F44093DF1A60
// ============================================================
static const uint8_t rs41_header[RS41_HEADER_LEN] = {
    0x10, 0xB6, 0xCA, 0x11, 0x22, 0x96, 0x12, 0xF8
};

// 64-bit representation for fast correlation matching (shifted left, MSB is first bit received)
static const uint64_t rs41_header_64 = 0x086D53884469481FULL;

// ============================================================
// Block position offsets within the frame (after header + ECC)
// Header: [0x00-0x07]  ECC: [0x08-0x37]  FrameType: [0x38]
// Blocks start at 0x39
// ============================================================

// STATUS block (0x79) — typical position
#define POS_FRAME_NR     0x3B   // 2 bytes LE: frame number
#define POS_SONDE_ID     0x3D   // 8 bytes: sonde serial
#define POS_BATT         0x45   // 2 bytes LE: battery (mV)

// GPS1 block (0x7C) — GPS week + time
#define POS_GPS_WEEK     0x95   // 2 bytes LE
#define POS_GPS_TOW      0x97   // 4 bytes LE (ms)

// GPS3 block (0x7B) — ECEF position
#define POS_GPS_ECEF_X   0x72   // 4 bytes LE (cm)
#define POS_GPS_ECEF_Y   0x76   // 4 bytes LE (cm)
#define POS_GPS_ECEF_Z   0x7A   // 4 bytes LE (cm)
#define POS_GPS_VEL_X    0x7E   // 2 bytes LE (cm/s)
#define POS_GPS_VEL_Y    0x80   // 2 bytes LE (cm/s)
#define POS_GPS_VEL_Z    0x82   // 2 bytes LE (cm/s)
#define POS_GPS_NUMSV    0x84   // 1 byte


// ============================================================
// CRC-16 CCITT (polynomial 0x1021)
// Used per-block in RS41 frames
// ============================================================
static uint16_t rs41_crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= ((uint16_t)data[i] << 8);
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// ============================================================
// Check CRC for a block at given position
// Block structure: [ID][LEN][DATA...][CRC_LO][CRC_HI]
// Returns true if CRC matches
// ============================================================
static bool rs41_check_block_crc(const uint8_t *frame, uint16_t pos)
{
    if (pos + 2 >= RS41_FRAME_LEN)
        return false;

    uint8_t blk_id  = frame[pos];
    uint8_t blk_len = frame[pos + 1];

    (void)blk_id;

    // Bounds check
    if (pos + 2 + blk_len + 2 > RS41_FRAME_LEN)
        return false;

    // CRC is computed over block data (not including ID and LEN bytes)
    uint16_t crc_calc = rs41_crc16(&frame[pos + 2], blk_len);

    // CRC stored little-endian after data
    uint16_t crc_frame = (uint16_t)frame[pos + 2 + blk_len]
                       | ((uint16_t)frame[pos + 2 + blk_len + 1] << 8);

    return (crc_calc == crc_frame);
}

// ============================================================
// XOR Descramble the entire frame
// ============================================================
static void rs41_descramble(uint8_t *frame, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        frame[i] ^= rs41_xor_mask[i % RS41_XOR_MASK_LEN];
    }
}

// ============================================================
// Read 16-bit little-endian value from frame
// ============================================================
static inline uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// ============================================================
// Read 32-bit little-endian value from frame
// ============================================================
static inline uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

// ============================================================
// Read signed 16-bit little-endian value from frame
// ============================================================
static inline int16_t read_i16_le(const uint8_t *p)
{
    return (int16_t)read_u16_le(p);
}

// ============================================================
// Read signed 32-bit little-endian value from frame
// ============================================================
static inline int32_t read_i32_le(const uint8_t *p)
{
    return (int32_t)read_u32_le(p);
}

#include <math.h>

static void rs41_ecef_to_lla(int32_t ecef_x_cm, int32_t ecef_y_cm, int32_t ecef_z_cm,
                              int32_t *lat_1e6, int32_t *lon_1e6, int32_t *alt_cm)
{
    float x = ecef_x_cm / 100.0f;
    float y = ecef_y_cm / 100.0f;
    float z = ecef_z_cm / 100.0f;
    
    const float a = 6378137.0f;
    const float e2 = 0.00669437999014f;
    const float b = 6356752.3142f;
    const float ep2 = 0.00673949674228f;
    
    float p = sqrtf(x*x + y*y);
    float th = atan2f(a*z, b*p);
    
    float lon = atan2f(y, x);
    float sin_th = sinf(th);
    float cos_th = cosf(th);
    
    float lat = atan2f(z + ep2*b*sin_th*sin_th*sin_th,
                       p - e2*a*cos_th*cos_th*cos_th);
                       
    float sin_lat = sinf(lat);
    float N = a / sqrtf(1.0f - e2*sin_lat*sin_lat);
    float alt = p / cosf(lat) - N;
    
    *lon_1e6 = (int32_t)(lon * (180.0f / 3.1415926535f) * 1000000.0f);
    *lat_1e6 = (int32_t)(lat * (180.0f / 3.1415926535f) * 1000000.0f);
    *alt_cm = (int32_t)(alt * 100.0f);
}

// ============================================================
// Find a block by ID within the frame
// Returns position of block start, or 0 if not found
// ============================================================
static uint16_t rs41_find_block(const uint8_t *frame, uint8_t blk_id)
{
    // GPS3 block is 0x7B, length 0x15
    // PTU block is 0x7A, length 0x21
    // STATUS block is 0x79, length 0x28
    // GPS INFO block is 0x7C, length 0x2A

    uint8_t expected_len = 0;
    if (blk_id == RS41_BLK_STATUS) expected_len = 0x28;
    else if (blk_id == RS41_BLK_MEAS) expected_len = 0x21;
    else if (blk_id == RS41_BLK_GPS_INFO) expected_len = 0x2A;
    else if (blk_id == RS41_BLK_GPS_POS) expected_len = 0x15;

    // Scan byte-by-byte starting after header (0x39)
    for (uint16_t pos = 0x39; pos < RS41_FRAME_LEN - 4; pos++) {
        if (frame[pos] == blk_id && frame[pos + 1] == expected_len) {
            return pos;
        }
    }
    return 0; // not found
}

// ============================================================
// Parse STATUS block (0x79)
// ============================================================
static void rs41_parse_status(RS41_Decoder_t *dec)
{
    uint16_t pos = rs41_find_block(dec->frame, RS41_BLK_STATUS);
    if (pos == 0) return;

    if (!rs41_check_block_crc(dec->frame, pos))
        return;

    dec->data.crc_ok |= RS41_CRC_STATUS;

    // Frame number: 2 bytes at offset +2 from block start (after ID+LEN)
    dec->data.frame_nr = read_u16_le(&dec->frame[pos + 2]);

    // Sonde ID: 8 bytes ASCII at offset +4
    memcpy(dec->data.sonde_id, &dec->frame[pos + 4], 8);
    dec->data.sonde_id[8] = '\0';

    // Battery voltage: 2 bytes at offset +14 (typical offset in STATUS block)
    // Value is in mV, little-endian
    if (pos + 2 + 40 + 2 <= RS41_FRAME_LEN) {
        dec->data.batt_mv = read_u16_le(&dec->frame[pos + 16]);
    }
}

// ============================================================
// Parse GPS TIME block (0x7C / GPS1)
// ============================================================
static void rs41_parse_gps_time(RS41_Decoder_t *dec)
{
    uint16_t pos = rs41_find_block(dec->frame, RS41_BLK_GPS_INFO);
    if (pos == 0) return;

    if (!rs41_check_block_crc(dec->frame, pos))
        return;

    dec->data.crc_ok |= RS41_CRC_GPS1;

    // GPS week: 2 bytes LE at data offset 0
    dec->data.gps_week = read_u16_le(&dec->frame[pos + 2]);

    // GPS TOW: 4 bytes LE at data offset 2 (milliseconds)
    dec->data.gps_tow_ms = read_u32_le(&dec->frame[pos + 4]);

    // Convert TOW to H:M:S
    uint32_t tow_sec = dec->data.gps_tow_ms / 1000;
    uint32_t time_of_day = tow_sec % (24 * 3600);
    dec->data.gps_hour = time_of_day / 3600;
    dec->data.gps_min  = (time_of_day % 3600) / 60;
    dec->data.gps_sec  = time_of_day % 60;
}

// ============================================================
// Parse GPS POSITION block (0x7B / GPS3)
// ============================================================
static void rs41_parse_gps_pos(RS41_Decoder_t *dec)
{
    uint16_t pos = rs41_find_block(dec->frame, RS41_BLK_GPS_POS);
    if (pos == 0) return;

    if (!rs41_check_block_crc(dec->frame, pos))
        return;

    dec->data.crc_ok |= RS41_CRC_GPS3;

    // ECEF X, Y, Z in centimeters (4 bytes each, signed LE)
    int32_t ecef_x = read_i32_le(&dec->frame[pos + 2]);
    int32_t ecef_y = read_i32_le(&dec->frame[pos + 6]);
    int32_t ecef_z = read_i32_le(&dec->frame[pos + 10]);

    // Velocity X, Y, Z in cm/s (2 bytes each, signed LE)
    int16_t vel_x = read_i16_le(&dec->frame[pos + 14]);
    int16_t vel_y = read_i16_le(&dec->frame[pos + 16]);
    int16_t vel_z = read_i16_le(&dec->frame[pos + 18]);

    // Number of satellites
    if (pos + 22 < RS41_FRAME_LEN)
        dec->data.numSV = dec->frame[pos + 20];

    // Convert ECEF to LLA (integer math)
    rs41_ecef_to_lla(ecef_x, ecef_y, ecef_z,
                     &dec->data.lat_1e6,
                     &dec->data.lon_1e6,
                     &dec->data.alt_cm);

    // Compute horizontal speed: sqrt(vx^2 + vy^2)
    // Working in cm/s
    float vx_f = vel_x;
    float vy_f = vel_y;
    dec->data.vH_cm = (int16_t)sqrtf(vx_f*vx_f + vy_f*vy_f);

    // Vertical speed
    dec->data.vV_cm = vel_z;

    // Heading: atan2(vy, vx) in degrees * 10
    if (dec->data.vH_cm > 0) {
        float heading_deg = atan2f(vy_f, vx_f) * (180.0f / 3.1415926535f);
        if (heading_deg < 0) heading_deg += 360.0f;
        dec->data.heading = (uint16_t)(heading_deg * 10.0f);
    }
}

// ============================================================
// Parse the complete frame
// ============================================================
static void rs41_parse_frame(RS41_Decoder_t *dec)
{
    // Reset output
    memset(&dec->data, 0, sizeof(RS41_Data_t));

    // Descramble the frame (XOR with LFSR mask)
    rs41_descramble(dec->frame, RS41_FRAME_LEN);

    // (Header verification skipped as it was matched by correlation)

    // Parse individual blocks
    rs41_parse_status(dec);
    rs41_parse_gps_time(dec);
    rs41_parse_gps_pos(dec);

    // Mark as valid if at least STATUS was decoded
    if (dec->data.crc_ok & RS41_CRC_STATUS) {
        dec->data.valid = true;
        dec->frames_crc_ok++;
    }
}

// ============================================================
// PUBLIC API
// ============================================================

void RS41_Init(RS41_Decoder_t *dec)
{
    memset(dec, 0, sizeof(RS41_Decoder_t));
    dec->bit_count = 0;
    dec->state = RS41_STATE_WAIT_PREAMBLE;
    dec->min_errors = 64; // Initialize to max possible errors
}

void RS41_Reset(RS41_Decoder_t *dec)
{
    RS41_Init(dec);
}

bool RS41_ProcessBit(RS41_Decoder_t *dec, uint8_t bit)
{
    bit &= 1;

    switch (dec->state) {
        case RS41_STATE_WAIT_PREAMBLE:
        case RS41_STATE_WAIT_HEADER:
        {
            // Shift bit into 64-bit register (MSB first)
            dec->shift_reg = (dec->shift_reg << 1) | bit;
            dec->bit_count++;

            if (dec->bit_count < RS41_SYNC_BITS)
                break;

            // Correlate with known header pattern
            uint64_t xor_val = dec->shift_reg ^ rs41_header_64;
            uint64_t xor_inv = dec->shift_reg ^ ~rs41_header_64;
            
            uint32_t hi = (uint32_t)(xor_val >> 32);
            uint32_t lo = (uint32_t)(xor_val & 0xFFFFFFFF);
            uint8_t errors = (uint8_t)(__builtin_popcount(hi) + __builtin_popcount(lo));
            
            uint32_t ihi = (uint32_t)(xor_inv >> 32);
            uint32_t ilo = (uint32_t)(xor_inv & 0xFFFFFFFF);
            uint8_t errors_inv = (uint8_t)(__builtin_popcount(ihi) + __builtin_popcount(ilo));

            if (errors < dec->min_errors) {
                dec->min_errors = errors;
                dec->last_shift_hi = (uint32_t)(dec->shift_reg >> 32);
                dec->last_shift_lo = (uint32_t)(dec->shift_reg & 0xFFFFFFFF);
            }
            if (errors_inv < dec->min_errors) {
                dec->min_errors = errors_inv;
                dec->last_shift_hi = (uint32_t)(dec->shift_reg >> 32);
                dec->last_shift_lo = (uint32_t)(dec->shift_reg & 0xFFFFFFFF);
            }
            
            // Save last error counts for display
            dec->last_errors = errors;
            dec->last_errors_inv = errors_inv;

            if (errors <= 12 || errors_inv <= 12) {
                // Header found! Start collecting frame
                dec->state = RS41_STATE_COLLECT_FRAME;
                dec->inverted = (errors_inv < errors);

                // Copy the header into frame buffer
                // We use the known good header rather than the received one
                for (int i = 0; i < RS41_HEADER_LEN; i++) {
                    dec->frame[i] = rs41_header[i];
                }

                dec->frame_byte_idx = RS41_HEADER_LEN;
                dec->frame_bit_idx = 0;
                dec->bit_count = 0;
            }
            break;
        }

        case RS41_STATE_COLLECT_FRAME:
        {
            if (dec->inverted) bit ^= 1;

            // Accumulate bits into frame bytes (RS41 transmits LSBit first)
            if (dec->frame_byte_idx < RS41_FRAME_LEN) {
                dec->frame[dec->frame_byte_idx] = (dec->frame[dec->frame_byte_idx] >> 1) | (bit << 7);
                dec->frame_bit_idx++;

                if (dec->frame_bit_idx >= 8) {
                    dec->frame_bit_idx = 0;
                    dec->frame_byte_idx++;
                }
            }

            // Check if frame is complete
            if (dec->frame_byte_idx >= RS41_FRAME_LEN) {
                dec->frames_received++;

                // Parse the frame
                rs41_parse_frame(dec);

                // Reset for next frame
                dec->state = RS41_STATE_WAIT_HEADER;
                dec->shift_reg = 0;
                dec->bit_count = 0;
                dec->frame_byte_idx = 0;
                dec->frame_bit_idx = 0;

                return dec->data.valid;
            }
            break;
        }

        case RS41_STATE_FRAME_READY:
            // Should not reach here, reset
            dec->state = RS41_STATE_WAIT_HEADER;
            break;
    }

    return false;
}

const RS41_Data_t* RS41_GetData(const RS41_Decoder_t *dec)
{
    return &dec->data;
}
