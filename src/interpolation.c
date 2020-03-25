/**
 * @file    stellar_evo.c
 * @brief   Interpolate particle parameters from passed dataset between timesteps in the simulation.
 * @author  Stanley A. Baronett <stanley.a.baronett@gmail.com>
 * 
 * @section     LICENSE
 * Copyright (c) 2015 Dan Tamayo, Hanno Rein
 *
 * This file is part of reboundx.
 *
 * reboundx is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * reboundx is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with rebound.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The section after the dollar signs gets built into the documentation by a script.  All lines must start with space * space like below.
 * Tables always must be preceded and followed by a blank line.  See http://docutils.sourceforge.net/docs/user/rst/quickstart.html for a primer on rst.
 * $$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
 *
 * $Mass Modifications$     // Effect category (must be the first non-blank line after dollar signs and between dollar signs to be detected by script). 
 * 
 * ======================= ===============================================
 * Authors                 Stanley A. Baronett
 * Implementation Paper    *In progress*
 * Based on                None
 * C Example               :ref:`c_example_stellar_evo`
 * Python Example          `StellarEvolution.ipynb <https://github.com/dtamayo/reboundx/blob/master/ipython_examples/StellarEvolution.ipynb>`_.
 * ======================= ===============================================
 * 
 * This interpolates particle parameter data for individual particles every timestep.
 * Set particles' ``mass_age``, ``mass_val``, and ``mass_2val`` as ``mass_n``-sized double arrays of time-mass values to be interpolated.
 * 
 * **Effect Parameters**
 * 
 * *None*
 * 
 * **Particle Parameters**
 * 
 * Only particles with their ``mass_age``, ``mass_val``, ``mass_2val``, and ``mass_n`` parameters set will have their masses affected.
 * 
 * ============================ =========== =======================================================
 * Name (C type)                Required    Description
 * ============================ =========== =======================================================
 * mass_age (double array)      Yes         Monotonic array of times in one-to-one correspondence with elements of ``mass_val``.
 * mass_val (double array)      Yes         Array of mass values in one-to-one correspondence with elements of ``mass_age``.
 * mass_2val (double array)     Yes         Uninitialized array, of size ``mass_n``, used for spline interpolation.
 * mass_n (int)                 Yes         Size of ``mass_age``, ``mass_val``, and ``mass_2val`` arrays. Mismatches will result in invalid interpolations (``mass_n`` < actual size) or segmentation faults (``mass_n`` > actual size).
 * ============================ =========== =======================================================
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "rebound.h"
#include "reboundx.h"
#include "core.h"

/**
 * Given a monotonic array x[0..(n-1)] and any array y[0..(n-1)],
 * sets y2[0..(n-1)] with second-order derivatives of the
 * interpolating function at the tabulated points x[i].
 * This routine assumes a "natural" spline, i.e. boundary
 * conditions with zero second derivatives at y2[0] and y2[(n-1)]. 
 * 
 * Adapted from "Numerical Recipes for C," 2nd Ed., §3.3, p. 115.
 */
static void rebx_spline(const double* x, const double* y, const int n, double* y2) {
    double p, qn, sig, un, u[n];

    y2[0] = 0.;
    u[0] = 0.0; // lower boundary condition is set to "natural"
    for (int i=1; i<n-1; i++) {
        // the decomposition loop of the tridiagonal algorithm.
        // y2 and u are used for temporary storage of the decompsed factors.
        sig = (x[i] - x[i-1])/(x[i+1] - x[i-1]);
        p = sig * y2[i-1] + 2.;
        y2[i] = (sig - 1.)/p;
        u[i] = (y[i+1] - y[i]) / (x[i+1] - x[i]) - (y[i] - y[i-1]) / (x[i] - x[i-1]);
        u[i] = (6.*u[i] / (x[i+1] - x[i-1]) - sig*u[i-1]) / p;
    }
    qn = 0.;
    un = 0.; // upper boundary condition is set to "natural"
    y2[n-1] = (un - qn*u[n-2]) / (qn * y2[n-2] + 1.);
    for (int k=n-2; k>=0; k--) // backsubstitution loop of tridiagonal alg.
        y2[k] = y2[k] * y2[k+1] + u[k];
}
/**
 * Given a monotonic array xa[0..(n-1)], any array ya[0..(n-1)], an array of
 * second derivatives y2a[0..(n-1)] outputted from spline() above, and a value
 * of x, this returns a cubic-spline interpolated value y.
 * "Splint" comes from spl(ine)-int(erpolation).
 * 
 * Adapted from "Numerical Recipes for C," 2nd Ed., §3.3, p. 116
 */
static double rebx_splint2(struct rebx_extras* const rebx, const double* xa, const double* ya, const double* y2a, const double x, int* klo) {
    double h, b, a;

    // since sequential calls are in increasing order,
    // find and update place for current and future calls
    while (*(xa+*klo+1) < x) *klo = *klo+1;
    h = *(xa+*klo+1) - *(xa+*klo);
    if (h == 0.0) { // xa's must be distinct
        rebx_error(rebx, "Cubic spline run-time error...\n");
        rebx_error(rebx, "Bad xa input to routine splint\n");
        rebx_error(rebx, "...now exiting to system...\n");
        return 0;
    }
    a = (*(xa+*klo+1) - x) / h;
    b = (x - *(xa+*klo)) / h;
    // evaluate cubic spline
    return a**(ya+*klo)+b**(ya+*klo+1)+((a*a*a-a)**(y2a+*klo)+(b*b*b-b)**(y2a+*klo+1))*(h*h)/6.;
}
static double rebx_splint(struct rebx_extras* const rebx, const double* xa, const double* ya, const double* y2a, const double x, int* klo) {
    double h, b, a;

    // since sequential calls are in increasing order,
    // find and update place for current and future calls
    while (xa[*klo+1] < x){
        *klo = *klo+1;
    }
    h = xa[*klo+1] - xa[*klo];
    if (h == 0.0) { // xa's must be distinct
        rebx_error(rebx, "Cubic spline run-time error...\n");
        rebx_error(rebx, "Bad xa input to routine splint\n");
        rebx_error(rebx, "...now exiting to system...\n");
        return 0;
    }
    a = (xa[*klo+1]-x) / h;
    b = (x - xa[*klo]) / h;
    // evaluate cubic spline
    return a*ya[*klo] + b*ya[*klo+1] + ((a*a*a-a)*y2a[*klo] + (b*b*b-b)*y2a[*klo+1])*(h*h)/6.;
}

struct rebx_interpolator* rebx_create_interpolator(struct rebx_extras* const rebx, const int Nvalues, const double* times, const double* values, enum rebx_interpolation_type interpolation){
    struct rebx_interpolator* interp = rebx_malloc(rebx, sizeof(*interp));
    rebx_init_interpolator(rebx, interp, Nvalues, times, values, interpolation);
    return interp;
}

void rebx_init_interpolator(struct rebx_extras* const rebx, struct rebx_interpolator* const interp, const int Nvalues, const double* times, const double* values, enum rebx_interpolation_type interpolation){
    interp->Nvalues = Nvalues;
    interp->interpolation = interpolation;
    interp->times = calloc(Nvalues, sizeof(*interp->times));
    interp->values = calloc(Nvalues, sizeof(*interp->values));
    memcpy(interp->times, times, Nvalues*sizeof(*interp->times));
    memcpy(interp->values, values, Nvalues*sizeof(*interp->values));
    interp->y2 = NULL;
    interp->klo = 0;
    if (interpolation == REBX_INTERPOLATION_SPLINE){
        interp->y2 = rebx_malloc(rebx, Nvalues*sizeof(*interp->y2));
        rebx_spline(interp->times, interp->values, interp->Nvalues, interp->y2);
    }
    return;
}

void rebx_free_interpolator_pointers(struct rebx_interpolator* const interpolator){
    free(interpolator->times); 
    free(interpolator->values);
    if (interpolator->y2 != NULL){
        free(interpolator->y2);
    }
    return;
}
void rebx_free_interpolator(struct rebx_interpolator* const interpolator){
    rebx_free_interpolator_pointers(interpolator);
    free(interpolator);
    return;
}
   
// Assumes all passed pointers are not NULL
// Interp value at t=time from an array of times and values
double rebx_interpolate(struct rebx_extras* const rebx, struct rebx_interpolator* const interpolator, const double time){
    switch (interpolator->interpolation){
        case REBX_INTERPOLATION_NONE:
        {
            return 0; // UPDATE
        }
        case REBX_INTERPOLATION_SPLINE:
        {
            return rebx_splint(rebx, interpolator->times, interpolator->values, interpolator->y2, time, &interpolator->klo);       // interpolate at passed time
        }
        default:
        {
            rebx_error(rebx, "REBOUNDx Error: Interpolation option not supported\n");
            return 0;
        }
    }
}