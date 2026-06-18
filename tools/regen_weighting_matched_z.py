#!/usr/bin/env python3
"""
tools/regen_weighting_matched_z.py — v3.3.0 A/C weighting via matched-z + correction

Generates A/C weighting biquad coefficients targeting IEC 61672-1 CLASS 1 (±0.7 dB)
at fs=48000 Hz using matched-z transform with 2nd-order correction.

STRATEGY: matched-z (pole positions exact) → 2nd-order parametric correction
  Stage 1: matched-z transform (z_p = exp(s_p·T), z_z = exp(s_z·T))
  Stage 2: 2nd-order RBJ peaking EQ, optimized via differential_evolution
  The peaking EQ is centered in 8-16 kHz to correct the matched-z gain shape error.

USAGE:
  python3 tools/regen_weighting_matched_z.py

Author: 蒙特卡洛
Date: 2026-06-18
"""

import sys, numpy as np
import scipy.signal as signal
from scipy.optimize import differential_evolution, minimize

A_POLES_HZ = [20.598997, 107.65265, 737.86223, 12194.217]
A_POLE_MULTS = [2, 1, 1, 2]
C_POLES_HZ = [20.598997, 12194.217]
C_POLE_MULTS = [2, 2]

A_REF = {
    10:-70.4,12.5:-63.4,16:-56.7,20:-50.5,25:-44.7,31.5:-39.4,
    40:-34.6,50:-30.2,63:-26.2,80:-22.5,100:-19.1,125:-16.1,
    160:-13.4,200:-10.9,250:-8.6,315:-6.6,400:-4.8,500:-3.2,
    630:-1.9,800:-0.8,1000:0.0,1250:0.6,1600:1.0,2000:1.2,
    2500:1.3,3150:1.2,4000:1.0,5000:0.5,6300:-0.1,8000:-1.1,
    10000:-2.5,12500:-4.3,16000:-6.6,20000:-9.3,
}
C_REF = {
    10:-14.3,12.5:-11.2,16:-8.5,20:-6.2,25:-4.4,31.5:-3.0,
    40:-2.0,50:-1.3,63:-0.8,80:-0.5,100:-0.3,125:-0.2,
    160:-0.1,200:0.0,250:0.0,315:0.0,400:0.0,500:0.0,
    630:0.0,800:0.0,1000:0.0,1250:0.0,1600:0.0,2000:0.0,
    2500:-0.1,3150:-0.2,4000:-0.3,5000:-0.5,6300:-0.8,8000:-1.1,
    10000:-1.6,12500:-2.3,16000:-3.3,20000:-4.4,
}


def matched_z(z_a, p_a, k_a, fs):
    T=1.0/fs
    z_d=[complex(np.exp(float(zz)*T)) for zz in z_a]
    p_d=[complex(np.exp(float(pp)*T)) for pp in p_a]
    n=len(p_d)-len(z_d)
    if n>0: z_d.extend([0j]*n)
    elif n<0: p_d.extend([0j]*abs(n))
    return z_d,p_d,float(k_a)


def norm_1k(sos, fs):
    w=2*np.pi*1000/fs; _,h=signal.sosfreqz(sos,worN=[w])
    return 1.0/abs(h[0])


def sos_resp_db(sos, freqs, fs):
    w=2*np.pi*freqs/fs; _,h=signal.sosfreqz(sos,worN=w)
    return 20*np.log10(np.abs(h)+1e-30)


def peaking_bq(f0, Q, G_dB, fs):
    """RBJ peaking EQ. Returns biquad with a0=1.0."""
    A=10**(G_dB/40); w0=2*np.pi*f0/fs; alpha=np.sin(w0)/(2*Q)
    b0=1+alpha*A; b1=-2*np.cos(w0); b2=1-alpha*A
    a0=1+alpha/A; a1=-2*np.cos(w0); a2=1-alpha/A
    return np.array([[float(b0/a0),float(b1/a0),float(b2/a0),
                       1.0,float(a1/a0),float(a2/a0)]])


def check(fs, sos, gain, ref, label, C1=0.7, C2=1.5, quiet=False):
    freqs=sorted([f for f in ref if f<fs/2]); fa=np.array(freqs)
    db=sos_resp_db(sos,fa,fs)+20*np.log10(gain)
    rv=np.array([ref[f] for f in fa]); err=db-rv
    mx=float(np.max(np.abs(err)))
    if not quiet:
        print(f"\n{label}  max={mx:.3f}dB  {'✓C1' if mx<=C1 else '⚠C2' if mx<=C2 else '✗'}", file=sys.stderr)
        for f,a,r,e in zip(freqs,db,rv,err):
            m="✓" if abs(e)<=C1 else ("⚠" if abs(e)<=C2 else "✗")
            print(f"  {f:>7.1f}Hz {a:>+7.2f} {r:>+7.2f} {e:>+6.2f}  {m}", file=sys.stderr)
    return err,mx,freqs


def optimize_peaking(sos_base, ref, freqs_arr, fs, C2=1.5):
    """Optimize (f0, Q, G_dB) for a peaking EQ to minimize max error.
    Uses differential_evolution (global) + Nelder-Mead (local refine)."""
    base_db = sos_resp_db(sos_base, freqs_arr, fs)
    ref_vals = np.array([ref[float(f)] for f in freqs_arr])

    def cost(params):
        f0,Q,G=params
        if not(500<f0<20000) or not(0.3<Q<15) or abs(G)>4: return 100.
        bq=peaking_bq(f0,Q,G,fs); bq_db=sos_resp_db(bq,freqs_arr,fs)
        return float(np.max(np.abs(base_db+bq_db-ref_vals)))

    # Global search with differential_evolution
    bounds=[(500,19000),(0.3,15.0),(-4.0,4.0)]
    r_de=differential_evolution(cost,bounds,seed=42,popsize=30,maxiter=150,polish=False)
    # Local refine
    r=minimize(cost,r_de.x,method='Nelder-Mead',
               options={'xatol':1e-3,'fatol':1e-4,'maxiter':2000})
    f0,Q,G=r.x; c=r.fun
    print(f"  peaking: f0={f0:.0f}Hz Q={Q:.2f} G={G:+.3f}dB  cost={c:.4f}dB", file=sys.stderr)
    return peaking_bq(f0,Q,G,fs), c


def optimize_shelf(sos_base, ref, freqs_arr, fs):
    """1st-order shelf (used for C-weighting which is simpler)."""
    base_db=sos_resp_db(sos_base,freqs_arr,fs)
    rv=np.array([ref[float(f)] for f in freqs_arr])
    def cost(params):
        zp,zz,gd=params
        if not(0.001<zp<0.9999) or not(0.001<zz<0.9999) or abs(gd)>6: return 100.
        g=10**(gd/20); w=2*np.pi*freqs_arr/fs; e=np.exp(-1j*w)
        s=20*np.log10(np.abs(g*(1-zz*e)/(1-zp*e))+1e-30)
        return float(np.max(np.abs(base_db+s-rv)))
    best=None
    for zp in [.1,.3,.5,.7,.9]:
        for zz in [.001,.1,.3,.5,.7,.9,.999]:
            for g_ in [-3,-1.5,0,1,2,3,-2,-4,-5]:
                r=minimize(cost,[zp,zz,g_],method='Nelder-Mead',
                           options={'xatol':1e-5,'fatol':1e-4,'maxiter':2000})
                if best is None or r.fun<best.fun: best=r
    zp,zz,gd=best.x; g=10**(gd/20)
    print(f"  shelf: zp={zp:.4f} zz={zz:.4f} g={gd:+.3f}dB cost={best.fun:.4f}dB", file=sys.stderr)
    return np.array([[g,-g*zz,0,1.0,-zp,0]]), best.fun



def emit_cpp(name, sos):
    lines=[f"static constexpr WeightingCoeffs {name}[{sos.shape[0]}] = {{"]
    for r in sos:
        b0,b1,b2,a0,a1,a2=r
        lines.append(f"    {{ {b0/a0:.10e}f, {b1/a0:.10e}f, {b2/a0:.10e}f, {a1/a0:.10e}f, {a2/a0:.10e}f }},")
    lines.append("};")
    return "\n".join(lines)


def main():
    fs=48000; C1,C2=0.7,1.5
    print("="*60,file=sys.stderr)
    print(f"Matched-z + 2nd-order correction  fs={fs}  target Class 1",file=sys.stderr)

    # ===== A-weighting =====
    freqs_a=np.array(sorted([f for f in A_REF if f<fs/2]))
    a_z=[0.0]*4; a_p=[]
    for f,m in zip(A_POLES_HZ,A_POLE_MULTS): a_p.extend([-2*np.pi*f]*m)
    z_d,p_d,k_d=matched_z(a_z,a_p,1.0,fs)
    sos_a1=signal.zpk2sos(z_d,p_d,k_d,pairing='nearest')
    print(f"\n[A] matched-z: {sos_a1.shape[0]} sections",file=sys.stderr)
    gain_a1=norm_1k(sos_a1,fs)
    ea1,ma1,_=check(fs,sos_a1,gain_a1,A_REF,"A(raw mz)",quiet=True)
    print(f"[A] raw matched-z max={ma1:.3f}dB",file=sys.stderr)

    # Stage 2: 2nd-order peaking correction
    if ma1>C1:
        print(f"[A] adding peaking correction...",file=sys.stderr)
        peak_a,cost_a=optimize_peaking(sos_a1,A_REF,freqs_a,fs)
        sos_a=np.vstack([sos_a1,peak_a])
        gain_a=norm_1k(sos_a,fs)
    else:
        sos_a,gain_a=sos_a1,gain_a1
    ea,ma,_=check(fs,sos_a,gain_a,A_REF,"A-FINAL")

    # ===== C-weighting =====
    freqs_c=np.array(sorted([f for f in C_REF if f<fs/2]))
    c_z=[0.0]*2; c_p=[]
    for f,m in zip(C_POLES_HZ,C_POLE_MULTS): c_p.extend([-2*np.pi*f]*m)
    z_d,p_d,k_d=matched_z(c_z,c_p,1.0,fs)
    sos_c1=signal.zpk2sos(z_d,p_d,k_d,pairing='nearest')
    print(f"\n[C] matched-z: {sos_c1.shape[0]} sections",file=sys.stderr)
    gain_c1=norm_1k(sos_c1,fs)
    ec1,mc1,_=check(fs,sos_c1,gain_c1,C_REF,"C(raw mz)",quiet=True)
    print(f"[C] raw matched-z max={mc1:.3f}dB",file=sys.stderr)

    if mc1>C1:
        print(f"[C] adding shelf...",file=sys.stderr)
        shelf_c,_=optimize_shelf(sos_c1,C_REF,freqs_c,fs)
        sos_c=np.vstack([sos_c1,shelf_c]); gain_c=norm_1k(sos_c,fs)
    else:
        sos_c,gain_c=sos_c1,gain_c1
    ec,mc,_=check(fs,sos_c,gain_c,C_REF,"C-FINAL")

    # ===== Emit header =====
    out=["""/**
 * @file weighting_coefficients_matched_z_48k.hpp
 * @brief Matched-z A/C weighting for fs=48000 (v3.3.0)
 *
 * DESIGN: Matched-z transform + 2nd-order parametric correction
 *   Stage 1: z_p = exp(s_p·T) — pole positions EXACT
 *   Stage 2: 2nd-order RBJ peaking EQ — corrects HF gain shape
 * Post-chain: 1 kHz gain normalization factor applied ONCE
 *
 * TARGET: IEC 61672-1 Class 1 (±0.7 dB)
 * Generated by tools/regen_weighting_matched_z.py
 */
#pragma once
namespace noise_toolkit_mz {
struct Wt { float b0,b1,b2,a1,a2; };
struct Chain { const Wt* s; int n; float g; };
"""]
    out.append(emit_cpp("A_48000",sos_a)); out.append("")
    out.append(emit_cpp("C_48000",sos_c)); out.append("")
    out.append(f"static constexpr Chain A_CHAIN = {{ A_48000, {sos_a.shape[0]}, {gain_a:.10e}f }};")
    out.append(f"static constexpr Chain C_CHAIN = {{ C_48000, {sos_c.shape[0]}, {gain_c:.10e}f }};")
    out.append("} // namespace")
    sys.stdout.write("\n".join(out)+"\n")

    print("\n"+"="*60,file=sys.stderr)
    print(f"  A max={ma:.3f}dB  C max={mc:.3f}dB",file=sys.stderr)
    ok=ma<=C1 and mc<=C1
    print(f"  {'✓ IEC 61672-1 CLASS 1 ACHIEVED' if ok else '⚠ partial'}",file=sys.stderr)
    print("="*60,file=sys.stderr)

if __name__=="__main__": main()
