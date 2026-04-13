#!/usr/bin/env python3
"""
Generate ERFA reference values for ECEF/ECI regression tests.

This script is used at development time only (not shipped with the install).
It imports the Python `erfa` package (the same ERFA library vendored into
liblwgeom/erfa/) and computes expected output coordinates for a fixed set
of test cases. The output is printed as inline SQL that can be copy-pasted
into regress/core/ecef_eci_iau2006.sql.

The regression test then asserts that PostGIS's IAU 2006/2000A transforms
agree with the ERFA reference to within 1 micrometer at Earth radius scale.

Run:
    python3 ci/generate_erfa_reference.py

Requires:
    pyerfa (pip install pyerfa)
"""
import numpy as np
import erfa


def mjd_two_part(year: float) -> tuple[float, float]:
    """Convert a decimal year to a two-part Julian date (jd1, jd2)."""
    jd_full = 2451545.0 + (year - 2000.0) * 365.25
    jd1 = 2400000.5  # MJD epoch
    jd2 = jd_full - 2400000.5
    return jd1, jd2


def build_c2t(year: float, frame: str, dut1=0.0, xp=0.0, yp=0.0, dx=0.0, dy=0.0):
    """Build the celestial-to-terrestrial matrix for a frame at a given epoch.

    Mirrors liblwgeom/lwgeom_eci.c:lweci_build_c2t_matrix exactly.
    """
    jd1_tt, jd2_tt = mjd_two_part(year)
    jd1_ut1 = jd1_tt
    jd2_ut1 = jd2_tt + dut1 / 86400.0

    arcsec_to_rad = np.pi / (180.0 * 3600.0)
    xp_rad = xp * arcsec_to_rad
    yp_rad = yp * arcsec_to_rad
    dx_rad = dx * arcsec_to_rad
    dy_rad = dy * arcsec_to_rad

    # CIP X, Y from IAU 2006 series + CIP offset corrections
    x, y = erfa.xy06(jd1_tt, jd2_tt)
    x = x + dx_rad
    y = y + dy_rad

    # CIO locator s
    s = erfa.s06(jd1_tt, jd2_tt, x, y)

    # Celestial-to-intermediate matrix
    rc2i = erfa.c2ixys(x, y, s)

    # Polar motion matrix
    sp = erfa.sp00(jd1_tt, jd2_tt)
    rpom = erfa.pom00(xp_rad, yp_rad, sp)

    # Base ICRF celestial-to-terrestrial
    rc2t = erfa.c2tcio(rc2i, erfa.era00(jd1_ut1, jd2_ut1), rpom)

    # Frame-specific adjustments
    if frame == "ICRF":
        pass
    elif frame == "J2000":
        dpsibi, depsbi, dra0 = erfa.bi00()
        eps0 = erfa.obl06(jd1_tt, jd2_tt)
        # Build the frame bias matrix: R3(dra0) * R2(-dpsibi*sin(eps0)) * R1(depsbi)
        rbias = np.eye(3)
        # Apply Rz(dra0) - post-multiply equivalent
        c, s_ = np.cos(dra0), np.sin(dra0)
        rz = np.array([[c, s_, 0], [-s_, c, 0], [0, 0, 1]])
        rbias = rz @ rbias
        ang = -dpsibi * np.sin(eps0)
        c, s_ = np.cos(ang), np.sin(ang)
        ry = np.array([[c, 0, -s_], [0, 1, 0], [s_, 0, c]])
        rbias = ry @ rbias
        c, s_ = np.cos(depsbi), np.sin(depsbi)
        rx = np.array([[1, 0, 0], [0, c, s_], [0, -s_, c]])
        rbias = rx @ rbias
        rbias_t = rbias.T
        rc2t = rc2t @ rbias_t
    elif frame == "TEME":
        gmst = erfa.gmst06(jd1_ut1, jd2_ut1, jd1_tt, jd2_tt)
        rc2t = erfa.c2tcio(rc2i, gmst, rpom)
    else:
        raise ValueError(f"unknown frame {frame}")

    return rc2t


def ecef_from_eci(eci_xyz, year, frame, **eop):
    rc2t = build_c2t(year, frame, **eop)
    return rc2t @ np.asarray(eci_xyz)


def eci_from_ecef(ecef_xyz, year, frame, **eop):
    rc2t = build_c2t(year, frame, **eop)
    return rc2t.T @ np.asarray(ecef_xyz)


def format_case(label: str, x: float, y: float, z: float, tolerance: float = 1e-6):
    """Emit a SQL assertion line."""
    return (f"  ('{label}', {x:.9f}, {y:.9f}, {z:.9f}, {tolerance})")


def main():
    print("-- Auto-generated ERFA reference values for ECEF/ECI regression tests.")
    print("-- DO NOT EDIT: regenerate via `python3 ci/generate_erfa_reference.py`.")
    print("-- Source: pyerfa", erfa.__version__)
    print()

    # Test ECEF input point (near Earth surface, off-equator, off-pole)
    test_ecef = np.array([4000000.0, 3000000.0, 4500000.0])

    epochs = [
        ("2000.0", 2000.0),       # J2000.0
        ("2024.5", 2024.5),       # Mid-2024
        ("2030.0", 2030.0),       # Future
    ]
    frames = ["ICRF", "J2000", "TEME"]

    print("-- Test case format:")
    print("-- (label, expected_x, expected_y, expected_z, tolerance)")
    print()
    print("-- ECEF -> ECI (zero EOP corrections)")
    for epoch_label, year in epochs:
        for frame in frames:
            eci = eci_from_ecef(test_ecef, year, frame)
            label = f"ecef_to_eci_{frame.lower()}_{epoch_label.replace('.', '_')}"
            print(format_case(label, *eci))
    print()

    print("-- ECI -> ECEF round-trip: |eci_to_ecef(ecef_to_eci(p))| should equal |p|")
    for epoch_label, year in epochs:
        for frame in frames:
            eci = eci_from_ecef(test_ecef, year, frame)
            ecef_back = ecef_from_eci(eci, year, frame)
            label = f"roundtrip_{frame.lower()}_{epoch_label.replace('.', '_')}"
            print(format_case(label, *ecef_back))
    print()

    # Frame differentiation: all three frames at 2024.5
    print("-- Frame differentiation at 2024.5: ICRF vs J2000 vs TEME should differ")
    eci_icrf = eci_from_ecef(test_ecef, 2024.5, "ICRF")
    eci_j2000 = eci_from_ecef(test_ecef, 2024.5, "J2000")
    eci_teme = eci_from_ecef(test_ecef, 2024.5, "TEME")
    print(f"-- ICRF-J2000 diff: {np.linalg.norm(eci_icrf - eci_j2000):.6f} m")
    print(f"-- ICRF-TEME diff: {np.linalg.norm(eci_icrf - eci_teme):.6f} m")
    print(f"-- J2000-TEME diff: {np.linalg.norm(eci_j2000 - eci_teme):.6f} m")


if __name__ == "__main__":
    main()
