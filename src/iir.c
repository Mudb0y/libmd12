#include <math.h>

#include "internal.h"

#define FIT 7
#define GRID 192
#define TOP 0.92
#define FLOOR 0.0316 /* -30 dB: below this the fit weighs absolute error */
#define PI 3.14159265358979323846

typedef struct { double re, im; } cpx;

static cpx cmul(cpx a, cpx b) { cpx r = {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re}; return r; }
static cpx cdiv(cpx a, cpx b)
{
    double d = b.re * b.re + b.im * b.im;
    cpx r = {(a.re * b.re + a.im * b.im) / d, (a.im * b.re - a.re * b.im) / d};
    return r;
}
static double cabs2(cpx a) { return a.re * a.re + a.im * a.im; }

/* The analogue response at w rad/s, frequencies scaled. */
static cpx analog(const md12_proto *p, double scale, double w)
{
    cpx h = {p->gain, 0.0}, s = {0.0, w / scale};
    int i;
    for (i = 0; i < p->nzero; i++) {
        cpx a = {s.re - p->zero[i][0], s.im - p->zero[i][1]};
        h = cmul(h, a);
        if (p->zero[i][1] > 0.0) {
            cpx b = {s.re - p->zero[i][0], s.im + p->zero[i][1]};
            h = cmul(h, b);
        }
    }
    for (i = 0; i < p->npole; i++) {
        cpx a = {s.re - p->pole[i][0], s.im - p->pole[i][1]};
        h = cdiv(h, a);
        if (p->pole[i][1] > 0.0) {
            cpx b = {s.re - p->pole[i][0], s.im + p->pole[i][1]};
            h = cdiv(h, b);
        }
    }
    return h;
}

/* A polynomial in z^-1 at e^(jw). */
static cpx poly(const double *c, int n, double w)
{
    cpx r = {0.0, 0.0};
    int k;
    for (k = 0; k < n; k++) {
        r.re += c[k] * cos(w * k);
        r.im -= c[k] * sin(w * k);
    }
    return r;
}

static void conv(double *c, int *n, double c1, double c2, int order)
{
    int k;
    for (k = *n + order - 1; k >= 0; k--) {
        double v = k < *n ? c[k] : 0.0;
        if (k >= 1 && k - 1 < *n)
            v += c1 * c[k - 1];
        if (order == 2 && k >= 2 && k - 2 < *n)
            v += c2 * c[k - 2];
        c[k] = v;
    }
    *n += order;
}

static int solve(double m[FIT][FIT + 1], double *x)
{
    int c, r, k;
    for (c = 0; c < FIT; c++) {
        int piv = c;
        for (r = c + 1; r < FIT; r++)
            if (fabs(m[r][c]) > fabs(m[piv][c]))
                piv = r;
        if (m[piv][c] == 0.0)
            return -1;
        for (k = 0; k <= FIT; k++) {
            double t = m[c][k];
            m[c][k] = m[piv][k];
            m[piv][k] = t;
        }
        for (r = 0; r < FIT; r++) {
            double f;
            if (r == c)
                continue;
            f = m[r][c] / m[c][c];
            for (k = c; k <= FIT; k++)
                m[r][k] -= f * m[c][k];
        }
    }
    for (c = 0; c < FIT; c++)
        x[c] = m[c][FIT] / m[c][c];
    return 0;
}

static void add_section(md12_iir *f, double *den, int *nden, double a1, double a2, int order)
{
    f->a1[f->ns] = (float)a1;
    f->a2[f->ns] = (float)a2;
    f->ns++;
    conv(den, nden, a1, a2, order);
}

/* A one-pole low-pass at -re rad/s: the pole mapped exactly, and a zero
 * placed so the magnitude is exact at DC and at 18 kHz (or 0.4 of the host
 * rate, if lower), which keeps it within about 0.2 dB below 20 kHz for poles
 * up to and past Nyquist. Returns the numerator's second coefficient over its
 * first. */
static double onepole_zero(double re, double fs)
{
    double fm = 0.4 * fs < 18000.0 ? 0.4 * fs : 18000.0, wt = 2.0 * PI * fm / fs;
    double r = exp(re / fs), mt2 = 1.0 / (1.0 + (wt * fs) * (wt * fs) / (re * re));
    double S = 1.0 - r, a2t = 1.0 - 2.0 * r * cos(wt) + r * r;
    double P = (S * S - mt2 * a2t) / (2.0 * (1.0 - cos(wt))), disc = S * S - 4.0 * P;
    double b0 = (S + sqrt(disc > 0.0 ? disc : 0.0)) / 2.0;
    return (S - b0) / b0;
}

void md12_iir_design(md12_iir *f, const md12_proto *p, double scale, double fs)
{
    double den[2 * MD12_IIR_SECTIONS + 1], zd[MD12_IIR_TAPS], num[MD12_IIR_TAPS];
    double nm[FIT][FIT + 1], fit[FIT], pending = 0.0, T = 1.0 / fs, g;
    int nden = 1, nzd = 1, nnum, i, k, m, have_real = 0, resonant = 0;
    cpx h0;

    for (i = 0; i < MD12_IIR_TAPS; i++)
        f->x[i] = f->b[i] = 0.0f;
    for (i = 0; i < MD12_IIR_SECTIONS; i++)
        f->a1[i] = f->a2[i] = f->y1[i] = f->y2[i] = 0.0f;
    f->ns = 0;
    f->xpos = 0;
    den[0] = 1.0;
    zd[0] = 1.0;
    for (i = 0; i < p->npole; i++)
        if (p->pole[i][1] > 0.0)
            resonant = 1;

    /* Zeros below Nyquist by z = e^(sT), so notches land where they belong. */
    for (i = 0; i < p->nzero; i++) {
        double re = p->zero[i][0] * scale, im = p->zero[i][1] * scale, r = exp(re * T);
        if (im * T >= PI * 0.98)
            continue;
        if (im > 0.0)
            conv(zd, &nzd, -2.0 * r * cos(im * T), r * r, 2);
        else
            conv(zd, &nzd, -r, 0.0, 1);
    }

    /* Poles by z = e^(sT), exact for any frequency; two real poles share a
     * section. */
    for (i = 0; i < p->npole; i++) {
        double re = p->pole[i][0] * scale, im = p->pole[i][1] * scale, r = exp(re * T);
        if (im > 0.0) {
            add_section(f, den, &nden, -2.0 * r * cos(im * T), r * r, 2);
        } else if (!resonant) {
            /* A chain of real poles needs no fit: a low pole alone is exact
             * enough, a high one gets its own zero. */
            add_section(f, den, &nden, -r, 0.0, 1);
            if (-re > 0.2 * PI * fs)
                conv(zd, &nzd, onepole_zero(re, fs), 0.0, 1);
        } else if (!have_real) {
            pending = r;
            have_real = 1;
        } else {
            add_section(f, den, &nden, -(pending + r), pending * r, 2);
            have_real = 0;
        }
    }
    if (have_real)
        add_section(f, den, &nden, -pending, 0.0, 1);

    if (resonant) {
        /* The rest of the numerator by weighted least squares against the
         * analogue response, complex, so phase is matched too. */
        for (i = 0; i < FIT; i++)
            for (k = 0; k <= FIT; k++)
                nm[i][k] = 0.0;
        for (k = 0; k < GRID; k++) {
            double w = TOP * PI * k / (GRID - 1), mag, wt2;
            cpx ha = analog(p, scale, w * fs), a = poly(den, nden, w), z = poly(zd, nzd, w), target;
            /* At an exactly placed zero there is nothing left to fit. */
            if (cabs2(z) < 1e-12)
                continue;
            target = cdiv(cmul(ha, a), z);
            mag = sqrt(cabs2(ha));
            wt2 = cabs2(z) / cabs2(a) / (mag > FLOOR ? mag * mag : FLOOR * FLOOR);
            for (i = 0; i < FIT; i++) {
                for (m = 0; m < FIT; m++)
                    nm[i][m] += wt2 * cos(w * (i - m));
                nm[i][FIT] += wt2 * (target.re * cos(w * i) - target.im * sin(w * i));
            }
        }
        if (solve(nm, fit)) {
            fit[0] = 1.0;
            for (i = 1; i < FIT; i++)
                fit[i] = 0.0;
        }
        nnum = FIT + nzd - 1;
        for (k = 0; k < nnum; k++) {
            num[k] = 0.0;
            for (i = 0; i < FIT; i++)
                if (k - i >= 0 && k - i < nzd)
                    num[k] += fit[i] * zd[k - i];
        }
    } else {
        nnum = nzd;
        for (k = 0; k < nnum; k++)
            num[k] = zd[k];
    }

    /* Exact gain at DC, or at 1 kHz for a filter that blocks DC. */
    h0 = analog(p, scale, 0.0);
    if (cabs2(h0) > 1e-12) {
        double a0 = 0.0, b0 = 0.0;
        for (k = 0; k < nden; k++)
            a0 += den[k];
        for (k = 0; k < nnum; k++)
            b0 += num[k];
        g = h0.re * a0 / b0;
    } else {
        double w = 2.0 * PI * 1000.0 * T;
        g = sqrt(cabs2(analog(p, scale, w * fs)) * cabs2(poly(den, nden, w)) / cabs2(poly(num, nnum, w)));
    }
    f->nb = (unsigned)nnum;
    for (k = 0; k < nnum; k++)
        f->b[k] = (float)(num[k] * g);
}

float md12_iir_run(md12_iir *f, float x)
{
    float acc = 0.0f;
    unsigned k, s;

    f->x[f->xpos] = x;
    for (k = 0; k < f->nb; k++)
        acc += f->b[k] * f->x[(f->xpos - k) & (MD12_IIR_TAPS - 1)];
    f->xpos = (f->xpos + 1) & (MD12_IIR_TAPS - 1);
    for (s = 0; s < f->ns; s++) {
        float y = acc - f->a1[s] * f->y1[s] - f->a2[s] * f->y2[s];
        /* Flush what has decayed to nothing, before it turns denormal. */
        if (y > -1e-20f && y < 1e-20f)
            y = 0.0f;
        f->y2[s] = f->y1[s];
        f->y1[s] = y;
        acc = y;
    }
    return acc;
}
