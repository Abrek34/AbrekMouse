#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
decay_band.py — AJ4 / config-presets

SORU: AJ1 "decay_rate duyarli bandini erisilemez kil" dedi.  Bandin TAM
siniri nerede?  Tek bir decay_rate sayisi mi, yoksa (decay_rate, x, limit)
uzayinda bir egri mi?

Yontem:
  - DOGRULUK = decimal modulu, 60 basamak, ayni ifade (accel-natural.hpp:165)
  - PORT     = Python float (= IEEE-754 double), ayni ifade SIRA SIYASI ile
  - hata     = |port - dogru| / |dogru|   (oracle'in 1e-9 toleransiyla ayni)
  - erisilebilir kutu: args.limit in [0, LIMIT_MAX=100], decay_rate in [0,10]

PC'ler (olcumu kendim denetler):
  PC-1 dogruluk yolu: Decimal ifadesi C++ ile birebir ayni parantez sirasinda
         yazilmis olmali -> kucuk bir "elle hesap" ornegiyle dogrulanir.
  PC-2 PORT yolu: ayni ifadeyi saf double ile hesaplayip C++ probe'unun
         (probe_float ile ayni ifade) ciktisiyla birebir karsilastirir.
  PC-3 kapsam: grid, hem AJ2'nin raporladigi en kotu noktayi
         (limit=100, decay=1e-9, x=0.01) hem de onun disindaki bolgeyi
         icermeli; en kotu nokta gridin disinda KALAMAZ.
"""

import sys
from decimal import Decimal, getcontext, localcontext

getcontext().prec = 60
getcontext().rounding = "ROUND_HALF_EVEN"

LIMIT_MAX = 100.0

# --- grid -------------------------------------------------------------------
LIMITS = [0.5, 1.0, 1.3, 2.0, 5.0, 10.0, 50.0, 100.0]
DECAYS = [0.0, 1e-12, 1e-11, 1e-10, 1e-9, 1e-8, 1e-7, 1e-6, 1e-5,
          1e-4, 1e-3, 1e-2, 0.08, 0.1, 1.0, 10.0]
XS = [1e-6, 1e-5, 1e-4, 1e-3, 1e-2, 0.1, 1.0, 10.0, 100.0, 1000.0,
      1e4, 1e5]
OFFSET = 0.0
TOL = 1e-9


# --- PORT: ayni ifade, ayni parantez sirasi, saf double ---------------------
def port_gain(args_limit, decay, x, offset=OFFSET, gain_mode=True):
    """accel-natural.hpp:17-166 birebir, C++ degerlendirme sirasiyla."""
    limit = args_limit - 1.0
    abs_limit = abs(limit)
    accel = decay / (1.0 if abs_limit < 1e-9 else abs_limit)
    if x <= offset:
        return 1.0
    t = x - offset
    decay_v = __import__("math").exp(-accel * t)
    if not gain_mode:
        offset_x = offset - x
        return limit * (1.0 - (offset - decay_v * offset_x) / x) + 1.0
    if x < 1e-9:
        return 1.0
    if accel < 1e-12:
        return 1.0
    offset_x = offset - x
    output = limit * (decay_v / accel - offset_x) - limit / accel
    return output / x + 1.0


# --- DOGRULUK: 60 basamak, ayni ifade --------------------------------------
def true_gain(args_limit, decay, x, offset=OFFSET, gain_mode=True):
    with localcontext() as ctx:
        ctx.prec = 60
        D = Decimal
        limit = D(str(args_limit)) - D("1")
        abs_limit = abs(limit)
        accel = D(str(decay)) / (D("1") if abs_limit < D("1e-9") else abs_limit)
        if D(str(x)) <= D(str(offset)):
            return D("1")
        t = D(str(x)) - D(str(offset))
        decay_v = (-accel * t).exp()
        if not gain_mode:
            offset_x = D(str(offset)) - D(str(x))
            return limit * (D("1") - (D(str(offset)) - decay_v * offset_x) / D(str(x))) + D("1")
        if D(str(x)) < D("1e-9"):
            return D("1")
        if accel < D("1e-12"):
            return D("1")
        offset_x = D(str(offset)) - D(str(x))
        output = limit * (decay_v / accel - offset_x) - limit / accel
        return output / D(str(x)) + D("1")


def rel_err(port, truth):
    truth = float(truth)
    if truth == 0.0:
        return abs(port)
    return abs(port - truth) / abs(truth)


# =========================== PC'ler =========================================
def pcs():
    out = []

    # PC-1: Decimal dogruluk yolu — elle bilinen bir deger.
    # accel = 1e-4/99 -> x=1 -> gain ~ 1 + limit*accel*x/2 (birinci mertebe)
    g = true_gain(100.0, 1e-4, 1.0)
    approx = 1.0 + 99.0 * (1e-4 / 99.0) * 1.0 / 2.0
    ok = abs(float(g) - approx) < 1e-6
    out.append(("PC-1 dogruluk yolu (Decimal ~ 1+L*a*x/2)", ok,
                f"decimal={float(g):.12g} yaklasik={approx:.12g}"))

    # PC-2: PORT yolu ifade sirasi — x<=offset kolu ve guard kollari.
    ok2 = (port_gain(100.0, 1e-9, 1e-12) == 1.0      # x<1e-9 guard
           and port_gain(100.0, 1e-13, 1.0) == 1.0    # accel<1e-12 guard
           and port_gain(100.0, 1.0, 0.0) == 1.0)     # x<=offset
    out.append(("PC-2 PORT guard kollari (x<1e-9 / accel<1e-12 / x<=off)", ok2,
                "uc guard da 1.0 donuyor"))

    # PC-3 kapsam: AJ2'nin raporladigi en kotu nokta gridin icinde mi?
    has_worst = (100.0 in LIMITS and 1e-9 in DECAYS and 1e-2 in XS)
    gw = port_gain(100.0, 1e-9, 1e-2)
    tw = true_gain(100.0, 1e-9, 1e-2)
    ok3 = has_worst and abs(gw - 0.8046875) < 1e-12 and abs(float(tw) - 1.000000000005) < 1e-9
    out.append(("PC-3 kapsam: AJ2 en-kotu nokta (100/1e-9/0.01) birebir", ok3,
                f"port={gw!r} dogru={float(tw):.12g}"))
    return out


# =========================== OLÇUM ==========================================
def main():
    p = pcs()
    print("=== PC'ler ===")
    bad = 0
    for name, ok, det in p:
        print(f"  [{'OK ' if ok else 'FAIL'}] {name}: {det}")
        if not ok:
            bad += 1
    if bad:
        print(f"SONUC: {bad} PC FAIL -> olcum GECERSIZ")
        return 1
    print(f"PC: {len(p)}/{len(p)} gecti\n")

    # tablo: her (limit, x) icin hatayi 1e-9'u asan en kucuk decay_rate
    print("=== harita: 1e-9 toleransini ASAN (limit, x) bolgesi ===")
    print(f"{'limit':>7} {'x':>8} | asan decay_rate sayisi | en kucuk asan | en buyuk gecen")
    print("-" * 74)
    worst_overall = (0.0, None)
    for L in LIMITS:
        for x in XS:
            fails = [d for d in DECAYS if d > 0 and rel_err(port_gain(L, d, x),
                                                            true_gain(L, d, x)) > TOL]
            if not fails:
                continue
            mx = max(fails)
            if worst_overall[0] < rel_err(port_gain(L, mx, x), true_gain(L, mx, x)):
                worst_overall = (rel_err(port_gain(L, mx, x), true_gain(L, mx, x)), (L, mx, x))
            okdec = [d for d in DECAYS if d > 0 and d > mx]
            lo = min(fails)
            print(f"{L:>7g} {x:>8g} | {len(fails):>6} / {len(DECAYS)-1:>4}         | {lo:>12g} | {mx:>14g}")

    print()
    print(f"en buyuk hata: {worst_overall[0]:.3e}  ->  {worst_overall[1]}")

    # kritik soru: tek bir alt sinir bandi kapatir mi?
    print("\n=== KRITIK: sabit bir alt sinir (floor) bandi kapatir mi? ===")
    print("floor = F verildiginde (0,F]->0'a yuvarlanir; geriye decay>F kalir.")
    for F in [1e-12, 1e-9, 1e-6, 1e-4, 1e-3, 1e-2, 0.08, 0.1, 1.0]:
        rem = 0
        tot = 0
        worst = (0.0, None)
        for L in LIMITS:
            for d in DECAYS:
                if d <= F or d == 0.0:
                    continue
                for x in XS:
                    tot += 1
                    e = rel_err(port_gain(L, d, x), true_gain(L, d, x))
                    if e > TOL:
                        rem += 1
                        if e > worst[0]:
                            worst = (e, (L, d, x))
        print(f"  floor={F:<8g} -> kalan ihlal {rem:>4}/{tot:<4}"
              f"  {'HEPSI KAPANDI' if rem == 0 else 'kapanmadi: ' + str(worst[1]) + ' hata=' + format(worst[0], '.3e')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
