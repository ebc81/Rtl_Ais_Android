/*
 * Elonics E4000 tuner driver
 *
 * (C) 2011-2012 by Harald Welte <laforge@gnumonks.org>
 * (C) 2012 by Sylvain Munaut <tnt@246tNt.com>
 * (C) 2012 by Hoernchen <la@tfc-server.de>
 *
 * All Rights Reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <reg_field.h>
#include <tuner_e4k.h>
#include <rtlsdr_i2c.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* If this is defined, the limits are somewhat relaxed compared to what the
 * vendor claims is possible */
#define OUT_OF_SPEC

#define MHZ(x)  ((x)*1000*1000)
#define KHZ(x)  ((x)*1000)

enum rtl_sdr_gain_mode {
    GAIN_MODE_AGC=0,
    GAIN_MODE_MANUAL=1,
    GAIN_MODE_LINEARITY=2,
    GAIN_MODE_SENSITIVITY=3
};

#define MAX_E4K_GAIN_MODES 3

uint32_t unsigned_delta(uint32_t a, uint32_t b)
{
    if (a > b)
        return a - b;
    else
        return b - a;
}

/* look-up table bit-width -> mask */
static const uint8_t width2mask[] = {
    0, 1, 3, 7, 0xf, 0x1f, 0x3f, 0x7f, 0xff
};

/***********************************************************************
 * Register Access */

/*! \brief Write a register of the tuner chip
 *  \param[in] e4k reference to the tuner
 *  \param[in] reg number of the register
 *  \param[in] val value to be written
 *  \returns 0 on success, negative in case of error
 */
static int e4k_reg_write(struct e4k_state *e4k, uint8_t reg, uint8_t val)
{
    uint8_t data[2];
    data[0] = reg;
    data[1] = val;

    return rtlsdr_i2c_write_fn(e4k->rtl_dev, e4k->i2c_addr, data, 2);
}

/*! \brief Read a register of the tuner chip
 *  \param[in] e4k reference to the tuner
 *  \param[in] reg number of the register
 *  \returns positive 8bit register contents on success, negative in case of error
 */
static int e4k_reg_read(struct e4k_state *e4k, uint8_t reg)
{
    uint8_t data = reg;

    if (rtlsdr_i2c_write_fn(e4k->rtl_dev, e4k->i2c_addr, &data, 1) < 1)
        return -1;

    if (rtlsdr_i2c_read_fn(e4k->rtl_dev, e4k->i2c_addr, &data, 1) < 1)
        return -1;

    return data;
}

/*! \brief Set or clear some (masked) bits inside a register
 *  \param[in] e4k reference to the tuner
 *  \param[in] reg number of the register
 *  \param[in] mask bit-mask of the value
 *  \param[in] val data value to be written to register
 *  \returns 0 on success, negative in case of error
 */
static int e4k_reg_set_mask(struct e4k_state *e4k, uint8_t reg,
             uint8_t mask, uint8_t val)
{
    uint8_t tmp = e4k_reg_read(e4k, reg);

    if ((tmp & mask) == val)
        return 0;

    return e4k_reg_write(e4k, reg, (tmp & ~mask) | (val & mask));
}

/*! \brief Write a given field inside a register
 *  \param[in] e4k reference to the tuner
 *  \param[in] field structure describing the field
 *  \param[in] val value to be written
 *  \returns 0 on success, negative in case of error
 */
static int e4k_field_write(struct e4k_state *e4k, const struct reg_field *field, uint8_t val)
{
    int rc;
    uint8_t mask;

    rc = e4k_reg_read(e4k, field->reg);
    if (rc < 0)
        return rc;

    mask = width2mask[field->width] << field->shift;

    return e4k_reg_set_mask(e4k, field->reg, mask, val << field->shift);
}

/*! \brief Read a given field inside a register
 *  \param[in] e4k reference to the tuner
 *  \param[in] field structure describing the field
 *  \returns positive value of the field, negative in case of error
 */
static int e4k_field_read(struct e4k_state *e4k, const struct reg_field *field)
{
    int rc;

    rc = e4k_reg_read(e4k, field->reg);
    if (rc < 0)
        return rc;

    rc = (rc >> field->shift) & width2mask[field->width];

    return rc;
}

/***********************************************************************
 * Filter Control */

static const uint32_t rf_filt_center_uhf[] = {
    MHZ(360), MHZ(380), MHZ(405), MHZ(425),
    MHZ(450), MHZ(475), MHZ(505), MHZ(540),
    MHZ(575), MHZ(615), MHZ(670), MHZ(720),
    MHZ(760), MHZ(840), MHZ(890), MHZ(970)
};

static const uint32_t rf_filt_center_l[] = {
    MHZ(1300), MHZ(1320), MHZ(1360), MHZ(1410),
    MHZ(1445), MHZ(1460), MHZ(1490), MHZ(1530),
    MHZ(1560), MHZ(1590), MHZ(1640), MHZ(1660),
    MHZ(1680), MHZ(1700), MHZ(1720), MHZ(1750)
};

static int closest_arr_idx(const uint32_t *arr, unsigned int arr_size, uint32_t freq)
{
    unsigned int i, bi = 0;
    uint32_t best_delta = 0xffffffff;

    /* iterate over the array containing a list of the center
     * frequencies, selecting the closest one */
    for (i = 0; i < arr_size; i++) {
        uint32_t delta = unsigned_delta(freq, arr[i]);
        if (delta < best_delta) {
            best_delta = delta;
            bi = i;
        }
    }

    return bi;
}

/* return 4-bit index as to which RF filter to select */
static int choose_rf_filter(enum e4k_band band, uint32_t freq)
{
    int rc;

    switch (band) {
        case E4K_BAND_VHF2:
        case E4K_BAND_VHF3:
            rc = 0;
            break;
        case E4K_BAND_UHF:
            rc = closest_arr_idx(rf_filt_center_uhf,
                         ARRAY_SIZE(rf_filt_center_uhf),
                         freq);
            break;
        case E4K_BAND_L:
            rc = closest_arr_idx(rf_filt_center_l,
                         ARRAY_SIZE(rf_filt_center_l),
                         freq);
            break;
        default:
            rc = -EINVAL;
            break;
    }

    return rc;
}

/* \brief Automatically select apropriate RF filter based on e4k state */
int e4k_rf_filter_set(struct e4k_state *e4k)
{
    int rc;

    rc = choose_rf_filter(e4k->band, e4k->vco.flo);
    if (rc < 0)
        return rc;

    return e4k_reg_set_mask(e4k, E4K_REG_FILT1, 0xF, rc);
}

/* Mixer Filter */
static const uint32_t mix_filter_bw[] = {
    KHZ(27000), KHZ(27000), KHZ(27000), KHZ(27000),
    KHZ(27000), KHZ(27000), KHZ(27000), KHZ(27000),
    KHZ(4600), KHZ(4200), KHZ(3800), KHZ(3400),
    KHZ(3300), KHZ(2700), KHZ(2300), KHZ(1900)
};

/* IF RC Filter */
static const uint32_t ifrc_filter_bw[] = {
    KHZ(21400), KHZ(21000), KHZ(17600), KHZ(14700),
    KHZ(12400), KHZ(10600), KHZ(9000), KHZ(7700),
    KHZ(6400), KHZ(5300), KHZ(4400), KHZ(3400),
    KHZ(2600), KHZ(1800), KHZ(1200), KHZ(1000)
};

/* IF Channel Filter */
static const uint32_t ifch_filter_bw[] = {
    KHZ(5500), KHZ(5300), KHZ(5000), KHZ(4800),
    KHZ(4600), KHZ(4400), KHZ(4300), KHZ(4100),
    KHZ(3900), KHZ(3800), KHZ(3700), KHZ(3600),
    KHZ(3400), KHZ(3300), KHZ(3200), KHZ(3100),
    KHZ(3000), KHZ(2950), KHZ(2900), KHZ(2800),
    KHZ(2750), KHZ(2700), KHZ(2600), KHZ(2550),
    KHZ(2500), KHZ(2450), KHZ(2400), KHZ(2300),
    KHZ(2280), KHZ(2240), KHZ(2200), KHZ(2150)
};

static const uint32_t *if_filter_bw[] = {
    mix_filter_bw,
    ifch_filter_bw,
    ifrc_filter_bw,
};

static const uint32_t if_filter_bw_len[] = {
    ARRAY_SIZE(mix_filter_bw),
    ARRAY_SIZE(ifch_filter_bw),
    ARRAY_SIZE(ifrc_filter_bw),
};

static const struct reg_field if_filter_fields[] = {
    {
        E4K_REG_FILT2, 4, 4,
    },
    {
        E4K_REG_FILT3, 0, 5,
    },
    {
        E4K_REG_FILT2, 0, 4,
    }
};

static int find_if_bw(enum e4k_if_filter filter, uint32_t bw)
{
    if (filter >= ARRAY_SIZE(if_filter_bw))
        return -EINVAL;

    return closest_arr_idx(if_filter_bw[filter],
                   if_filter_bw_len[filter], bw);
}

/*! \brief Set the filter band-width of any of the IF filters
 *  \param[in] e4k reference to the tuner chip
 *  \param[in] filter filter to be configured
 *  \param[in] bandwidth bandwidth to be configured
 *  \returns 0 success, negative in case of error
 */
int e4k_if_filter_bw_set(struct e4k_state *e4k, enum e4k_if_filter filter,
                 uint32_t bandwidth)
{
    int bw_idx;
    const struct reg_field *field;

    if (filter >= ARRAY_SIZE(if_filter_bw))
        return -EINVAL;

    bw_idx = find_if_bw(filter, bandwidth);

    field = &if_filter_fields[filter];

    return e4k_field_write(e4k, field, bw_idx);
}

/*! \brief Enables / Disables the channel filter
 *  \param[in] e4k reference to the tuner chip
 *  \param[in] on 1=filter enabled, 0=filter disabled
 *  \returns 0 success, negative errors
 */
int e4k_if_filter_chan_enable(struct e4k_state *e4k, int on)
{
    return e4k_reg_set_mask(e4k, E4K_REG_FILT3, E4K_FILT3_DISABLE,
                            on ? 0 : E4K_FILT3_DISABLE);
}

int e4k_if_filter_bw_get(struct e4k_state *e4k, enum e4k_if_filter filter)
{
    const uint32_t *arr;
    int rc;
    const struct reg_field *field;

    if (filter >= ARRAY_SIZE(if_filter_bw))
        return -EINVAL;

    field = &if_filter_fields[filter];

    rc = e4k_field_read(e4k, field);
    if (rc < 0)
        return rc;

    arr = if_filter_bw[filter];

    return arr[rc];
}


/***********************************************************************
 * Frequency Control */

#define E4K_FVCO_MIN_KHZ    2600000 /* 2.6 GHz */
#define E4K_FVCO_MAX_KHZ    3900000 /* 3.9 GHz */
#define E4K_PLL_Y       65536

#ifdef OUT_OF_SPEC
#define E4K_FLO_MIN_MHZ     50
#define E4K_FLO_MAX_MHZ     2200UL
#else
#define E4K_FLO_MIN_MHZ     64
#define E4K_FLO_MAX_MHZ     1700
#endif

struct pll_settings {
    uint32_t freq;
    uint8_t reg_synth7;
    uint8_t mult;
};

static const struct pll_settings pll_vars[] = {
    {KHZ(72400),    (1 << 3) | 7,   48},
    {KHZ(81200),    (1 << 3) | 6,   40},
    {KHZ(108300),   (1 << 3) | 5,   32},
    {KHZ(162500),   (1 << 3) | 4,   24},
    {KHZ(216600),   (1 << 3) | 3,   16},
    {KHZ(325000),   (1 << 3) | 2,   12},
    {KHZ(350000),   (1 << 3) | 1,   8},
    {KHZ(432000),   (0 << 3) | 3,   8},
    {KHZ(667000),   (0 << 3) | 2,   6},
    {KHZ(1200000),  (0 << 3) | 1,   4}
};

static int is_fvco_valid(uint32_t fvco_z)
{
    /* check if the resulting fosc is valid */
    if (fvco_z/1000 < E4K_FVCO_MIN_KHZ ||
        fvco_z/1000 > E4K_FVCO_MAX_KHZ) {
        fprintf(stderr, "[E4K] Fvco %u invalid\n", fvco_z);
        return 0;
    }

    return 1;
}

static int is_fosc_valid(uint32_t fosc)
{
    if (fosc < MHZ(16) || fosc > MHZ(30)) {
        fprintf(stderr, "[E4K] Fosc %u invalid\n", fosc);
        return 0;
    }

    return 1;
}

static int is_z_valid(uint32_t z)
{
    if (z > 255) {
        fprintf(stderr, "[E4K] Z %u invalid\n", z);
        return 0;
    }

    return 1;
}

/*! \brief Determine if 3-phase mixing shall be used or not */
static int use_3ph_mixing(uint32_t flo)
{
    /* this is a magic number somewhre between VHF and UHF */
    if (flo < MHZ(350))
        return 1;

    return 0;
}

/* \brief compute Fvco based on Fosc, Z and X
 * \returns positive value (Fvco in Hz), 0 in case of error */
static uint64_t compute_fvco(uint32_t f_osc, uint8_t z, uint16_t x)
{
    uint64_t fvco_z, fvco_x, fvco;

    /* We use the following transformation in order to
     * handle the fractional part with integer arithmetic:
     *  Fvco = Fosc * (Z + X/Y) <=> Fvco = Fosc * Z + (Fosc * X)/Y
     * This avoids X/Y = 0.  However, then we would overflow a 32bit
     * integer, as we cannot hold e.g. 26 MHz * 65536 either.
     */
    fvco_z = (uint64_t)f_osc * z;

#if 0
    if (!is_fvco_valid(fvco_z))
        return 0;
#endif

    fvco_x = ((uint64_t)f_osc * x) / E4K_PLL_Y;

    fvco = fvco_z + fvco_x;

    return fvco;
}

static uint32_t compute_flo(uint32_t f_osc, uint8_t z, uint16_t x, uint8_t r)
{
    uint64_t fvco = compute_fvco(f_osc, z, x);
    if (fvco == 0)
        return -EINVAL;

    return fvco / r;
}

static int e4k_band_set(struct e4k_state *e4k, enum e4k_band band)
{
    int rc;

    switch (band) {
    case E4K_BAND_VHF2:
    case E4K_BAND_VHF3:
    case E4K_BAND_UHF:
        e4k_reg_write(e4k, E4K_REG_BIAS, 3);
        break;
    case E4K_BAND_L:
        e4k_reg_write(e4k, E4K_REG_BIAS, 0);
        break;
    }

    /* workaround: if we don't reset this register before writing to it,
     * we get a gap between 325-350 MHz */
    rc = e4k_reg_set_mask(e4k, E4K_REG_SYNTH1, 0x06, 0);
    rc = e4k_reg_set_mask(e4k, E4K_REG_SYNTH1, 0x06, band << 1);
    if (rc >= 0)
        e4k->band = band;

    return rc;
}

/*! \brief Compute PLL parameters for givent target frequency
 *  \param[out] oscp Oscillator parameters, if computation successful
 *  \param[in] fosc Clock input frequency applied to the chip (Hz)
 *  \param[in] intended_flo target tuning frequency (Hz)
 *  \returns actual PLL frequency, as close as possible to intended_flo,
 *       0 in case of error
 */
uint32_t e4k_compute_pll_params(struct e4k_pll_params *oscp, uint32_t fosc, uint32_t intended_flo)
{
    uint32_t i;
    uint8_t r = 2;
    uint64_t intended_fvco, remainder;
    uint64_t z = 0;
    uint32_t x;
    int flo;
    int three_phase_mixing = 0;
    oscp->r_idx = 0;

    if (!is_fosc_valid(fosc))
        return 0;

    for(i = 0; i < ARRAY_SIZE(pll_vars); ++i) {
        if(intended_flo < pll_vars[i].freq) {
            three_phase_mixing = (pll_vars[i].reg_synth7 & 0x08) ? 1 : 0;
            oscp->r_idx = pll_vars[i].reg_synth7;
            r = pll_vars[i].mult;
            break;
        }
    }

    //fprintf(stderr, "[E4K] Fint=%u, R=%u\n", intended_flo, r);

    /* flo(max) = 1700MHz, R(max) = 48, we need 64bit! */
    intended_fvco = (uint64_t)intended_flo * r;

    /* compute integral component of multiplier */
    z = intended_fvco / fosc;

    /* compute fractional part.  this will not overflow,
    * as fosc(max) = 30MHz and z(max) = 255 */
    remainder = intended_fvco - (fosc * z);
    /* remainder(max) = 30MHz, E4K_PLL_Y = 65536 -> 64bit! */
    x = (remainder * E4K_PLL_Y) / fosc;
    /* x(max) as result of this computation is 65536 */

    flo = compute_flo(fosc, z, x, r);

    oscp->fosc = fosc;
    oscp->flo = flo;
    oscp->intended_flo = intended_flo;
    oscp->r = r;
//  oscp->r_idx = pll_vars[i].reg_synth7 & 0x0;
    oscp->threephase = three_phase_mixing;
    oscp->x = x;
    oscp->z = z;

    return flo;
}

int e4k_tune_params(struct e4k_state *e4k, struct e4k_pll_params *p)
{
    /* program R + 3phase/2phase */
    e4k_reg_write(e4k, E4K_REG_SYNTH7, p->r_idx);
    /* program Z */
    e4k_reg_write(e4k, E4K_REG_SYNTH3, p->z);
    /* program X */
    e4k_reg_write(e4k, E4K_REG_SYNTH4, p->x & 0xff);
    e4k_reg_write(e4k, E4K_REG_SYNTH5, p->x >> 8);

    /* we're in auto calibration mode, so there's no need to trigger it */

    memcpy(&e4k->vco, p, sizeof(e4k->vco));

    /* set the band */
    if (e4k->vco.flo < MHZ(140))
        e4k_band_set(e4k, E4K_BAND_VHF2);
    else if (e4k->vco.flo < MHZ(350))
        e4k_band_set(e4k, E4K_BAND_VHF3);
    else if (e4k->vco.flo < MHZ(1135))
        e4k_band_set(e4k, E4K_BAND_UHF);
    else
        e4k_band_set(e4k, E4K_BAND_L);

    /* select and set proper RF filter */
    e4k_rf_filter_set(e4k);

    return e4k->vco.flo;
}

/*! \brief High-level tuning API, just specify frquency
 *
 *  This function will compute matching PLL parameters, program them into the
 *  hardware and set the band as well as RF filter.
 *
 *  \param[in] e4k reference to tuner
 *  \param[in] freq frequency in Hz
 *  \returns actual tuned frequency, negative in case of error
 */
int e4k_tune_freq(struct e4k_state *e4k, uint32_t freq)
{
    uint32_t rc;
    struct e4k_pll_params p;

    /* determine PLL parameters */
    rc = e4k_compute_pll_params(&p, e4k->vco.fosc, freq);
    if (!rc)
        return -EINVAL;

    /* actually tune to those parameters */
    rc = e4k_tune_params(e4k, &p);

    /* check PLL lock */
    rc = e4k_reg_read(e4k, E4K_REG_SYNTH1);
    if (!(rc & 0x01)) {
        fprintf(stderr, "[E4K] PLL not locked for %u Hz!\n", freq);
        return -1;
    }

    return 0;
}

/***********************************************************************
 * Gain Control */

static const int8_t if_stage1_gain[] = {
    -3, 6
};

static const int8_t if_stage23_gain[] = {
    0, 3, 6, 9
};

static const int8_t if_stage4_gain[] = {
    0, 1, 2, 3
};

static const int8_t if_stage56_gain[] = {
    3, 6, 9, 12, 15, 15, 15, 15
};

static const int8_t *if_stage_gain[] = {
    0,
    if_stage1_gain,
    if_stage23_gain,
    if_stage23_gain,
    if_stage4_gain,
    if_stage56_gain,
    if_stage56_gain
};

static const uint8_t if_stage_gain_len[] = {
    0,
    ARRAY_SIZE(if_stage1_gain),
    ARRAY_SIZE(if_stage23_gain),
    ARRAY_SIZE(if_stage23_gain),
    ARRAY_SIZE(if_stage4_gain),
    ARRAY_SIZE(if_stage56_gain),
    ARRAY_SIZE(if_stage56_gain)
};

static const struct reg_field if_stage_gain_regs[] = {
    { 0, 0, 0 },
    { E4K_REG_GAIN3, 0, 1 },
    { E4K_REG_GAIN3, 1, 2 },
    { E4K_REG_GAIN3, 3, 2 },
    { E4K_REG_GAIN3, 5, 2 },
    { E4K_REG_GAIN4, 0, 3 },
    { E4K_REG_GAIN4, 3, 3 }
};

static const int32_t lnagain[] = {
    -50,    0,
    -25,    1,
    0,  4,
    25, 5,
    50, 6,
    75, 7,
    100,    8,
    125,    9,
    150,    10,
    175,    11,
    200,    12,
    250,    13,
    300,    14,
};

static const int32_t enhgain[] = {
    10, 30, 50, 70
};

int e4k_set_lna_gain(struct e4k_state *e4k, int32_t gain)
{
    uint32_t i;
    for(i = 0; i < ARRAY_SIZE(lnagain)/2; ++i) {
        if(lnagain[i*2] == gain) {
            e4k_reg_set_mask(e4k, E4K_REG_GAIN1, 0xf, lnagain[i*2+1]);
            return 0;
        }
    }
    return -EINVAL;
}

static const struct gain_table_mode_struct {
    int32_t gain;
    int32_t LNA_gain;
    int32_t mixer_gain;
    int32_t IF_gain[6];
    /* data sheet seems wrong, LNA gain "30dB" is 25dB and mixer gain
       4dB is more 5dB */
} gain_table_mode_linearity[] = {         /* LNA Mixer IF  Total */
    {-250, -5,  4, { -3, 0, 0, 2, 9, 6}},   /* -5    5  12     14  */
    {-200, -5,  4, { -3, 0, 0, 1, 12, 9}},  /* -5    5  19     19  */
    {-150, -5,  4, { -3, 6, 0, 0, 12, 9}},  /* -5    5  24     24  */
    {-100, -5, 12, { -3, 3, 0, 1, 12, 9}},  /* -5   12  22     29  */
     {-50, -5, 12, { -3, 6, 0, 0, 12, 12}}, /* -5   12  27     34  */
      {0,  0, 12, { -3, 6, 0, 0, 12, 12}}, /*  0   12  27     39  */
      {50,  5, 12, { -3, 6, 0, 0, 12, 12}}, /*  5   12  27     44  */
     {100, 10, 12, { -3, 6, 0, 0, 12, 12}}, /* 10   12  27     49  */
     {150, 15, 12, { -3, 6, 0, 0, 12, 12}}, /* 15   12  27     54  */
     {200, 20, 12, { -3, 6, 0, 0, 12, 12}}, /* 20   12  27     59  */
     {250, 30, 12, { -3, 6, 0, 0, 12, 12}}, /* 25   12  27     64  */
},
gain_table_mode_sensitivity[] = {         /* LNA Mixer IF  Total */
    {-250,  5,  4, { -3, 0, 0, 1, 6, 3}},   /*  5    5   7     17  */
    {-200, 10,  4, { -3, 0, 0, 1, 6, 3}},   /* 10    5   7     22  */
    {-150, 15,  4, { -3, 0, 0, 1, 6, 3}},   /* 15    5   7     27  */
    {-100, 20,  4, { -3, 0, 0, 1, 6, 3}},   /* 20    5   7     32  */
     {-50, 30,  4, { -3, 0, 0, 1, 6, 3}},   /* 25    5   7     37  */
       {0, 30, 12, { -3, 0, 0, 2, 3, 3}},   /* 25   12   5     42  */
      {50, 30, 12, { -3, 3, 0, 1, 6, 3}},   /* 25   12  10     47  */
     {100, 30, 12, {  6, 0, 0, 0, 6, 3}},   /* 25   12  15     52  */
     {150, 30, 12, {  6, 0, 0, 2, 9, 3}},   /* 25   12  20     57  */
     {200, 30, 12, {  6, 3, 0, 1, 9, 6}},   /* 25   12  25     62  */
     {250, 30, 12, {  6, 6, 0, 0, 9, 9}},   /* 25   12  30     67  */
};

static int tuner_gain_table_linearity[ARRAY_SIZE(gain_table_mode_linearity)];
static int tuner_gain_table_sensitivity[ARRAY_SIZE(gain_table_mode_sensitivity)];

int e4k_set_lna_mixer_if_gain(struct e4k_state *e4k, int32_t gain)
{
    uint32_t i;
    const struct gain_table_mode_struct * gain_table;
    unsigned int gain_table_len;

    if (e4k->gain_mode == GAIN_MODE_LINEARITY ) {
        gain_table = gain_table_mode_linearity;
        gain_table_len = ARRAY_SIZE(gain_table_mode_linearity);
    } else if (e4k->gain_mode == GAIN_MODE_SENSITIVITY) {
        gain_table = gain_table_mode_sensitivity;
        gain_table_len = ARRAY_SIZE(gain_table_mode_sensitivity);
    } else
        return -EINVAL;

    for(i = 0; i < gain_table_len; ++i) {
        if (gain_table->gain == gain) {
            int l;
            e4k_set_lna_gain(e4k, gain_table->LNA_gain*10);
            e4k_mixer_gain_set(e4k, gain_table->mixer_gain);
            for (l=0; l<6; l++)
                e4k_if_gain_set(e4k, l+1, gain_table->IF_gain[l]);

            return gain;
        }
        gain_table++;
    }
    return -EINVAL;
}

int e4k_set_enh_gain(struct e4k_state *e4k, int32_t gain)
{
    uint32_t i;
    for(i = 0; i < ARRAY_SIZE(enhgain); ++i) {
        if(enhgain[i] == gain) {
            e4k_reg_set_mask(e4k, E4K_REG_AGC11, 0x7, E4K_AGC11_LNA_GAIN_ENH | (i << 1));
            return gain;
        }
    }
    e4k_reg_set_mask(e4k, E4K_REG_AGC11, 0x7, 0);

    /* special case: 0 = off*/
    if(0 == gain)
        return 0;
    else
        return -EINVAL;
}

static void compute_gain_table(struct e4k_state *e4k)
{
    unsigned int i;

    for (i=0; i<ARRAY_SIZE(gain_table_mode_linearity); i++)
        tuner_gain_table_linearity[i] = gain_table_mode_linearity[i].gain;
    for (i=0; i<ARRAY_SIZE(gain_table_mode_sensitivity); i++)
        tuner_gain_table_sensitivity[i] = gain_table_mode_sensitivity[i].gain;
}

int e4k_get_tuner_gains(struct e4k_state *e4k, const int **ptr, int *len)
{
    const int e4k_gains[] = { -10, 15, 40, 65, 90, 115, 140, 165, 190, 215,
                  240, 290, 340, 420 };
    /* Add standard gains */
    const int e4k_std_gains[] = { -250, -200, -150, -100, -50, 0, 50,
                  100, 150, 200, 250};

    if (!len & !ptr)
        return -1;

    switch (e4k->gain_mode) {
        case GAIN_MODE_MANUAL:
            *ptr = e4k_gains;
            *len = sizeof(e4k_gains);
            return 0;
        case GAIN_MODE_LINEARITY:
            *ptr = tuner_gain_table_linearity;
            *len = sizeof(tuner_gain_table_linearity);
            return 0;
        case GAIN_MODE_SENSITIVITY:
            *ptr = tuner_gain_table_sensitivity;
            *len = sizeof(tuner_gain_table_sensitivity);
            return 0;
    }
    return -1;
}

int e4k_enable_manual_gain(struct e4k_state *e4k, uint8_t mode)
{
    if (mode) {
        /* Set LNA mode to manual */
        e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK, E4K_AGC_MOD_SERIAL);

        /* Set Mixer Gain Control to manual */
        e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 0);
        /* Add a flag for more gain modes and return it
           so we know the library has this feature. */
        e4k->gain_mode = mode;
        if (e4k->gain_mode > MAX_E4K_GAIN_MODES)
            e4k->gain_mode = MAX_E4K_GAIN_MODES;
        if (e4k->gain_mode == GAIN_MODE_MANUAL)
            return 0; /* compatibility to old mode API */
        return e4k->gain_mode;
    } else {
        e4k->gain_mode=GAIN_MODE_AGC;
        /* Set LNA mode to auto */
        e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK, E4K_AGC_MOD_IF_SERIAL_LNA_AUTON);
        /* Set Mixer Gain Control to auto */
        e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 1);

        e4k_reg_set_mask(e4k, E4K_REG_AGC11, 0x7, 0);
    }

    return 0;
}

static int find_stage_gain(uint8_t stage, int8_t val)
{
    const int8_t *arr;
    int i;

    if (stage >= ARRAY_SIZE(if_stage_gain))
        return -EINVAL;

    arr = if_stage_gain[stage];

    for (i = 0; i < if_stage_gain_len[stage]; i++) {
        if (arr[i] == val)
            return i;
    }
    return -EINVAL;
}

/*! \brief Set the gain of one of the IF gain stages
 *  \param [e4k] handle to the tuner chip
 *  \param [stage] number of the stage (1..6)
 *  \param [value] gain value in dB
 *  \returns 0 on success, negative in case of error
 */
int e4k_if_gain_set(struct e4k_state *e4k, uint8_t stage, int8_t value)
{
    int rc;
    uint8_t mask;
    const struct reg_field *field;

    rc = find_stage_gain(stage, value);
    if (rc < 0)
        return rc;

    /* compute the bit-mask for the given gain field */
    field = &if_stage_gain_regs[stage];
    mask = width2mask[field->width] << field->shift;

    return e4k_reg_set_mask(e4k, field->reg, mask, rc << field->shift);
}

int e4k_mixer_gain_set(struct e4k_state *e4k, int8_t value)
{
    uint8_t bit;

    switch (value) {
    case 4:
        bit = 0;
        break;
    case 12:
        bit = 1;
        break;
    default:
        return -EINVAL;
    }

    return e4k_reg_set_mask(e4k, E4K_REG_GAIN2, 1, bit);
}

int e4k_get_tuner_stage_gains(struct e4k_state *e4k, uint8_t stage, const int32_t **gains, const char **description)
{
    switch(stage) {
    case 0: {
        static const char LNA_desc[] = "LNA";
        static const int32_t LNA_stage[] = { -50, -25,  0, 25, 50, 75, 100, 125, 150, 175, 200, 250, 300 };

        *gains = LNA_stage;
        *description = LNA_desc;
        return ARRAY_SIZE(LNA_stage);
    }
    case 1: {
        static const char Mixer_desc[] = "Mixer";
        static const int32_t Mixer_stage[] = { 40, 120 };

        *gains = Mixer_stage;
        *description = Mixer_desc;
        return ARRAY_SIZE(Mixer_stage);
    }
    case 2: {
        static const char IF1_desc[] = "IF1";
        static const int32_t IF1_stage[] = { -30, 60 };

        *gains = IF1_stage;
        *description = IF1_desc;
        return ARRAY_SIZE(IF1_stage);
    }
    case 3: {
        static const char IF2_desc[] = "IF2";
        static const int32_t IF2_stage[] = { 0, 30, 60, 90 };

        *gains = IF2_stage;
        *description = IF2_desc;
        return ARRAY_SIZE(IF2_stage);
    }
    case 4: {
        static const char IF3_desc[] = "IF3";
        static const int32_t IF3_stage[] = { 0, 30, 60, 90 };

        *gains = IF3_stage;
        *description = IF3_desc;
        return ARRAY_SIZE(IF3_stage);
    }
    case 5: {
        static const char IF4_desc[] = "IF4";
        static const int32_t IF4_stage[] = { 0, 10, 20, 30 };

        *gains = IF4_stage;
        *description = IF4_desc;
        return ARRAY_SIZE(IF4_stage);
    }
    case 6: {
        static const char IF5_desc[] = "IF5";
        static const int32_t IF5_stage[] = { 30, 60, 90, 120, 150 };

        *gains = IF5_stage;
        *description = IF5_desc;
        return ARRAY_SIZE(IF5_stage);
    }
    case 7: {
        static const char IF6_desc[] = "IF6";
        static const int32_t IF6_stage[] = { 30, 60, 90, 120, 150 };

        *gains = IF6_stage;
        *description = IF6_desc;
        return ARRAY_SIZE(IF6_stage);
    }
    }
    return 0;
}

int e4k_set_tuner_stage_gain(struct e4k_state *e4k, uint8_t stage, int32_t gain)
{
    if (stage==0)
        return e4k_set_lna_gain(e4k, gain);
    else if (stage==1)
        return e4k_mixer_gain_set(e4k, gain/10);
    else
        return e4k_if_gain_set(e4k, stage-2, gain);
}

int e4k_commonmode_set(struct e4k_state *e4k, int8_t value)
{
    if(value < 0)
        return -EINVAL;
    else if(value > 7)
        return -EINVAL;

    return e4k_reg_set_mask(e4k, E4K_REG_DC7, 7, value);
}

/***********************************************************************
 * DC Offset */

int e4k_manual_dc_offset(struct e4k_state *e4k, int8_t iofs, int8_t irange, int8_t qofs, int8_t qrange)
{
    int res;

    if((iofs < 0x00) || (iofs > 0x3f))
        return -EINVAL;
    if((irange < 0x00) || (irange > 0x03))
        return -EINVAL;
    if((qofs < 0x00) || (qofs > 0x3f))
        return -EINVAL;
    if((qrange < 0x00) || (qrange > 0x03))
        return -EINVAL;

    res = e4k_reg_set_mask(e4k, E4K_REG_DC2, 0x3f, iofs);
    if(res < 0)
        return res;

    res = e4k_reg_set_mask(e4k, E4K_REG_DC3, 0x3f, qofs);
    if(res < 0)
        return res;

    res = e4k_reg_set_mask(e4k, E4K_REG_DC4, 0x33, (qrange << 4) | irange);
    return res;
}

/*! \brief Perform a DC offset calibration right now
 *  \param [e4k] handle to the tuner chip
 */
int e4k_dc_offset_calibrate(struct e4k_state *e4k)
{
    /* make sure the DC range detector is enabled */
    e4k_reg_set_mask(e4k, E4K_REG_DC5, E4K_DC5_RANGE_DET_EN, E4K_DC5_RANGE_DET_EN);

    return e4k_reg_write(e4k, E4K_REG_DC1, 0x01);
}


static const int8_t if_gains_max[] = {
    0, 6, 9, 9, 2, 15, 15
};

struct gain_comb {
    int8_t mixer_gain;
    int8_t if1_gain;
    uint8_t reg;
};

static const struct gain_comb dc_gain_comb[] = {
    { 4,  -3, 0x50 },
    { 4,   6, 0x51 },
    { 12, -3, 0x52 },
    { 12,  6, 0x53 },
};

#define TO_LUT(offset, range)   (offset | (range << 6))

int e4k_dc_offset_gen_table(struct e4k_state *e4k)
{
    uint32_t i;

    /* FIXME: read ont current gain values and write them back
     * before returning to the caller */

    /* disable auto mixer gain */
    e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 0);

    /* set LNA/IF gain to full manual */
    e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK,
             E4K_AGC_MOD_SERIAL);

    /* set all 'other' gains to maximum */
    for (i = 2; i <= 6; i++)
        e4k_if_gain_set(e4k, i, if_gains_max[i]);

    /* iterate over all mixer + if_stage_1 gain combinations */
    for (i = 0; i < ARRAY_SIZE(dc_gain_comb); i++) {
        uint8_t offs_i, offs_q, range, range_i, range_q;

        /* set the combination of mixer / if1 gain */
        e4k_mixer_gain_set(e4k, dc_gain_comb[i].mixer_gain);
        e4k_if_gain_set(e4k, 1, dc_gain_comb[i].if1_gain);

        /* perform actual calibration */
        e4k_dc_offset_calibrate(e4k);

        /* extract I/Q offset and range values */
        offs_i = e4k_reg_read(e4k, E4K_REG_DC2) & 0x3f;
        offs_q = e4k_reg_read(e4k, E4K_REG_DC3) & 0x3f;
        range  = e4k_reg_read(e4k, E4K_REG_DC4);
        range_i = range & 0x3;
        range_q = (range >> 4) & 0x3;

        fprintf(stderr, "[E4K] Table %u I=%u/%u, Q=%u/%u\n",
            i, range_i, offs_i, range_q, offs_q);

        /* write into the table */
        e4k_reg_write(e4k, dc_gain_comb[i].reg,
                  TO_LUT(offs_q, range_q));
        e4k_reg_write(e4k, dc_gain_comb[i].reg + 0x10,
                  TO_LUT(offs_i, range_i));
    }

    return 0;
}

/***********************************************************************
 * Standby */

/*! \brief Enable/disable standby mode
 */
int e4k_standby(struct e4k_state *e4k, int enable)
{
    e4k_reg_set_mask(e4k, E4K_REG_MASTER1, E4K_MASTER1_NORM_STBY,
             enable ? 0 : E4K_MASTER1_NORM_STBY);

    return 0;
}

/***********************************************************************
 * Initialization */

static int magic_init(struct e4k_state *e4k)
{
    e4k_reg_write(e4k, 0x7e, 0x01);
    e4k_reg_write(e4k, 0x7f, 0xfe);
    e4k_reg_write(e4k, 0x82, 0x00); /* ? not in data sheet */
    e4k_reg_write(e4k, 0x86, 0x50); /* polarity A */
    e4k_reg_write(e4k, 0x87, 0x20); /* configure mixer */
    e4k_reg_write(e4k, 0x88, 0x01); /* configure mixer */
    e4k_reg_write(e4k, 0x9f, 0x7f); /* configure LNA */
    e4k_reg_write(e4k, 0xa0, 0x07); /* configure LNA */

    return 0;
}

/*! \brief Initialize the E4K tuner
 */
int e4k_init(struct e4k_state *e4k)
{
    /* make a dummy i2c read or write command, will not be ACKed! */
    e4k_reg_read(e4k, 0);

    /* Make sure we reset everything and clear POR indicator */
    e4k_reg_write(e4k, E4K_REG_MASTER1,
        E4K_MASTER1_RESET |
        E4K_MASTER1_NORM_STBY |
        E4K_MASTER1_POR_DET
    );

    /* Configure clock input */
    e4k_reg_write(e4k, E4K_REG_CLK_INP, 0x00);

    /* Disable clock output */
    e4k_reg_write(e4k, E4K_REG_REF_CLK, 0x00);
    e4k_reg_write(e4k, E4K_REG_CLKOUT_PWDN, 0x96);

    /* Write some magic values into registers */
    magic_init(e4k);
#if 0
    /* Set common mode voltage a bit higher for more margin 850 mv */
    e4k_commonmode_set(e4k, 4);

    /* Initialize DC offset lookup tables */
    e4k_dc_offset_gen_table(e4k);

    /* Enable time variant DC correction */
    e4k_reg_write(e4k, E4K_REG_DCTIME1, 0x01);
    e4k_reg_write(e4k, E4K_REG_DCTIME2, 0x01);
#endif

    /* Set LNA mode to manual */
    e4k_reg_write(e4k, E4K_REG_AGC4, 0x10); /* High threshold */
    e4k_reg_write(e4k, E4K_REG_AGC5, 0x04); /* Low threshold */
    e4k_reg_write(e4k, E4K_REG_AGC6, 0x1a); /* LNA calib + loop rate */

    e4k_reg_set_mask(e4k, E4K_REG_AGC1, E4K_AGC1_MOD_MASK,
        E4K_AGC_MOD_SERIAL);

    /* Set Mixer Gain Control to manual */
    e4k_reg_set_mask(e4k, E4K_REG_AGC7, E4K_AGC7_MIX_GAIN_AUTO, 0);

#if 0
    /* Enable LNA Gain enhancement */
    e4k_reg_set_mask(e4k, E4K_REG_AGC11, 0x7,
             E4K_AGC11_LNA_GAIN_ENH | (2 << 1));

    /* Enable automatic IF gain mode switching */
    e4k_reg_set_mask(e4k, E4K_REG_AGC8, 0x1, E4K_AGC8_SENS_LIN_AUTO);
#endif

    /* Use auto-gain as default */
    e4k_enable_manual_gain(e4k, 0);

    /* Select moderate gain levels */
    e4k_if_gain_set(e4k, 1, 6);
    e4k_if_gain_set(e4k, 2, 0);
    e4k_if_gain_set(e4k, 3, 0);
    e4k_if_gain_set(e4k, 4, 0);
    e4k_if_gain_set(e4k, 5, 9);
    e4k_if_gain_set(e4k, 6, 9);

    /* Set the most narrow filter we can possibly use */
    e4k_if_filter_bw_set(e4k, E4K_IF_FILTER_MIX, KHZ(1900));
    e4k_if_filter_bw_set(e4k, E4K_IF_FILTER_RC, KHZ(1000));
    e4k_if_filter_bw_set(e4k, E4K_IF_FILTER_CHAN, KHZ(2150));
    e4k_if_filter_chan_enable(e4k, 1);

    /* Disable time variant DC correction and LUT */
    e4k_reg_set_mask(e4k, E4K_REG_DC5, 0x03, 0);
    e4k_reg_set_mask(e4k, E4K_REG_DCTIME1, 0x03, 0);
    e4k_reg_set_mask(e4k, E4K_REG_DCTIME2, 0x03, 0);

    /* Compute the gain tables */
    compute_gain_table(e4k);

    return 0;
}
